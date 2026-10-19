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

// POST /env 契约响应：{ok,applied,rejected,influence,cooldowns}（+可选 err）。
// applied 为成功条数；rejected 为 [{key,reason}]；cooldowns 为兼容字段，手动事件无冷却恒为 0。
std::string envResponseJson(const EnvApplyResult& res, const EnvState& st, bool ok,
                            const std::string& err) {
    std::string out = "{\"ok\":";
    out += ok ? "true" : "false";
    out += ",\"applied\":" + std::to_string(res.applied);
    out += ",\"rejected\":[";
    bool first = true;
    for (const auto& r : res.rejected) {
        if (!first) out += ",";
        first = false;
        out += "{\"key\":\"" + jsonEscape(r.first) + "\",\"reason\":\"" +
               jsonEscape(r.second) + "\"}";
    }
    out += "],\"influence\":";
    char num[64];
    std::snprintf(num, sizeof(num), "%.2f", st.influence);
    out += num;
    out += ",\"cooldowns\":{";
    first = true;
    for (const EnvEventDef& d : EnvControl::events()) {
        if (!first) out += ",";
        first = false;
        out += "\"" + std::string(d.id) + "\":" +
               std::to_string(st.cooldown[(int)d.kind]);
    }
    out += "}";
    if (!err.empty()) out += ",\"err\":\"" + jsonEscape(err) + "\"";
    out += "}";
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
    // 用 env_config.txt 的 max_fps 作为启动帧率上限（0 = 不限速）；后续由前端
    // maxfps: 指令覆盖。缺省 WorldConfig::maxFps = 0.0，即无配置文件时限速。
    maxFps_.store(pendingCfg_.maxFps);
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
            rejectEnvRequest("当前为菜单态，请先开始模拟");  // 兜底：处理终止竞态下的残留命令
            if (takeLoadRequest(loadName)) {
                std::string err;
                loadWorldFromSave(loadName, err);
                if (!err.empty()) {
                    std::cerr << "读档失败: " << err << "\n";
                } else {
                    paused_.store(true);  // 读档后停在暂停态供查看
                    state_.store((int)State::Running);
                    refreshEnvCache();
                }
            } else if (takeRunRequest(params)) {
                createWorld(params);
                state_.store((int)State::Running);
                refreshEnvCache();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (st == State::Finished) {
            rejectEnvRequest("模拟已结束，环境调控不可用");
            if (toMenuRequested_.exchange(false)) {
                destroyWorld();
                state_.store((int)State::Menu);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        // ---- Running ----
        // 环境信箱：先于 step 应用（暂停态也应用），保证 /env 的改写在下一帧生效；
        // 顺序固定为「保存请求（循环顶部）→ 环境命令 → step」。
        processEnvRequest();
        if (stopRequested_.exchange(false)) {
            world_->stopNow("用户停止");
            web_->render(*world_);
            doEnding();
            finishReason_ = world_->finishReason();
            state_.store((int)State::Finished);
            refreshEnvCache();
            continue;
        }
        if (world_->finished()) {
            web_->render(*world_);  // 最终快照（含结束原因）
            doEnding();
            finishReason_ = world_->finishReason();
            state_.store((int)State::Finished);
            refreshEnvCache();
            continue;
        }
        if (paused_.load()) {
            if (!consumeStep()) {
                refreshEnvCache();  // 暂停时每轮刷新，影响力/冷却/参数立即可见
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
        // GET /env 缓存每 10 帧重建一次（toJson 有字符串分配，避免每帧开销）。
        if (world_->frame() % 10 == 0) refreshEnvCache();
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
    WorldConfig cfg;
    {
        // pendingCfg_ 由服务器线程（参数页保存 / 菜单态 POST /env）写入，拷贝必须加锁。
        std::lock_guard<std::mutex> lk(mutex_);
        cfg = pendingCfg_;  // 参数页当前配置为基准
    }
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
    refreshEnvCache();  // 回菜单态：GET /env 用 pendingCfg_ 的默认状态重建缓存
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

// ---------- 环境 API：缓存 + 信箱（主线程侧）----------

void LauncherApp::refreshEnvCache() {
    // World 只能由主线程访问；本函数只允许主线程调用。
    EnvState st;
    WorldConfig cfg;
    int nodes = 0, links = 0;
    bool cyanoEnabled = false;
    bool climateMod = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const std::string& id : enabledMods_) {
            if (id == "climate_event") {
                climateMod = true;
                break;
            }
        }
    }
    if (world_) {
        st = world_->envState();
        cfg = world_->config();
        nodes = world_->cyano().aliveNodeCount();
        links = world_->cyano().aliveLinkCount();
        cyanoEnabled = world_->cyanoEnabled();
        // 世界运行中：climate_event 启用时由该 mod 每帧自报 nextClimateIn（它持有周期常量）；
        // 未启用时这里复位，避免上一局状态残留。
        if (!climateMod) setEnvModStatus(EnvModStatus{false, 0});
    } else {
        // 菜单态/无世界：用参数页当前配置 + 默认 EnvState（影响力 50、无冷却）。
        std::lock_guard<std::mutex> lk(mutex_);
        cfg = pendingCfg_;
        nodes = cfg.initialCyanoNodes;
        cyanoEnabled = cfg.initialCyanoNodes > 0;
        setEnvModStatus(EnvModStatus{climateMod, 0});
    }
    const bool running = world_ != nullptr && (State)state_.load() == State::Running;
    const bool paused = paused_.load();
    std::string json =
        EnvControl::toJson(st, cfg, nodes, links, cyanoEnabled, running, paused);
    std::lock_guard<std::mutex> lk(envCacheMutex_);
    envJson_ = std::move(json);
    envCacheValid_ = true;
}

void LauncherApp::processEnvRequest() {
    std::map<std::string, std::string> cmds;
    {
        std::lock_guard<std::mutex> lk(envMutex_);
        if (!envPending_) return;
        cmds = envRequest_;
        envPending_ = false;
    }
    EnvApplyResult res;
    EnvState st;
    if (!world_) {
        res.rejected.emplace_back("", "没有运行中的模拟");
    } else if (world_->finished()) {
        res.rejected.emplace_back("", "模拟已结束");
    } else {
        // 主线程应用：可与存档/step 共用 World 而无需额外加锁（rng 单线程确定性）。
        res = world_->applyEnvCommand(cmds);
        st = world_->envState();
        refreshEnvCache();  // 让随后的 GET /env 立即反映本次改动
    }
    {
        std::lock_guard<std::mutex> lk(envMutex_);
        envResult_ = std::move(res);
        envResultState_ = st;
        envDone_ = true;
    }
    envCond_.notify_all();
}

void LauncherApp::rejectEnvRequest(const std::string& why) {
    bool had = false;
    {
        std::lock_guard<std::mutex> lk(envMutex_);
        if (!envPending_) return;
        envPending_ = false;
        envResult_ = EnvApplyResult{};
        envResult_.rejected.emplace_back("", why);
        envResultState_ = EnvState{};
        envDone_ = true;
        had = true;
    }
    if (had) envCond_.notify_all();
}

// ---------- 环境 API：服务器线程侧 ----------

// 菜单态：直接写入 pendingCfg_（axis/fine/preset/cyano），不扣影响力、无冷却。
// 事件必须等模拟开始后走世界信箱，因此这里明确拒绝。
std::string LauncherApp::handleEnvMenu(const std::map<std::string, std::string>& cmds) {
    EnvApplyResult res;
    EnvState st;  // 菜单态用默认 EnvState（影响力 50，冷却全 0）
    if (cmds.empty()) {
        res.rejected.emplace_back("", "空请求");
        return envResponseJson(res, st, false, "");
    }
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& kv : cmds) {
        const std::string& key = kv.first;
        const std::string& val = kv.second;
        std::string err;
        bool ok = false;
        if (key.rfind("axis.", 0) == 0) {
            const std::string id = key.substr(5);
            const EnvAxisDef* a = EnvControl::axis(id);
            if (!a) {
                err = "未知环境轴: " + id;
            } else {
                int level = -1;
                bool parsed = false;
                try {
                    size_t pos = 0;
                    int v = std::stoi(val, &pos);
                    if (pos == val.size()) {
                        level = v;
                        parsed = true;
                    }
                } catch (...) {
                }
                if (!parsed) err = "档位必须是整数 0..4";
                else ok = EnvControl::applyAxisLevel(pendingCfg_, a->axis, level, err);
            }
        } else if (key.rfind("fine.", 0) == 0) {
            ok = EnvControl::applyFine(pendingCfg_, key.substr(5), val, err);
        } else if (key.rfind("preset.", 0) == 0) {
            const EnvPresetDef* p = EnvControl::preset(key.substr(7));
            if (!p) {
                err = "未知预设: " + key.substr(7);
            } else if (EnvControl::applyPresetConfig(pendingCfg_, *p, err)) {
                ok = true;
                // 预设的蓝细菌目标直接落到参数页（下次启动生效）。
                if (p->cyanoOn && p->cyanoTarget > 0) {
                    pendingCfg_.initialCyanoNodes = p->cyanoTarget;
                } else if (!p->cyanoOn) {
                    pendingCfg_.initialCyanoNodes = 0;
                }
            }
        } else if (key == "cyano") {
            if (val == "on" || val == "1" || val == "true") {
                if (pendingCfg_.initialCyanoNodes <= 0) pendingCfg_.initialCyanoNodes = 150;
                ok = true;
            } else if (val == "off" || val == "0" || val == "false") {
                pendingCfg_.initialCyanoNodes = 0;
                ok = true;
            } else {
                err = "cyano 取值必须为 on/off";
            }
        } else if (key.rfind("event.", 0) == 0) {
            err = "菜单态无法触发事件（开始模拟后可用）";
        } else {
            err = "未知环境命令: " + key;
        }
        if (ok) {
            ++res.applied;
        } else {
            if (err.empty()) err = "应用失败";
            res.rejected.emplace_back(key, err);
        }
    }
    return envResponseJson(res, st, res.rejected.empty() && res.applied > 0, "");
}

// 运行/暂停态：投递环境信箱，主线程在 step 前应用；条件变量阻塞等待（超时 2s）。
std::string LauncherApp::handleEnvLive(const std::map<std::string, std::string>& cmds) {
    {
        std::lock_guard<std::mutex> lk(envMutex_);
        envRequest_ = cmds;
        envPending_ = true;
        envDone_ = false;
        envResult_ = EnvApplyResult{};
        envResultState_ = EnvState{};
    }
    std::unique_lock<std::mutex> lk(envMutex_);
    const bool done = envCond_.wait_for(lk, std::chrono::milliseconds(2000),
                                        [this] { return envDone_; });
    if (!done) {
        // 超时：撤销未处理的投递（主循环卡死/已退出），避免命令稍后被意外应用。
        envPending_ = false;
        EnvApplyResult res;
        res.rejected.emplace_back("", "环境命令超时（主循环无响应）");
        return envResponseJson(res, EnvState{}, false, "timeout");
    }
    EnvApplyResult res = envResult_;
    EnvState st = envResultState_;
    const bool ok = res.rejected.empty() && res.applied > 0;
    return envResponseJson(res, st, ok, "");
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
    // 同步限速：参数页改 max_fps 后立即生效，无需重启或重新启动模拟。
    // （否则 pendingCfg_.maxFps 更新了、env_config.txt 也写了，但正在跑的
    //   模拟仍用旧的 maxFps_，出现"界面显示与实际运行不一致"。）
    maxFps_.store(pendingCfg_.maxFps);
    std::cout << "参数已保存为默认（env_config.txt）" << std::endl;
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
    // ---- v2.2.0 环境 API ----
    if (method == "GET" && path == "/env") {
        // 菜单态：按参数页当前配置 + 默认 EnvState 现场构造；
        // 运行/暂停/结束态：返回主线程维护的缓存（World 不跨线程访问）。
        if ((State)state_.load() == State::Menu) {
            WorldConfig cfg;
            bool climateMod = false;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                cfg = pendingCfg_;
                for (const std::string& id : enabledMods_) {
                    if (id == "climate_event") {
                        climateMod = true;
                        break;
                    }
                }
            }
            // 菜单态自动气候还没开始跑：只报"是否将在本局启用"，倒计时为 0。
            setEnvModStatus(EnvModStatus{climateMod, 0});
            EnvState def;
            const int nodes = cfg.initialCyanoNodes;
            resp = EnvControl::toJson(def, cfg, nodes, 0, nodes > 0, false, false);
        } else {
            std::string cached;
            bool valid = false;
            {
                std::lock_guard<std::mutex> lk(envCacheMutex_);
                cached = envJson_;
                valid = envCacheValid_;
            }
            if (valid) {
                resp = cached;
            } else {
                // 缓存尚未生成（启动/读档瞬间）：退回参数页默认视图，避免空响应。
                WorldConfig cfg;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    cfg = pendingCfg_;
                }
                EnvState def;
                const int nodes = cfg.initialCyanoNodes;
                resp = EnvControl::toJson(def, cfg, nodes, 0, nodes > 0, false, false);
            }
        }
        contentType = "application/json; charset=utf-8";
        return true;
    }
    if (method == "POST" && path == "/env") {
        const std::map<std::string, std::string> cmds = parseKeyValues(body);
        if ((State)state_.load() == State::Menu) {
            resp = handleEnvMenu(cmds);
        } else {
            resp = handleEnvLive(cmds);
        }
        contentType = "application/json; charset=utf-8";
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
        out += ",\"requires\":\"" + jsonEscape(m.requires) + "\"";
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
        // 读档后派生对象（sun_/蓝细菌参数/网格）按恢复的 config 重建。
        // readState 内部也会调用；此处显式再调一次，确保 mod 状态段读取前已一致（幂等）。
        world_->syncDerivedFromConfig();
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
        // 读档成功后把世界配置同步回参数页：参数页/四轴档位/细调滑杆与读档世界一致。
        pendingCfg_ = world_->config();
        maxFps_.store(pendingCfg_.maxFps);
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
