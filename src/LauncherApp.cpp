#include "LauncherApp.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "EnvConfig.h"
#include "ParamSchema.h"
#include "Sampler.h"
#include "WebRenderer.h"
#include "WinFs.h"
#include "mods_registry.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 把 body（key=value 行，UTF-8）解析为键值表；未知/格式错误行跳过。
std::map<std::string, std::string> parseKeyValues(const std::string& body) {
    std::map<std::string, std::string> out;
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        out[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
    }
    return out;
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string item;
    while (std::getline(is, item, ',')) {
        std::string t = trim(item);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// 存档/材质包名安全检查：拒绝路径分隔符与 Windows 非法字符。
bool safeSaveName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name[0] == '.') return false;
    if (name.find("..") != std::string::npos) return false;
    static const char* bad = "/\\:*?\"<>|";
    for (char c : name) {
        for (const char* p = bad; *p; ++p) {
            if (c == *p) return false;
        }
    }
    return true;
}

// 标准 base64 解码（忽略空白与 '='）。
std::string base64Decode(const std::string& in) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val[256];
    for (int i = 0; i < 256; ++i) val[i] = -1;
    for (int i = 0; T[i]; ++i) val[(unsigned char)T[i]] = i;
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\r' || c == '\n') continue;
        int v = val[(unsigned char)c];
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// 读取行式 key=value 元信息文件。
std::map<std::string, std::string> readMetaFile(const std::string& path) {
    return parseKeyValues([&]() {
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }());
}
}  // namespace

LauncherApp::LauncherApp(std::string webRoot, std::string baseDir, int port)
    : webRoot_(std::move(webRoot)), baseDir_(std::move(baseDir)), port_(port) {
    // 启动时载入"当前"配置：exe 同目录 env_config.txt → 当前目录 env_config.txt。
    std::string envPath;
    {
        std::string c = baseDir_ + "/env_config.txt";
        std::ifstream f(WinFs::toAnsi(c));
        if (f.good()) envPath = c;
    }
    if (envPath.empty()) {
        std::ifstream f("env_config.txt");
        if (f.good()) envPath = "env_config.txt";
    }
    if (!envPath.empty()) {
        EnvConfig::load(WinFs::toAnsi(envPath), pendingCfg_);
        std::cout << "已加载环境配置: " << envPath << "\n";
    }
    // mods.list：有序启用清单（顺序 = 优先级）。
    enabledMods_ = readModsList(WinFs::toAnsi(baseDir_ + "/mods.list"));
    if (!enabledMods_.empty()) {
        std::cout << "已加载 mod 清单（" << enabledMods_.size() << " 个）\n";
    }
    // packs.list：材质包堆叠顺序（后者覆盖前者）。
    {
        std::ifstream f(WinFs::toAnsi(baseDir_ + "/packs.list"));
        std::string line;
        while (std::getline(f, line)) {
            std::string t = trim(line);
            if (!t.empty() && t[0] != '#') enabledPacks_.push_back(t);
        }
        if (!enabledPacks_.empty()) {
            std::cout << "已加载材质包清单（" << enabledPacks_.size() << " 个）\n";
        }
    }
}

LauncherApp::~LauncherApp() { stop(); }

int LauncherApp::start(bool openBrowser) {
    server_.setWebRoot(webRoot_);
    server_.setStateProvider([this]() { return snapshotBytes(); });
    server_.setControlHandler([this](const std::string& cmd) { onControl(cmd); });
    server_.setRouter([this](const std::string& m, const std::string& p, const std::string& b,
                             std::string& resp, std::string& ct, int& st) {
        return route(m, p, b, resp, ct, st);
    });
    int p = server_.start(port_);
    if (p < 0) return -1;
    port_ = p;
    std::string url = "http://127.0.0.1:" + std::to_string(p) + "/";
    std::cout << "启动器地址: " << url << "\n" << std::flush;
#ifdef _WIN32
    if (openBrowser) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#endif
    return p;
}

void LauncherApp::stop() {
    exitRequested_.store(true);
    server_.stop();
}

void LauncherApp::runMainLoop() {
    using clock = std::chrono::steady_clock;
    while (!exitRequested_.load()) {
        State st = (State)state_.load();

        // 存档请求（运行/结束态均可保存；世界只由主线程访问）。
        {
            std::lock_guard<std::mutex> lk(saveMutex_);
            if (savePending_) {
                std::string name = saveRequest_;
                std::string err;
                saveOk_ = doSave(name, err);
                saveErr_ = err;
                savePending_ = false;
                saveCond_.notify_all();
            }
        }

        if (st == State::Menu) {
            std::string loadName;
            std::map<std::string, std::string> params;
            if (takeLoadRequest(loadName)) {
                std::string err;
                loadWorldFromSave(loadName, err);
                if (!err.empty()) {
                    std::cerr << "读档失败: " << err << "\n";
                } else {
                    paused_.store(true);  // 读档后停在暂停态供查看
                    state_.store((int)State::Running);
                }
            } else if (takeRunRequest(params)) {
                createWorld(params);
                state_.store((int)State::Running);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (st == State::Finished) {
            if (toMenuRequested_.exchange(false)) {
                destroyWorld();
                state_.store((int)State::Menu);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        // ---- Running ----
        if (stopRequested_.exchange(false)) {
            world_->stopNow("用户停止");
            web_->render(*world_);
            doEnding();
            finishReason_ = world_->finishReason();
            state_.store((int)State::Finished);
            continue;
        }
        if (world_->finished()) {
            web_->render(*world_);  // 最终快照（含结束原因）
            doEnding();
            finishReason_ = world_->finishReason();
            state_.store((int)State::Finished);
            continue;
        }
        if (paused_.load()) {
            if (!consumeStep()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        } else {
            consumeStep();
        }
        auto t0 = clock::now();
        world_->step();
        const WorldConfig& cfg = world_->config();
        if (cfg.renderInterval > 0 && world_->frame() % cfg.renderInterval == 0) {
            web_->render(*world_);
        }
        if (cfg.sampleInterval > 0 && world_->frame() % cfg.sampleInterval == 0) {
            sampler_->sample(*world_);
        }
        double maxFps = maxFps_.load();
        if (maxFps > 0.0) {
            auto elapsed = clock::now() - t0;
            auto dur = std::chrono::duration<double>(1.0 / maxFps);
            if (elapsed < dur) {
                std::this_thread::sleep_for(dur - elapsed);
            }
        }
    }
    destroyWorld();
}

// ---------- 主线程助手 ----------

bool LauncherApp::takeRunRequest(std::map<std::string, std::string>& out) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!runPending_) return false;
    out.swap(runRequest_);
    runPending_ = false;
    return true;
}

void LauncherApp::createWorld(const std::map<std::string, std::string>& params) {
    WorldConfig cfg = pendingCfg_;  // 参数页当前配置为基准
    for (const auto& kv : params) {
        ParamSchema::applyLine(cfg, kv.first, kv.second);
    }
    world_ = std::make_unique<World>(cfg);
    world_->initialize();
    modApi_ = std::make_unique<ModAPI>(
        ModAPI{*world_, world_->energy(), world_->rng(), world_->events()});
    std::lock_guard<std::mutex> lk(mutex_);
    registerAllMods(*modApi_, enabledMods_);  // 顺序 = mods.list 顺序 = 优先级
    web_ = std::make_unique<WebRenderer>(*world_);
    sampler_ = std::make_unique<Sampler>(".");
    paused_.store(false);
    singleStep_.store(0);
    std::cout << "模拟启动 | 世界 " << cfg.width << "x" << cfg.height
              << " | 小球 " << cfg.initialBalls << " | 核 " << cfg.initialNuclei
              << " | 帧数 " << cfg.maxFrames << " | 种子 " << cfg.seed << "\n" << std::flush;
}

void LauncherApp::destroyWorld() {
    // 先销毁依赖 world 的对象（mod 回调持 world 引用），再销毁 world。
    web_.reset();
    sampler_.reset();
    modApi_.reset();
    world_.reset();
    state_.store((int)State::Menu);
}

void LauncherApp::doEnding() {
    std::cout << "\n===== 模拟结束 =====\n";
    std::cout << "结束原因: " << world_->finishReason() << "\n";
    std::cout << "最终帧号: " << world_->frame() << "\n";
    std::cout << "存活核数: " << world_->aliveNucleusCount() << "\n";
    std::cout << "小球总数: " << world_->balls().size() << "\n";
    std::cout << "平均核能量: " << std::fixed << std::setprecision(2)
              << world_->averageNucleusEnergy() << "\n";
    if (world_->aliveNucleusCount() > 0) {
        sampler_->writeSurvivors(*world_);
        std::cout << "存活核参数已导出到 survivors.csv\n";
    }
}

bool LauncherApp::consumeStep() {
    int v = singleStep_.load();
    while (v > 0 && !singleStep_.compare_exchange_weak(v, v - 1)) {
    }
    return v > 0;
}

// ---------- 请求处理（服务器线程）----------

void LauncherApp::onControl(const std::string& cmd) {
    if (cmd == "pause") paused_.store(true);
    else if (cmd == "resume") paused_.store(false);
    else if (cmd == "step") singleStep_.fetch_add(1);
    else if (cmd == "stop") stopRequested_.store(true);
    else if (cmd == "menu") toMenuRequested_.store(true);
    else if (cmd == "exit") exitRequested_.store(true);
    else if (cmd.rfind("maxfps:", 0) == 0) {
        try {
            maxFps_.store(std::stod(cmd.substr(7)));
        } catch (...) {
            maxFps_.store(0.0);
        }
    }
}

void LauncherApp::onRun(const std::string& body) {
    if (state_.load() != (int)State::Menu) return;  // 仅菜单态接受启动请求
    std::map<std::string, std::string> params = parseKeyValues(body);
    std::lock_guard<std::mutex> lk(mutex_);
    runRequest_ = std::move(params);
    runPending_ = true;
}

void LauncherApp::onSaveConfig(const std::string& body) {
    std::map<std::string, std::string> params = parseKeyValues(body);
    std::lock_guard<std::mutex> lk(mutex_);
    // 先更新内存中的"当前"配置，再写出完整 env_config.txt。
    for (const auto& kv : params) {
        ParamSchema::applyLine(pendingCfg_, kv.first, kv.second);
    }
    ParamSchema::writeEnvFile(baseDir_ + "/env_config.txt", pendingCfg_, params);
    std::cout << "参数已保存为默认（env_config.txt）\n" << std::flush;
}

void LauncherApp::onSetMods(const std::string& body) {
    std::vector<std::string> mods;
    for (const auto& kv : parseKeyValues(body)) {
        if (kv.first == "mods") mods = splitList(kv.second);
    }
    std::lock_guard<std::mutex> lk(mutex_);
    enabledMods_ = mods;
    std::ofstream f(baseDir_ + "/mods.list");
    for (const std::string& id : mods) {
        f << id << "\n";
    }
    std::cout << "mod 清单已更新（下次启动模拟生效）\n" << std::flush;
}

// ---------- HTTP 路由 ----------

bool LauncherApp::route(const std::string& method, const std::string& path,
                        const std::string& body, std::string& resp,
                        std::string& contentType, int& status) {
    (void)status;
    if (method == "GET" && path == "/status") {
        resp = statusText();
        contentType = "text/plain; charset=utf-8";
        return true;
    }
    if (method == "GET" && path == "/schema") {
        resp = buildSchemaJson();
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "GET" && path == "/mods") {
        resp = buildModsJson();
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/run") {
        onRun(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/save-config") {
        onSaveConfig(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/set-mods") {
        onSetMods(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "GET" && path == "/saves") {
        resp = buildSavesJson();
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/save") {
        onSaveRequest(body);  // 阻塞等待主线程完成（≤2s）
        std::lock_guard<std::mutex> lk(saveMutex_);
        resp = saveOk_ ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"" + jsonEscape(saveErr_) + "\"}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/load") {
        onLoadRequest(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/delete-save") {
        onDeleteSave(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/rename-save") {
        onRenameSave(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/save-thumb") {
        onSaveThumb(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "GET" && path == "/packs") {
        resp = buildPacksJson();
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/set-packs") {
        onSetPacks(body);
        resp = "{\"ok\":true}";
        contentType = "application/json; charset=utf-8";
        return true;
    }
    // 存档静态文件（缩略图等）：/savefile/<name>/<file>
    if (method == "GET" && path.rfind("/savefile/", 0) == 0) {
        std::string rest = path.substr(10);
        size_t slash = rest.find('/');
        if (slash == std::string::npos) return false;
        std::string name = rest.substr(0, slash);
        std::string file = rest.substr(slash + 1);
        if (!safeSaveName(name) || file.empty() || file.find("..") != std::string::npos) {
            status = 400;
            resp = "bad path";
            contentType = "text/plain; charset=utf-8";
            return true;
        }
        std::string full = baseDir_ + "/saves/" + name + "/" + file;
        std::ifstream f(WinFs::toAnsi(full), std::ios::binary);
        if (!f.good()) {
            status = 404;
            resp = "not found";
            contentType = "text/plain; charset=utf-8";
            return true;
        }
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        resp = data;
        contentType = file.rfind(".png") == file.size() - 4 ? "image/png" : "application/octet-stream";
        return true;
    }
    // 材质包静态文件：/rp/<name>/<file>
    if (method == "GET" && path.rfind("/rp/", 0) == 0) {
        std::string rest = path.substr(4);
        size_t slash = rest.find('/');
        if (slash == std::string::npos) return false;
        std::string name = rest.substr(0, slash);
        std::string file = rest.substr(slash + 1);
        if (!safeSaveName(name) || file.empty() || file.find("..") != std::string::npos) {
            status = 400;
            resp = "bad path";
            contentType = "text/plain; charset=utf-8";
            return true;
        }
        std::string full = baseDir_ + "/resourcepacks/" + name + "/" + file;
        std::ifstream f(WinFs::toAnsi(full), std::ios::binary);
        if (!f.good()) {
            status = 404;
            resp = "not found";
            contentType = "text/plain; charset=utf-8";
            return true;
        }
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        resp = data;
        contentType = file.rfind(".txt") == file.size() - 4 ? "text/plain; charset=utf-8"
                                                            : "application/octet-stream";
        return true;
    }
    return false;
}

std::string LauncherApp::statusText() const {
    State st = (State)state_.load();
    switch (st) {
        case State::Menu: return "menu";
        case State::Running:
            return paused_.load() ? "paused" : "running";
        case State::Finished: return "finished:" + finishReason_;
    }
    return "menu";
}

std::string LauncherApp::buildSchemaJson() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::string out = "[";
    bool first = true;
    for (const ParamDef& d : ParamSchema::all()) {
        if (!first) out += ",";
        first = false;
        const char* typeName = "double";
        if (d.type == ParamDef::Type::Int) typeName = "int";
        else if (d.type == ParamDef::Type::Bool) typeName = "bool";
        else if (d.type == ParamDef::Type::Pair) typeName = "pair";
        char num[64];
        out += "{\"key\":\"" + std::string(d.key) + "\"";
        out += ",\"group\":\"" + jsonEscape(d.group) + "\"";
        out += ",\"desc\":\"" + jsonEscape(d.desc) + "\"";
        out += ",\"type\":\"" + std::string(typeName) + "\"";
        std::snprintf(num, sizeof(num), "%g", d.min);
        out += ",\"min\":" + std::string(num);
        std::snprintf(num, sizeof(num), "%g", d.max);
        out += ",\"max\":" + std::string(num);
        out += ",\"dflt\":\"" + jsonEscape(ParamSchema::valueString(
                                   WorldConfig{}, d)) + "\"";
        out += ",\"cur\":\"" + jsonEscape(ParamSchema::valueString(pendingCfg_, d)) + "\"";
        out += "}";
    }
    out += "]";
    return out;
}

std::string LauncherApp::buildModsJson() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::string out = "{\"mods\":[";
    const auto& mods = allMods();
    bool first = true;
    for (const ModInfo& m : mods) {
        if (!first) out += ",";
        first = false;
        bool enabled = false;
        int order = -1;
        for (size_t i = 0; i < enabledMods_.size(); ++i) {
            if (enabledMods_[i] == m.id) {
                enabled = true;
                order = (int)i;
                break;
            }
        }
        out += "{\"id\":\"" + std::string(m.id) + "\"";
        out += ",\"name\":\"" + jsonEscape(m.name) + "\"";
        out += ",\"desc\":\"" + jsonEscape(m.desc) + "\"";
        out += ",\"touches\":\"" + jsonEscape(m.touches) + "\"";
        out += ",\"incompatible\":\"" + jsonEscape(m.incompatible) + "\"";
        out += ",\"enabled\":" + std::string(enabled ? "true" : "false");
        out += ",\"order\":" + std::to_string(order);
        out += "}";
    }
    // 冲突检查：启用的 mod 两两比对 touches 交集与 incompatible。
    out += "],\"warnings\":[";
    first = true;
    for (size_t i = 0; i < enabledMods_.size(); ++i) {
        for (size_t j = i + 1; j < enabledMods_.size(); ++j) {
            const ModInfo* a = nullptr;
            const ModInfo* b = nullptr;
            for (const ModInfo& m : mods) {
                if (m.id == enabledMods_[i]) a = &m;
                if (m.id == enabledMods_[j]) b = &m;
            }
            if (!a || !b) continue;
            std::string warnType;
            std::string text;
            auto touchesA = splitList(a->touches);
            auto touchesB = splitList(b->touches);
            bool bothHard = false;
            for (const std::string& x : touchesA) {
                for (const std::string& y : touchesB) {
                    if (x == y) bothHard = true;
                }
            }
            auto incompatA = splitList(a->incompatible);
            auto incompatB = splitList(b->incompatible);
            bool hardIncompat = false;
            for (const std::string& x : incompatA) {
                if (x == b->id) hardIncompat = true;
            }
            for (const std::string& x : incompatB) {
                if (x == a->id) hardIncompat = true;
            }
            if (hardIncompat) {
                warnType = "error";
                text = std::string(a->name) + " 与 " + b->name + " 互斥（incompatible）";
            } else if (bothHard) {
                warnType = "warn";
                text = std::string(a->name) + " 与 " + b->name +
                       " 影响相同的机制，结果取决于优先级顺序（列表中越靠前越先执行）";
            }
            if (!warnType.empty()) {
                if (!first) out += ",";
                first = false;
                out += "{\"type\":\"" + warnType + "\",\"text\":\"" + jsonEscape(text) + "\"}";
            }
        }
    }
    out += "]}";
    return out;
}

std::vector<uint8_t> LauncherApp::snapshotBytes() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (web_) return web_->snapshotBytes();
    return {0};  // 菜单态：无效快照（前端按 magic 忽略）
}

// ==================== 存档（Phase D） ====================

bool LauncherApp::takeLoadRequest(std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!loadPending_) return false;
    name = loadName_;
    loadPending_ = false;
    return true;
}

void LauncherApp::loadWorldFromSave(const std::string& name, std::string& err) {
    if (!safeSaveName(name)) {
        err = "存档名包含非法字符";
        return;
    }
    std::string path = baseDir_ + "/saves/" + name + "/save.bin";
    std::ifstream f(WinFs::toAnsi(path), std::ios::binary);
    if (!f.good()) {
        err = "存档不存在";
        return;
    }
    destroyWorld();
    WorldConfig cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        world_ = std::make_unique<World>(cfg);
        if (!world_->readState(f, err)) {
            world_.reset();
            return;
        }
        // mod 状态段（占位钩子：无状态 mod 跳过）。
        uint32_t modCount = 0;
        f.read((char*)&modCount, 4);
        if (f.good() && modCount <= 100) {
            for (uint32_t i = 0; i < modCount; ++i) {
                uint32_t idLen = 0;
                f.read((char*)&idLen, 4);
                if (!f.good() || idLen > 256) break;
                std::string id((size_t)idLen, '\0');
                f.read(&id[0], (std::streamsize)idLen);
                for (const ModInfo& m : allMods()) {
                    if (m.id == id && m.loadState) m.loadState(f);
                }
            }
        }
        modApi_ = std::make_unique<ModAPI>(
            ModAPI{*world_, world_->energy(), world_->rng(), world_->events()});
        registerAllMods(*modApi_, enabledMods_);
        web_ = std::make_unique<WebRenderer>(*world_);
        sampler_ = std::make_unique<Sampler>(".");
    }
    paused_.store(true);
    singleStep_.store(0);
    std::cout << "已载入存档: " << name << "（帧 " << world_->frame() << "）\n" << std::flush;
}

bool LauncherApp::doSave(const std::string& name, std::string& err) {
    if (!world_) {
        err = "没有运行中的模拟";
        return false;
    }
    if (!safeSaveName(name)) {
        err = "存档名包含非法字符";
        return false;
    }
    std::string dir = baseDir_ + "/saves/" + name;
    if (!WinFs::mkdirs(dir)) {
        err = "目录创建失败";
        return false;
    }
    std::ofstream f(WinFs::toAnsi(dir + "/save.bin"), std::ios::binary);
    if (!f.good()) {
        err = "无法创建存档文件";
        return false;
    }
    if (!world_->writeState(f)) {
        err = "世界状态写入失败";
        return false;
    }
    // mod 状态段：写入带 saveState 钩子的启用 mod（按优先级顺序）。
    std::vector<const ModInfo*> withState;
    for (const std::string& id : enabledMods_) {
        for (const ModInfo& m : allMods()) {
            if (m.id == id && m.saveState) withState.push_back(&m);
        }
    }
    uint32_t n = (uint32_t)withState.size();
    f.write((const char*)&n, 4);
    for (const ModInfo* m : withState) {
        uint32_t idLen = (uint32_t)std::strlen(m->id);
        f.write((const char*)&idLen, 4);
        f.write(m->id, (std::streamsize)idLen);
        m->saveState(f);
    }
    f.close();
    if (!f.good()) {
        err = "存档写入失败";
        return false;
    }
    // meta.txt（行式 key=value）
    std::ofstream mf(WinFs::toAnsi(dir + "/meta.txt"));
    std::time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    const WorldConfig& cfg = world_->config();
    mf << "name = " << name << "\n";
    mf << "created = " << timeBuf << "\n";
    mf << "frame = " << world_->frame() << "\n";
    mf << "seed = " << cfg.seed << "\n";
    mf << "balls = " << cfg.initialBalls << "\n";
    mf << "nuclei = " << cfg.initialNuclei << "\n";
    mf << "width = " << cfg.width << "\n";
    mf << "height = " << cfg.height << "\n";
    mf << "alive = " << world_->aliveNucleusCount() << "\n";
    std::string mods;
    for (size_t i = 0; i < enabledMods_.size(); ++i) {
        if (i) mods += ",";
        mods += enabledMods_[i];
    }
    mf << "mods = " << mods << "\n";
    mf.close();
    std::cout << "已保存存档: " << name << "（帧 " << world_->frame() << "）\n" << std::flush;
    return true;
}

std::string LauncherApp::buildSavesJson() {
    std::string out = "[";
    bool first = true;
    std::string savesDir = baseDir_ + "/saves";
    if (WinFs::exists(savesDir)) {
        for (const std::string& name : WinFs::listDirs(savesDir)) {
            auto meta = readMetaFile(WinFs::toAnsi(savesDir + "/" + name + "/meta.txt"));
            if (meta.empty()) continue;
            bool hasThumb = WinFs::exists(savesDir + "/" + name + "/thumb.png");
            if (!first) out += ",";
            first = false;
            out += "{\"name\":\"" + jsonEscape(name) + "\"";
            out += ",\"created\":\"" + jsonEscape(meta.count("created") ? meta["created"] : "") + "\"";
            out += ",\"frame\":" + (meta.count("frame") ? meta["frame"] : "0");
            out += ",\"seed\":" + (meta.count("seed") ? meta["seed"] : "0");
            out += ",\"balls\":" + (meta.count("balls") ? meta["balls"] : "0");
            out += ",\"nuclei\":" + (meta.count("nuclei") ? meta["nuclei"] : "0");
            out += ",\"alive\":" + (meta.count("alive") ? meta["alive"] : "0");
            out += ",\"mods\":\"" + jsonEscape(meta.count("mods") ? meta["mods"] : "") + "\"";
            out += ",\"thumb\":" + std::string(hasThumb ? "true" : "false");
            out += "}";
        }
    }
    out += "]";
    return out;
}

void LauncherApp::onSaveRequest(const std::string& body) {
    std::string name;
    for (const auto& kv : parseKeyValues(body)) {
        if (kv.first == "name") name = kv.second;
    }
    {
        std::lock_guard<std::mutex> lk(saveMutex_);
        saveRequest_ = name;
        savePending_ = true;
        saveOk_ = false;
        saveErr_ = "存档超时（可能没有运行中的模拟）";
    }
    std::unique_lock<std::mutex> lk(saveMutex_);
    saveCond_.wait_for(lk, std::chrono::milliseconds(2000),
                       [this] { return !savePending_; });
}

void LauncherApp::onLoadRequest(const std::string& body) {
    if (state_.load() != (int)State::Menu) return;  // 仅菜单态接受读档
    for (const auto& kv : parseKeyValues(body)) {
        if (kv.first == "name" && safeSaveName(kv.second)) {
            std::lock_guard<std::mutex> lk(mutex_);
            loadName_ = kv.second;
            loadPending_ = true;
        }
    }
}

void LauncherApp::onDeleteSave(const std::string& body) {
    for (const auto& kv : parseKeyValues(body)) {
        if (kv.first == "name" && safeSaveName(kv.second)) {
            WinFs::removeAll(baseDir_ + "/saves/" + kv.second);
            std::cout << "已删除存档: " << kv.second << "\n" << std::flush;
        }
    }
}

void LauncherApp::onRenameSave(const std::string& body) {
    std::string oldName, newName;
    for (const auto& kv : parseKeyValues(body)) {
        if (kv.first == "old") oldName = kv.second;
        else if (kv.first == "new") newName = kv.second;
    }
    if (!safeSaveName(oldName) || !safeSaveName(newName)) return;
    if (!WinFs::renamePath(baseDir_ + "/saves/" + oldName, baseDir_ + "/saves/" + newName)) {
        return;
    }
    // 同步 meta 中的名字
    auto meta = readMetaFile(WinFs::toAnsi(baseDir_ + "/saves/" + newName + "/meta.txt"));
    meta["name"] = newName;
    std::ofstream mf(WinFs::toAnsi(baseDir_ + "/saves/" + newName + "/meta.txt"));
    for (const auto& kv : meta) mf << kv.first << " = " << kv.second << "\n";
    std::cout << "存档已重命名: " << oldName << " → " << newName << "\n" << std::flush;
}

void LauncherApp::onSaveThumb(const std::string& body) {
    // 首行为 name=...，其余为 base64 PNG 数据。
    size_t nl = body.find('\n');
    std::string name;
    std::string b64;
    if (nl == std::string::npos) {
        name = body;
    } else {
        std::string firstLine = body.substr(0, nl);
        b64 = body.substr(nl + 1);
        for (const auto& kv : parseKeyValues(firstLine)) {
            if (kv.first == "name") name = kv.second;
        }
    }
    if (!safeSaveName(name)) return;
    std::string data = base64Decode(b64);
    if (data.size() < 8) return;
    std::ofstream f(WinFs::toAnsi(baseDir_ + "/saves/" + name + "/thumb.png"), std::ios::binary);
    f.write(data.data(), (std::streamsize)data.size());
}

// ==================== 材质包（Phase E） ====================

std::string LauncherApp::buildPacksJson() {
    std::string out = "[";
    bool first = true;
    std::string packsDir = baseDir_ + "/resourcepacks";
    if (WinFs::exists(packsDir)) {
        for (const std::string& name : WinFs::listDirs(packsDir)) {
            auto meta = readMetaFile(WinFs::toAnsi(packsDir + "/" + name + "/pack.txt"));
            if (meta.empty() && !WinFs::exists(packsDir + "/" + name + "/pack.txt")) continue;
            bool enabled = false;
            int order = -1;
            for (size_t i = 0; i < enabledPacks_.size(); ++i) {
                if (enabledPacks_[i] == name) {
                    enabled = true;
                    order = (int)i;
                    break;
                }
            }
            if (!first) out += ",";
            first = false;
            out += "{\"name\":\"" + jsonEscape(name) + "\"";
            out += ",\"desc\":\"" + jsonEscape(meta.count("desc") ? meta["desc"] : "") + "\"";
            out += ",\"version\":\"" + jsonEscape(meta.count("version") ? meta["version"] : "") + "\"";
            out += ",\"author\":\"" + jsonEscape(meta.count("author") ? meta["author"] : "") + "\"";
            out += ",\"enabled\":" + std::string(enabled ? "true" : "false");
            out += ",\"order\":" + std::to_string(order);
            out += "}";
        }
    }
    out += "]";
    return out;
}

void LauncherApp::onSetPacks(const std::string& body) {
    std::vector<std::string> packs;
    for (const auto& kv : parseKeyValues(body)) {
        if (kv.first == "packs") packs = splitList(kv.second);
    }
    std::lock_guard<std::mutex> lk(mutex_);
    enabledPacks_ = packs;
    std::ofstream f(baseDir_ + "/packs.list");
    for (const std::string& p : packs) f << p << "\n";
    std::cout << "材质包清单已更新\n" << std::flush;
}
