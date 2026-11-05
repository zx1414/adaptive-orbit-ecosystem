// agentsim - app/main.cpp : deterministic CLI entry. No timing output.
// R3-B：新增 --serve [port] 实时调控模式（EnvControl → RuntimeEnv → SimConfig →
// 下一帧生效；GET /state 二进制快照）。批跑路径（无 --serve）与旧版逐字节一致
// ——确定性门禁（check.ps1 双跑）以本文件批跑输出为契约，serve 模式是旁路。
#include "app/Cli.h"
#include "app/EnvControl.h"
#include "app/HttpServer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // R8-A：防 windows.h 拖入 winsock.h 与下方 winsock2.h 冲突
#endif
#include <windows.h>  // R8-A：ShellExecuteA 自动拉起浏览器（无参双击模式）
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")  // R8-A：ShellExecuteA 链接
#include "app/ParamSchema.h"
#include "app/ResourcePack.h"
#include "app/RuntimeEnv.h"
#include "app/SaveStore.h"
#include "app/Timeline.h"
#include "app/TraceWriter.h"
#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>  // R10：inet_pton（单实例探活）
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif

namespace {

// serve 段的短名（本文件匿名命名空间内；批跑函数体不用这些短名）。
using agentsim::Ball;
using agentsim::CliOptions;
using agentsim::CyanoNode;
using agentsim::Nucleus;
using agentsim::SimConfig;
using agentsim::Simulation;
using agentsim::SunConfig;
using agentsim::Vec2;
using agentsim::app::EnvChange;
using agentsim::app::EnvControl;
using agentsim::app::EnvEventEffect;
using agentsim::app::EnvPendingEvent;
using agentsim::app::EnvState;
using agentsim::app::HttpServer;
using agentsim::app::ParamSchema;
using agentsim::app::ResourcePackStore;
using agentsim::app::RuntimeEnv;
using agentsim::app::SaveStore;

void printHelp() {
    std::printf("agentsim - adaptive orbit nucleus ecology simulator\n");
    std::printf("usage: agentsim [options]\n");
    std::printf("  --width W  --height H      world size (torus)\n");
    std::printf("  --ticks N  --seed S        tick count / deterministic seed\n");
    std::printf("  --nuclei N                 initial nucleus count\n");
    std::printf("  --cyano-nodes N            initial cyanobacteria node count (default 512)\n");
    std::printf("  --cyano-max-nodes N        cyanobacteria node hard cap (default 1024)\n");
    std::printf("  --sun-base F  --sun-amplitude F   sunlight baseline / day-cycle amplitude\n");
    std::printf("  --policy rule|stub|perceptron|thermal  decision policy\n");
    std::printf("  --perceptron-seed S        perceptron policy init RNG seed\n");
    std::printf("  --perceptron-scale F       perceptron init weight/bias scale\n");
    std::printf("  --thermal-pref F           thermal policy preferred temperature\n");
    std::printf("  --thermal-w-thermal F      thermal policy temperature-fit weight\n");
    std::printf("  --thermal-w-caution F      thermal policy distortion-caution weight\n");
    std::printf("  --terrain-obstacles N      obstacle blobs to generate (default 0)\n");
    std::printf("  --terrain-waters N         water blobs to generate (default 0)\n");
    std::printf("  --terrain-thermal N        thermal-zone blobs to generate (default 0)\n");
    std::printf("  --agent-mode N             behavior mode (1 = autonomous)\n");
    std::printf("  --audit  --stats [path]  --stats-interval N\n");
    std::printf("  --trace <path>  --trace-interval N\n");
    std::printf("  --trace-genome             export per-nucleus genome (24 genes + hue) into frames\n");
    std::printf("  --serve [port]             HTTP real-time control mode (default port 8080)\n");
    std::printf("  --no-open                  with serve mode: do not auto-open the app window (Edge --app or default browser)\n");
    std::printf("                             (no args at all = serve mode + browser, R8-A)\n");
    std::printf("  --render | --no-render\n");
    std::printf("  --help | --version\n");
}

// Local torus wrap for trace export: folds coordinates into [0,w) x [0,h).
// Deliberately standalone: the app layer must not include environment headers.
agentsim::Vec2 wrapTorus(agentsim::Vec2 v, double w, double h) {
    agentsim::Vec2 r = v;
    if (w > 0.0) {
        r.x = std::fmod(v.x, w);
        if (r.x < 0.0) r.x += w;
        if (r.x >= w) r.x -= w;
    }
    if (h > 0.0) {
        r.y = std::fmod(v.y, h);
        if (r.y < 0.0) r.y += h;
        if (r.y >= h) r.y -= h;
    }
    return r;
}

// R6-C：从行式/表单体里取 key 的值（name=xxx / frame=N 等简易解析）。
std::string formValue(const std::string& body, const std::string& key) {
    const std::string pat = key + "=";
    const std::size_t pos = body.find(pat);
    if (pos == std::string::npos) return "";
    std::size_t e = body.find_first_of("&\r\n", pos);
    return body.substr(pos + pat.size(),
                       e == std::string::npos ? std::string::npos : e - pos - pat.size());
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool parseIntArg(const std::string& s, int& out) {
    if (s.empty() || s.size() > 11) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size()) return false;
    long long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    if (s[0] == '-') v = -v;
    if (v < -2147483648LL || v > 2147483647LL) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseDoubleArg(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == nullptr || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// ---- HTTP <-> 主循环 桥（ServeBridge）----
// HTTP 线程与主循环之间唯一共享状态（std::mutex 保护）。设计目标：
// HTTP 线程绝不直接读 Simulation —— 它只消费主循环每帧发布的只读快照；
// 主循环绝不直接写调控面 —— 它只消费锁内交换的事件队列与 cfgDirty 标志。
// TakeCfg 用"整对象拷贝出锁"避免锁内做 SimConfig 级深拷贝。
class ServeBridge {
public:
    void pushEvent(const EnvPendingEvent& ev) {
        std::lock_guard<std::mutex> lk(m_);
        events_.push_back(ev);
    }
    void takeEvents(std::vector<EnvPendingEvent>& out) {
        std::lock_guard<std::mutex> lk(m_);
        out.swap(events_);
        events_.clear();
    }
    void markCfgDirty() { std::lock_guard<std::mutex> lk(m_); cfgDirty_ = true; }
    bool takeCfgDirty() {
        std::lock_guard<std::mutex> lk(m_);
        const bool d = cfgDirty_;
        cfgDirty_ = false;
        return d;
    }
    // HTTP 线程：把当前 liveCfg 拷进 out（锁内仅拷贝指针级的 cfg 指针，不做深拷贝；
    // 深拷贝由调用方在锁外完成——copyCfg 是静态工具，无共享状态）。
    void copyCfg(const SimConfig& src, SimConfig& dst) const {
        std::lock_guard<std::mutex> lk(m_);
        dst = src;  // HTTP 线程是 liveCfg 的唯一写者；主循环读时 HTTP 线程可能
                    // 正在写 —— 这正是本锁存在的理由：liveCfg 的读写相遇点
                    // 全部穿过 m_。
    }
    void publish(std::uint64_t tick, std::size_t nuclei,
                 const std::vector<unsigned char>& state,
                 const std::string& envJson) {
        std::lock_guard<std::mutex> lk(m_);
        tick_ = tick;
        nuclei_ = nuclei;
        state_ = state;
        envJson_ = envJson;
    }
    std::uint64_t tick() const {
        std::lock_guard<std::mutex> lk(m_);
        return tick_;
    }
    std::vector<unsigned char> state() const {
        std::lock_guard<std::mutex> lk(m_);
        return state_;  // 拷贝出站：HTTP 响应期间主循环可继续发布新帧
    }
    std::string envJson() const {
        std::lock_guard<std::mutex> lk(m_);
        return envJson_;
    }
    // R6-C：/seek 回放请求（暂停态限定；主循环恢复检查点后补帧到目标）。
    void requestLoad(const agentsim::SimConfig& cfg, const std::string& name) {
        std::lock_guard<std::mutex> lk(m_);
        loadCfg_ = cfg;
        loadName_ = name;
        loadPending_ = true;
    }
    bool takeLoadRequest(agentsim::SimConfig& cfg, std::string& name) {
        std::lock_guard<std::mutex> lk(m_);
        if (!loadPending_) return false;
        loadPending_ = false;
        cfg = loadCfg_;
        name = loadName_;
        return true;
        return true;
    }
    void requestSeek(std::uint64_t frame) {
        std::lock_guard<std::mutex> lk(m_);
        seekTarget_ = frame;
        seekPending_ = true;
    }
    bool takeSeekRequest(std::uint64_t& frame) {
        std::lock_guard<std::mutex> lk(m_);
        if (!seekPending_) return false;
        seekPending_ = false;
        frame = seekTarget_;
        return true;
    }
    // R6-C：/run 换局请求（HTTP 线程交新 cfg，主循环下帧构造新 Simulation）。
    void requestRun(const agentsim::SimConfig& cfg) {
        std::lock_guard<std::mutex> lk(m_);
        runCfg_ = cfg;
        runPending_ = true;
    }
    bool takeRunRequest(agentsim::SimConfig& out) {
        std::lock_guard<std::mutex> lk(m_);
        if (!runPending_) return false;
        runPending_ = false;
        out = runCfg_;
        return true;
    }
    // R6-C：POST /save 请求通道。save 是只读快照，但 SaveStore::save 需要 sim 引用
    //（主循环独占）——HTTP 线程入队名字，主循环下帧执行，结果经 saveResult 回传。
    void requestSave(const std::string& name) {
        std::lock_guard<std::mutex> lk(m_);
        saveName_ = name;
        savePending_ = true;
    }
    bool takeSaveRequest(std::string& name) {
        std::lock_guard<std::mutex> lk(m_);
        if (!savePending_) return false;
        savePending_ = false;
        name = saveName_;
        return true;
    }
    void publishSaveResult(bool ok, const std::string& detail) {
        std::lock_guard<std::mutex> lk(m_);
        saveOk_ = ok;
        saveDetail_ = detail;
        saveDone_ = true;
    }
    // HTTP 线程等待保存完成（save 是磁盘 IO，主循环一帧内完成，等待有界）。
    bool waitSaveResult(int timeoutMs, bool& ok, std::string& detail) {
        for (int i = 0; i < timeoutMs / 10; ++i) {
            {
                std::lock_guard<std::mutex> lk(m_);
                if (saveDone_) { ok = saveOk_; detail = saveDetail_; saveDone_ = false; return true; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
    // POST /control：播放控制（暂停/继续/单步）。主循环每帧读一次。
    // 为什么需要：前端播放控制如果只在浏览器本地暂停轮询，后端仍在跑，
    // 用户"暂停"后再"继续"会看到时间跳了一大段。必须让后端真的停。
    void setPaused(bool v) {
        std::lock_guard<std::mutex> lk(m_);
        paused_ = v;
        if (v) stepOnce_ = false;  // 暂停时取消待单步
    }
    void requestStep() {
        std::lock_guard<std::mutex> lk(m_);
        paused_ = true;
        stepOnce_ = true;
    }
    bool paused() const {
        std::lock_guard<std::mutex> lk(m_);
        return paused_;
    }
    // 主循环调用：返回本帧是否应该推进。暂停态下只有 stepOnce_ 允许走一帧。
    bool shouldAdvance() {
        std::lock_guard<std::mutex> lk(m_);
        if (!paused_) return true;
        if (stepOnce_) { stepOnce_ = false; return true; }
        return false;
    }

private:
    bool paused_ = false;
    bool stepOnce_ = false;
    mutable std::mutex m_;
    std::vector<EnvPendingEvent> events_;
    bool cfgDirty_ = false;
    std::uint64_t tick_ = 0;
    std::size_t nuclei_ = 0;
    std::vector<unsigned char> state_;
    std::string envJson_;
    // R6-C：/load 载入请求（HTTP 线程已 loadHeader 拿 cfg，主循环构造 sim 并恢复实体）。
    // R6-C：load 请求。
    bool loadPending_ = false;
    agentsim::SimConfig loadCfg_{};
    std::string loadName_;
    // R6-C：seek 请求。
    bool seekPending_ = false;
    std::uint64_t seekTarget_ = 0;
    // R6-C：换局请求。
    bool runPending_ = false;
    agentsim::SimConfig runCfg_{};
    // R6-C：存档请求通道。
    bool savePending_ = false;
    bool saveDone_ = false;
    bool saveOk_ = false;
    std::string saveName_;
    std::string saveDetail_;
};

// ---- POST /env 行式协议解析（HTTP 线程执行）----
// 每行一条命令：axis.light=3 / fine.sun_base_level=1.35 / event.harvest=1 /
// preset.balance=1 / cyano=off。逐条走 EnvControl::apply（状态机，本线程独占
// envSt）+ RuntimeEnv::applyAll（key -> liveCfg 字段，本线程独占 liveCfg）。
// 事件只产出描述并入队（锁内），由主循环下一帧走 ledger 执行——能量绝不改于
// HTTP 线程。返回 200 JSON（ok/applied/rejected/unwired）。
std::string handleEnvPost(const std::string& body, SimConfig& cfg,
                          EnvState& envSt, ServeBridge& bridge) {
    int applied = 0;
    int rejected = 0;
    int paramChanges = 0;
    int queuedEvents = 0;
    std::string lastErr;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t eol = body.find('\n', pos);
        if (eol == std::string::npos) eol = body.size();
        std::string line = body.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { ++rejected; lastErr = "malformed line: " + line; continue; }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        std::vector<EnvChange> changes;
        EnvPendingEvent ev;
        std::string err;
        if (!EnvControl::apply(key, value, envSt, changes, err, &ev)) {
            ++rejected;
            lastErr = err;
            continue;
        }
        // 事件：只产出描述，入队由主循环下一帧走 ledger 执行（不在这里碰能量）。
        if (!ev.id.empty()) {
            bridge.pushEvent(ev);
            ++queuedEvents;
        }
        // 参数变更：key -> liveCfg 字段（RuntimeEnv 是映射唯一权威；本线程独占）。
        const int ok = RuntimeEnv::applyAll(cfg, changes);
        paramChanges += ok;
        if (!changes.empty()) bridge.markCfgDirty();
        rejected += static_cast<int>(changes.size()) - ok;
        applied += ok;
        if (changes.empty()) ++applied;  // 幂等命中（同档/同开关态）也算一次成功干预
    }
    std::string resp = "{\"ok\":";
    resp += (rejected == 0) ? "true" : "false";
    resp += ",\"applied\":" + std::to_string(applied);
    resp += ",\"paramChanges\":" + std::to_string(paramChanges);
    resp += ",\"queuedEvents\":" + std::to_string(queuedEvents);
    resp += ",\"rejected\":" + std::to_string(rejected);
    if (!lastErr.empty()) {
        resp += ",\"error\":\"" + lastErr + "\"";
    }
    const std::vector<std::string> unwired = RuntimeEnv::unwiredKeys();
    if (!unwired.empty()) {
        resp += ",\"unwired\":[";
        for (size_t i = 0; i < unwired.size(); ++i) {
            if (i > 0) resp += ',';
            resp += '"' + unwired[i] + '"';
        }
        resp += ']';
    }
    resp += '}';
    return resp;
}

// ---- 事件执行器（主循环线程；能量必须走 ledger）----
// 审计红线：每个事件执行后 sim.auditOk() 必须仍为 true，否则打日志并 exit 3
//（运行面不能带着破账继续跑——审计恒等式是本项目的存在性证明）。
// 写点声明：harvest/winter/plague 直接对核能量做 +=/-=，是"组合根事件通道"——
// 与 spawnNuclei 的出生引导（守卫 1c 已知豁免）同一性质的配置面写点：
// 玩家事件是配置面动作，行为层没有事件 API（AgentBehavior.h 本轮不可改），
// 能量记账全部走 ledger（注入/转移/耗散三通道），审计恒等式逐事件自证。
// R4 若把事件下沉为 AgentBehavior::applyEvent，这里的直接写随之退役。
void executePendingEvents(Simulation& sim,
                          const std::vector<EnvPendingEvent>& events) {
    using agentsim::Nucleus;
    for (const EnvPendingEvent& ev : events) {
        const EnvEventEffect& eff = ev.effect;
        if (ev.id == "harvest") {
            // 每核 +8，单次事件总注入 <= capPerEvent（2000）。能量来源 = 环境
            // 生产者池（kFoodPoolId == 0 == 蓝细菌池），一级注入与光合同通道；
            // 随后逐核 transfer（零和搬运），账本视角 = "环境产出一笔、核群分食"。
            const double cap = eff.capPerEvent > 0.0 ? eff.capPerEvent : 0.0;
            const double per = eff.perNucleus;
            double budget = cap > 0.0 ? cap : 1e300;
            double injected = 0.0;
            std::vector<Nucleus>& pop =
                const_cast<std::vector<Nucleus>&>(sim.nuclei());
            for (Nucleus& n : pop) {
                if (!n.alive) continue;
                if (per > budget) break;  // 封顶：剩余核不再注入
                n.energy += per;
                sim.ledger().transfer(0, n.id, per);
                injected += per;
                budget -= per;
            }
            if (injected > 0.0) sim.ledger().injectPrimary(0, injected);
            std::printf("[serve] event harvest: injected=%.3f (cap=%.1f)\n",
                        injected, cap);
        } else if (ev.id == "winter") {
            // 每核 -5（能量 < skipBelow 跳过）：扣减走 dissipate（能量离开系统）。
            std::vector<Nucleus>& pop =
                const_cast<std::vector<Nucleus>&>(sim.nuclei());
            double total = 0.0;
            for (Nucleus& n : pop) {
                if (!n.alive) continue;
                if (n.energy < eff.skipBelow) continue;  // <5 跳过（契约原文）
                const double take = eff.perNucleus < 0.0 ? -eff.perNucleus : eff.perNucleus;
                if (take > n.energy) continue;  // 付不起不扣（能量永不为负）
                n.energy -= take;
                sim.ledger().dissipate("event:winter", take);
                total += take;
            }
            std::printf("[serve] event winter: dissipated=%.3f\n", total);
        } else if (ev.id == "plague") {
            // 能量 > richThreshold 的富核 -12：抑制垄断巨头。
            std::vector<Nucleus>& pop =
                const_cast<std::vector<Nucleus>&>(sim.nuclei());
            double total = 0.0;
            for (Nucleus& n : pop) {
                if (!n.alive) continue;
                if (!(n.energy > eff.richThreshold)) continue;
                const double take = eff.perNucleus < 0.0 ? -eff.perNucleus : eff.perNucleus;
                if (take > n.energy) continue;
                n.energy -= take;
                sim.ledger().dissipate("event:plague", take);
                total += take;
            }
            std::printf("[serve] event plague: dissipated=%.3f\n", total);
        } else if (ev.id == "bloom") {
            // 追加蓝细菌节点：0 能量播种（账面零操作，能量从光合长出入账）。
            const int added =
                sim.addCyanoNodes(eff.cyanoNodesToAdd > 0 ? eff.cyanoNodesToAdd : 0);
            std::printf("[serve] event bloom: +%d cyano nodes (request %d, cap-guarded)\n",
                        added, eff.cyanoNodesToAdd);
        }
        // 审计红线：事件后必须仍守恒，否则 exit 3（任务卡原文）。
        if (!sim.auditOk()) {
            std::fprintf(stderr,
                         "error: audit failed after event \"%s\" - exiting (code 3)\n",
                         ev.id.c_str());
            std::exit(3);
        }
    }
}

// ---- GET /state 二进制快照（格式定义见交付报告；任务卡 §4 契约）----
// 小端，头 80 字节：magic 'A'(1) + version=1(1) + 状态(1) + 保留(1) + 帧号 i32(4)
//   + 世界宽 f32(4) + 世界高 f32(4) + 球数 i32(4) + 核数 i32(4) + 存活核数 i32(4)
//   + 自由球数 i32(4) + 蓝细菌节点数 i32(4) + 蓝细菌连线数 i32(4) + 蓝细菌平均能量 f32(4)
//   + 全局阳光 f32(4) + 保留 i32(4) + 保留 char[28] = 80
// 记录顺序：球(24B) -> 核(28B) -> 蓝细菌节点(20B) -> 蓝细菌连线(8B)。
// 记录数与字节流的一致性：本函数在主循环线程内、step() 之外调用，函数执行期间
// 世界状态不可变（detached_/alive 只在 step() 内变化），两遍遍历天然一致——
// 头部计数字段从实际写入的字节统计而来，恒与总长自洽。
std::vector<unsigned char> buildStateSnapshot(const Simulation& sim,
                                              const SimConfig& cfg,
                                              bool paused, double sunLevel, const char* reason) {
    std::vector<unsigned char> out;
    const auto pushF32 = [&out](float v) {
        unsigned int bits = 0;
        std::memcpy(&bits, &v, 4);
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<unsigned char>((bits >> (i * 8)) & 0xFF));
        }
    };
    const auto pushU32 = [&out](unsigned int v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<unsigned char>((v >> (i * 8)) & 0xFF));
        }
    };
    const auto pushI32 = [&out, &pushU32](int v) {
        pushU32(static_cast<unsigned int>(v));
    };
    const auto pushU8 = [&out](unsigned char v) { out.push_back(v); };

    // ---- 球记录（隶属球按核序；自由球与 detached 交接球 owner=0）----
    std::size_t ballCount = 0;
    std::size_t freeBallCount = 0;
    for (const Nucleus& n : sim.nuclei()) {
        if (!n.alive) continue;  // 死核：能量已入账（dissipate/carrion），快照不展示
        for (const Ball& b : n.balls) {
            const Vec2 p = wrapTorus(b.pos, cfg.world.width, cfg.world.height);
            pushU32(static_cast<unsigned int>(b.id & 0xFFFFFFFFu));
            pushU32(static_cast<unsigned int>(b.ownerId & 0xFFFFFFFFu));
            pushF32(static_cast<float>(p.x));
            pushF32(static_cast<float>(p.y));
            pushF32(static_cast<float>(b.energy));
            pushU8(static_cast<unsigned char>(static_cast<int>(b.type) & 0x3));
            pushF32(static_cast<float>(b.radius));
            ++ballCount;
        }
        // detached_ 交接球（能量在账）：owner=0 语义进快照（下一帧正式入 freeBalls_）。
        for (const Ball& b : n.detached_) {
            const Vec2 p = wrapTorus(b.pos, cfg.world.width, cfg.world.height);
            pushU32(static_cast<unsigned int>(b.id & 0xFFFFFFFFu));
            pushU32(0u);
            pushF32(static_cast<float>(p.x));
            pushF32(static_cast<float>(p.y));
            pushF32(static_cast<float>(b.energy));
            pushU8(static_cast<unsigned char>(static_cast<int>(b.type) & 0x3));
            pushF32(static_cast<float>(b.radius));
            ++ballCount;
            ++freeBallCount;
        }
    }
    for (const Ball& b : sim.freeBalls()) {
        const Vec2 p = wrapTorus(b.pos, cfg.world.width, cfg.world.height);
        pushU32(static_cast<unsigned int>(b.id & 0xFFFFFFFFu));
        pushU32(0u);
        pushF32(static_cast<float>(p.x));
        pushF32(static_cast<float>(p.y));
        pushF32(static_cast<float>(b.energy));
        pushU8(static_cast<unsigned char>(static_cast<int>(b.type) & 0x3));
        pushF32(static_cast<float>(b.radius));
        ++ballCount;
        ++freeBallCount;
    }

    // ---- 核记录（只含存活核；v2：32B 头（id x y energy cap speed hue alive+pad3）+ 24×f32 基因 = 128B）----
    // v2 基因区起始于记录 +32（alive+pad3 之后）；24 值顺序（Genome.h 声明顺序，前端 live-client.js 按此解析）：
    //   [0..2]   affinity[Resource,Scout,Shield]
    //   [3..5]   orbitRadius[Resource,Scout,Shield]
    //   [6..8]   absorbPreference[Resource,Scout,Shield]
    //   [9..11]  repelStrength[Resource,Scout,Shield]
    //   [12]     maxSpeed            [13] maxAccel       [14] energyThreshold
    //   [15]     mutationRate        [16] cyanoPreference [17] cyanoDigestion
    //   [18]     nucleusSightRadius  [19] nucleusPersonalSpace
    //   [20]     nucleusRepelStrength [21] nucleusCohesion
    //   [22]     forceRange          [23] forceCostK
    //   （hue 在 28B 头内，不重复；cyanoPreference 排在 12 标量段中段，见 Genome.h）
    std::size_t nucleusCount = 0;
    for (const Nucleus& n : sim.nuclei()) {
        if (!n.alive) continue;
        const Vec2 p = wrapTorus(n.pos, cfg.world.width, cfg.world.height);
        pushU32(static_cast<unsigned int>(n.id & 0xFFFFFFFFu));
        pushF32(static_cast<float>(p.x));
        pushF32(static_cast<float>(p.y));
        pushF32(static_cast<float>(n.energy));
        pushF32(static_cast<float>(n.cap));
        pushF32(static_cast<float>(n.speed));
        pushF32(static_cast<float>(n.genome.hue));  // 用户点名：核用色相变化
        pushU8(1);
        pushU8(0); pushU8(0); pushU8(0);  // pad 到 4 字节边界
        // R8-A /state v2：24 基因 f32（顺序契约见函数头注释；Genome.h 声明顺序）
        const agentsim::Genome& g = n.genome;
        for (int t = 0; t < 3; ++t) pushF32(static_cast<float>(g.affinity[t]));
        for (int t = 0; t < 3; ++t) pushF32(static_cast<float>(g.orbitRadius[t]));
        for (int t = 0; t < 3; ++t) pushF32(static_cast<float>(g.absorbPreference[t]));
        for (int t = 0; t < 3; ++t) pushF32(static_cast<float>(g.repelStrength[t]));
        pushF32(static_cast<float>(g.maxSpeed));
        pushF32(static_cast<float>(g.maxAccel));
        pushF32(static_cast<float>(g.energyThreshold));
        pushF32(static_cast<float>(g.mutationRate));
        pushF32(static_cast<float>(g.cyanoPreference));
        pushF32(static_cast<float>(g.cyanoDigestion));
        pushF32(static_cast<float>(g.nucleusSightRadius));
        pushF32(static_cast<float>(g.nucleusPersonalSpace));
        pushF32(static_cast<float>(g.nucleusRepelStrength));
        pushF32(static_cast<float>(g.nucleusCohesion));
        pushF32(static_cast<float>(g.forceRange));
        pushF32(static_cast<float>(g.forceCostK));
        ++nucleusCount;
    }

    // ---- 蓝细菌节点（20B：id x y energy hue）----
    std::size_t cyanoCount = 0;
    double cyanoEnergy = 0.0;
    for (const CyanoNode& c : sim.cyanoNodes()) {
        pushU32(static_cast<unsigned int>(c.id & 0xFFFFFFFFu));
        pushF32(static_cast<float>(c.pos.x));
        pushF32(static_cast<float>(c.pos.y));
        pushF32(static_cast<float>(c.energy));
        pushF32(static_cast<float>(c.hue));
        ++cyanoCount;
        cyanoEnergy += c.energy;
    }

    // ---- 蓝细菌连线（8B：idA idB，(小,大) 有序对）----
    std::size_t linkCount = 0;
    for (const auto& lk : sim.cyanoLinks()) {
        pushU32(static_cast<unsigned int>(lk.first & 0xFFFFFFFFu));
        pushU32(static_cast<unsigned int>(lk.second & 0xFFFFFFFFu));
        ++linkCount;
    }

    // ---- 头部：记录已写完，头部前插（记录区整体平移到 [80, end)）----
    std::vector<unsigned char> recs;
    recs.swap(out);
    out.clear();
    out.reserve(80 + recs.size());

    pushU8(0x41);                 // magic 'A'
    pushU8(2);                    // version=2（R8-A：核记录 128B = 32B 头 + 24 基因；v1=32B 无基因）
    // R11-P1-6：结束相位可信号——灭绝/到达帧上限时 phase=1（前端据此显示结束原因）
    const bool snapEnded = reason && (std::strcmp(reason, "extinct") == 0 ||
                                      std::strcmp(reason, "max_ticks") == 0);
    pushU8(paused ? 2 : (snapEnded ? 1 : 0));  // 状态：0=运行 1=已结束 2=暂停
    pushU8(0);                    // 保留
    pushI32(static_cast<int>(sim.tick()));
    pushF32(static_cast<float>(cfg.world.width));
    pushF32(static_cast<float>(cfg.world.height));
    pushI32(static_cast<int>(ballCount));
    pushI32(static_cast<int>(nucleusCount));
    pushI32(static_cast<int>(nucleusCount));  // 存活核数（快照只含存活核，同上）
    pushI32(static_cast<int>(freeBallCount));
    pushI32(static_cast<int>(cyanoCount));
    pushI32(static_cast<int>(linkCount));
    pushF32(cyanoCount > 0
                ? static_cast<float>(cyanoEnergy / static_cast<double>(cyanoCount))
                : 0.0f);
    pushF32(static_cast<float>(sunLevel));   // 全局阳光（serve 估算，前端渲染共用）
    pushI32(0);                              // 保留
    // R11-P1-6：结束原因写入保留区（≤27 字符 + NUL；v2.3.1 finishReason 对位）；头恰 80 字节
    {
        char reasonBuf[28] = {0};
        if (reason) {
            for (int rbi = 0; rbi < 27 && reason[rbi] != '\0'; ++rbi) reasonBuf[rbi] = reason[rbi];
        }
        for (int i = 0; i < 28; ++i) pushU8(static_cast<unsigned char>(reasonBuf[i]));
    }

    out.insert(out.end(), recs.begin(), recs.end());
    return out;
}

// 全局阳光的 serve 侧估算：与 Simulation.cpp 的 sunLevelAt 同式（白天正弦项 +
// 基线），前端渲染共用。计算式的权威实现仍在 sim 层；这里是展示辅助，
// 不参与任何账目（写错只影响 /state 头的一个 f32 字段）。
double serveSunEstimate(const SunConfig& sun, std::uint64_t tick) {
    double v = sun.baseLevel;
    if (sun.dayPeriodFrames > 0.0 && sun.dayAmplitude != 0.0) {
        v += sun.dayAmplitude *
             std::sin(6.283185307179586 * static_cast<double>(tick) /
                      sun.dayPeriodFrames);
    }
    if (!(v > 0.0)) v = 0.0;
    if (v > 1.0) v = 1.0;
    return v;
}

// ---- R7-A：材质包（resourcepacks/）加载与 JSON 序列化（serve 旁路面）----
// 生命周期归属：packs 全部状态 HTTP 线程独占（与 envSt/liveCfg 同一归属模型），
// 主循环绝不触碰。启动时读一次；POST /set-packs 原地重建。
struct PackState {
    std::vector<agentsim::app::ResourcePack> all;   // 全部可用包（enabled 优先排序）
    std::vector<std::string> enabled;               // 启用名单（有序，后者覆盖前者）
    std::string dir = "resourcepacks";              // 包根目录
    std::string listPath = "resourcepacks.list";    // 启用名单落盘文件
};

// 重扫包目录；诊断写 packsErr（非致命：目录缺失/包半成品只影响可用列表，
// active 始终有内置默认兜底）。
void reloadPacks(PackState& ps, std::string& packsErr) {
    std::string err;
    ps.all = agentsim::app::ResourcePackStore::loadAll(ps.dir, ps.enabled, err);
    packsErr = err;
}

// GET /packs 响应：{"ok":true,"packs":[...],"active":{...colors...},"err":"..."}
std::string packsJson(const PackState& ps, const std::string& packsErr) {
    const agentsim::app::ResourcePack active =
        agentsim::app::ResourcePackStore::mergeEnabled(ps.all, ps.enabled);
    std::string arr = "[";
    bool first = true;
    for (const agentsim::app::ResourcePack& p : ps.all) {
        if (!first) arr += ",";
        first = false;
        bool en = false;
        for (const std::string& n : ps.enabled) {
            if (n == p.name) { en = true; break; }
        }
        arr += "{\"name\":\"" + agentsim::app::ResourcePackStore::jsonEscape(p.name) +
               "\",\"desc\":\"" + agentsim::app::ResourcePackStore::jsonEscape(p.desc) +
               "\",\"version\":\"" + agentsim::app::ResourcePackStore::jsonEscape(p.version) +
               "\",\"author\":\"" + agentsim::app::ResourcePackStore::jsonEscape(p.author) +
               "\",\"enabled\":" + (en ? "true" : "false") + "}";
    }
    arr += "]";
    std::string colors = "{";
    bool fc = true;
    for (const auto& kv : active.colors) {
        if (!fc) colors += ",";
        fc = false;
        colors += "\"" + agentsim::app::ResourcePackStore::jsonEscape(kv.first) + "\":\"" +
                  agentsim::app::ResourcePackStore::jsonEscape(kv.second) + "\"";
    }
    colors += "}";
    return "{\"ok\":true,\"packs\":" + arr + ",\"active\":" + colors +
           ",\"err\":\"" + agentsim::app::ResourcePackStore::jsonEscape(packsErr) + "\"}";
}

// ---- --serve 模式主循环 ----
// 线程模型（数据竞争免疫的归属设计）：
//   HTTP 线程独占：envSt（调控面状态机）、liveCfg（RuntimeEnv 写目标）、
//                 unwired 诊断、POST /env 的全部解析与应用。
//   主循环独占：sim（唯一推进者）、事件执行、快照构建。
//   ServeBridge（mutex 保护）：事件队列（HTTP 写 / 主循环读）、
//                 cfgDirty 标志、每帧发布的 {tick, nuclei, cyanoNodes,
//                 cyanoLinks, /env JSON, /state 二进制} 只读快照。
// 路由层绝不直接读 sim —— 只读 bridge 里的发布快照，主循环写、HTTP 读，
// 锁内 memcpy 级开销。
int serveMode(const CliOptions& opts, int port, bool ticksGiven, bool openBrowser) {
    // R6-C：sim 改为 unique_ptr——/run /load 需要"原子替换世界"（关旧开新/换存档态），
    // 栈对象做不到。独占权在主循环；HTTP 线程只碰 bridge（既有模式不变）。
    auto simPtr = std::make_unique<agentsim::Simulation>(opts.config);
    agentsim::Simulation* simRaw = simPtr.get();   // R6-C：换局时随 ptr 一起换
    SimConfig liveCfg = opts.config;   // RuntimeEnv 的写目标（HTTP 线程独占）
    EnvState envSt;                    // 调控面状态（HTTP 线程独占）
    // R7-A：启动即加载材质包（resourcepacks.list 不存在 = 只用内置默认色）。
    PackState packState;
    std::string packsErr;
    {
        std::string listErr;
        packState.enabled =
            agentsim::app::ResourcePackStore::readListFile(packState.listPath, listErr);
        reloadPacks(packState, packsErr);
        if (!listErr.empty()) {
            if (!packsErr.empty()) packsErr += "; ";
            packsErr += listErr;
        }
    }
    if (!packsErr.empty()) {
        std::printf("[serve] resource packs: %zu available, %zu enabled (%s)\n",
                    packState.all.size(), packState.enabled.size(), packsErr.c_str());
    } else {
        std::printf("[serve] resource packs: %zu available, %zu enabled\n",
                    packState.all.size(), packState.enabled.size());
    }
    ServeBridge bridge;
    // R6-C：时间轴（回放数据源）。默认 200 帧一个检查点。
    auto timelinePtr = std::make_unique<agentsim::app::Timeline>(liveCfg);   // R6-C：回放检查点

    HttpServer server;
    // R10 修复：webRoot 相对可执行文件定位（双击 Explorer 时 CWD 可能不是仓库根——
    // 实测 agentsimw/agentsim 双击 → CWD=build\ → "frontend" 解析失败 → 根路径 404）。
    std::string webRoot = "frontend";
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        const std::size_t slash = exeDir.find_last_of("\\/");
        if (slash != std::string::npos) exeDir = exeDir.substr(0, slash + 1);
        // 优先 exe 同级（仓库根<exe 同级>rontend），其次 exe 目录下的 frontend，
        // 最后回落 CWD（旧行为，脚本从仓库根跑时等价）。
        if (std::ifstream(std::string(exeDir + "..\\frontend\\agentsim.html")).good())
            webRoot = exeDir + "..\\frontend";
        else if (std::ifstream(std::string(exeDir + "frontend\\agentsim.html")).good())
            webRoot = exeDir + "frontend";
    }
    server.setWebRoot(webRoot);
    server.setRouter([&](const std::string& method, const std::string& path,
                         const std::string& body, const std::string& query,
                         std::string& resp, std::string& contentType,
                         int& status) -> bool {
        (void)query;
        if (method == "GET" && path == "/status") {
            // 只读 bridge 发布的 tick（锁内拷出的原子快照）。
            resp = "{\"ok\":true,\"phase\":\"running\",\"tick\":" +
                   std::to_string(bridge.tick()) + "}";
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        // POST /control：播放控制。体为 cmd=pause|resume|step|stop。
        // 前端"暂停/继续/单步"必须真的停后端，否则用户暂停后继续会看到时间跳段。
        if (method == "POST" && path == "/control") {
            std::string cmd = "resume";
            // 简易行式解析：cmd=xxx
            const std::size_t pos = body.find("cmd=");
            if (pos != std::string::npos) {
                std::size_t e = body.find_first_of("&\r\n", pos);
                cmd = body.substr(pos + 4, e == std::string::npos ? std::string::npos : e - pos - 4);
            }
            if (cmd == "pause") { bridge.setPaused(true); }
            else if (cmd == "resume") { bridge.setPaused(false); }
            else if (cmd == "step") { bridge.requestStep(); }
            else { status = 400; resp = "{\"ok\":false,\"error\":\"unknown cmd\"}";
                   contentType = "application/json; charset=utf-8"; return true; }
            resp = std::string("{\"ok\":true,\"paused\":") +
                   (bridge.paused() ? "true" : "false") + "}";
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        if (method == "GET" && path == "/env") {
            resp = bridge.envJson();  // 主循环每帧发布的 JSON（含 cyano 计数）
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        if (method == "GET" && path == "/schema") {
            // ParamSchema 是不可变静态表（函数级 static，C++11 线程安全初始化），
            // HTTP 线程直读安全。
            resp = ParamSchema::toJson();
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        if (method == "POST" && path == "/env") {
            resp = handleEnvPost(body, liveCfg, envSt, bridge);
            contentType = "application/json; charset=utf-8";
            status = resp.find("\"ok\":true") != std::string::npos ? 200 : 200;
            return true;
        }
        if (method == "GET" && path == "/state") {
            const std::vector<unsigned char> snap = bridge.state();
            resp.assign(snap.begin(), snap.end());
            contentType = "application/octet-stream";
            status = 200;
            return true;
        }
        // ---- R6-C：存档与新局路由（骨架占位，逐步真实现）----
        if (method == "POST" && path == "/run") {
            // 开新局：按 body 参数构造新 SimConfig，经 bridge 入队，主循环原子换世界。
            agentsim::SimConfig next = liveCfg;
            const std::string seedS = formValue(body, "seed");
            const std::string nucleiS = formValue(body, "nuclei");
            const std::string cyanoS = formValue(body, "cyano_nodes");
            if (!seedS.empty()) next.world.seed = static_cast<std::uint64_t>(std::stoull(seedS));
            if (!nucleiS.empty()) next.colony.nuclei = static_cast<std::size_t>(std::stoul(nucleiS));
            if (!cyanoS.empty()) next.cyanoNodeCount = static_cast<std::size_t>(std::stoul(cyanoS));
            // R10 框架键：决策策略选择（rule/perceptron/thermal/hysteresis/foragebank/
            // bandit/riskaverse/stub——装配路由按名匹配，未知名回落 rule）。
            const std::string policyS = formValue(body, "policy");
            if (!policyS.empty()) {
                next.policy.name = policyS;
                next.policy.usePerceptron = (policyS == "perceptron");
                next.policy.useThermal = (policyS == "thermal");
            }
            // R10：帧率上限随开局设置（0 = 不限帧，模拟全速跑）
            const std::string fpsS = formValue(body, "max_fps");
            if (!fpsS.empty()) {
                try { next.runtime.maxFps = std::stod(fpsS); } catch (...) {}
            }
            bridge.requestRun(next);
            resp = "{\"ok\":true,\"phase\":\"running\",\"seed\":" +
                   std::to_string(next.world.seed) + "}" ;
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "GET" && path == "/saves") {
            const auto metas = agentsim::app::SaveStore::list();
            std::string arr = "[";
            for (std::size_t i = 0; i < metas.size(); ++i) {
                if (i) arr += ",";
                arr += "{\"name\":\"" + metas[i].name + "\",\"frame\":" +
                       std::to_string(metas[i].frame) + ",\"seed\":" +
                       std::to_string(metas[i].seed) + ",\"aliveNuclei\":" +
                       std::to_string(metas[i].aliveNuclei) + ",\"savedAt\":\""
                       + metas[i].savedAt + "\"}";
            }
            arr += "]";
            resp = "{\"ok\":true,\"saves\":" + arr + "}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "POST" && path == "/save") {
            const std::string name = formValue(body, "name");
            std::string verr;
            if (!agentsim::app::SaveStore::validName(name, verr)) {
                resp = std::string("{\"ok\":false,\"error\":\"") + verr + "\"}";
                contentType = "application/json; charset=utf-8"; status = 400; return true;
            }
            bridge.requestSave(name);
            bool ok = false; std::string detail;
            if (!bridge.waitSaveResult(3000, ok, detail)) {
                resp = "{\"ok\":false,\"error\":\"save timeout\"}";
                contentType = "application/json; charset=utf-8"; status = 504; return true;
            }
            if (!ok) {
                resp = std::string("{\"ok\":false,\"error\":\"") + detail + "\"}";
                contentType = "application/json; charset=utf-8"; status = 500; return true;
            }
            resp = "{\"ok\":true,\"name\":\"" + name + "\"}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "POST" && path == "/load") {
            const std::string name = formValue(body, "name");
            std::string verr, policyName;
            agentsim::SimConfig cfg2;
            if (!agentsim::app::SaveStore::loadHeader(name, cfg2, policyName, verr)) {
                resp = std::string("{\"ok\":false,\"error\":\"") + verr + "\"}";
                contentType = "application/json; charset=utf-8"; status = 400; return true;
            }
            bridge.requestLoad(cfg2, name);
            resp = "{\"ok\":true,\"name\":\"" + name + "\"}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "POST" && path == "/rename") {
            // R6-D 用"删旧+存新"近似，这里补真 rename（保留原 save.bin 不重写）
            const std::string oldName = formValue(body, "old");
            const std::string newName = formValue(body, "new");
            std::string err;
            if (!agentsim::app::SaveStore::rename(oldName, newName, err)) {
                resp = std::string("{\"ok\":false,\"error\":\"") + err + "\"}";
                contentType = "application/json; charset=utf-8"; status = 400; return true;
            }
            resp = "{\"ok\":true,\"name\":\"" + newName + "\"}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "POST" && path == "/delete-save") {
            std::string name = formValue(body, "name");
            std::string err;
            if (!agentsim::app::SaveStore::remove(name, err)) {
                resp = std::string("{\"ok\":false,\"error\":\"") + err + "\"}";
                contentType = "application/json; charset=utf-8"; status = 400; return true;
            }
            resp = "{\"ok\":true,\"deleted\":\"" + name + "\"}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "GET" && path == "/timeline") {
            std::string arr = "[";
            bool firstCp = true;
            for (const auto& cp : *timelinePtr) {
                if (!firstCp) arr += ",";
                arr += std::to_string(cp.frame);
                firstCp = false;
            }
            arr += "]";
            resp = "{\"ok\":true,\"checkpoints\":" + arr + ",\"current\":" +
                   std::to_string(bridge.tick()) + "}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }
        if (method == "POST" && path == "/seek") {
            // 连续 step 补到目标帧（检查点间隔小，重放代价可控）。
            if (!bridge.paused()) {
                resp = "{\"ok\":false,\"error\":\"seek requires paused state\"}";
                contentType = "application/json; charset=utf-8"; status = 409; return true;
            }
            const std::string fs_ = formValue(body, "frame");
            if (fs_.empty()) {
                resp = "{\"ok\":false,\"error\":\"missing frame\"}";
                contentType = "application/json; charset=utf-8"; status = 400; return true;
            }
            const std::uint64_t target = std::stoull(fs_);
            bridge.requestSeek(target);   // 入队，主循环执行（sim 独占）
            resp = "{\"ok\":true,\"frame\":" + fs_ + "}";
            contentType = "application/json; charset=utf-8"; status = 200; return true;
        }

        // ---- R7-A：材质包路由（HTTP 线程独占 packState/packsErr）----
        if (method == "GET" && path == "/packs") {
            resp = packsJson(packState, packsErr);
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        if (method == "POST" && path == "/set-packs") {
            // 行式体：enabled=classic_flat,another（空 = 全部停用，只留内置默认色）。
            const std::string csv = formValue(body, "enabled");
            packState.enabled = agentsim::app::ResourcePackStore::parseEnabledList(csv);
            std::string werr;
            const bool saved = agentsim::app::ResourcePackStore::writeListFile(
                packState.listPath, packState.enabled, werr);
            reloadPacks(packState, packsErr);
            if (!werr.empty()) {
                if (!packsErr.empty()) packsErr += "; ";
                packsErr += werr;
            }
            resp = packsJson(packState, packsErr);
            contentType = "application/json; charset=utf-8";
            status = saved ? 200 : 500;
            return true;
        }
        return false;  // 其余路径回落静态文件（frontend/）
    });

    const int actualPort = server.start(port);
    if (actualPort < 0) {
        std::fprintf(stderr, "error: cannot start HTTP server on port %d\n", port);
        return 1;
    }
    std::printf("[serve] listening on http://127.0.0.1:%d (web root: %s)\n",
                actualPort, webRoot.c_str());
    std::printf("[serve] 关闭本窗口即退出模拟\n");

    // R8-A：自动拉起默认浏览器（ShellExecuteA，零第三方依赖）。失败静默——
    // 前端可手动打开；这行不是账目路径，出错不影响仿真。
    if (openBrowser) {
        char url[64];
        std::snprintf(url, sizeof(url), "http://127.0.0.1:%d/", actualPort);
        // R11 发布形态：优先 Edge --app 独立窗口（无边框桌面应用观感、无浏览器
        // 标签页 chrome）；Edge 缺失/启动失败回落默认浏览器。展示路径，不影响账目。
        const char* edgeCands[] = {
            "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
            "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        };
        bool opened = false;
        for (const char* edge : edgeCands) {
            const DWORD attr = GetFileAttributesA(edge);
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            std::string params = std::string("--app=") + url + " --window-size=1280,800";
            const HINSTANCE h = ShellExecuteA(nullptr, "open", edge, params.c_str(),
                                              nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(h) > 32) { opened = true; break; }
        }
        if (!opened) {
            const HINSTANCE h = ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(h) <= 32) {
                std::printf("[serve] browser auto-open failed (%d); open %s manually\n",
                            static_cast<int>(reinterpret_cast<INT_PTR>(h)), url);
            }
        }
    }
    std::printf("[serve] wired keys: %d; rejected-on-purpose: width/height/seed/ticks/regrow_food\n",
                RuntimeEnv::wiredKeyCount());

    // 主循环：帧推进（HTTP 线程只通过 bridge 交换数据）。
    // 无 --ticks 时永久运行（实时服务语义，60fps 节流）；有 --ticks 时跑满退出
    //（与批跑同契约，便于脚本验收）。
    const bool runForever = !ticksGiven;
    const std::uint64_t maxTicks = opts.config.world.maxTicks;
    int renderInterval = liveCfg.runtime.renderInterval > 0
                             ? liveCfg.runtime.renderInterval : 1;
    auto lastFrame = std::chrono::steady_clock::now();
    while (runForever || simRaw->tick() < maxTicks) {
        // R6-C：/run 换局（HTTP 线程交新 cfg，本帧原子换世界）。
        {
            agentsim::SimConfig next;
            if (bridge.takeRunRequest(next)) {
                simPtr = std::make_unique<agentsim::Simulation>(next);
                simRaw = simPtr.get();
                timelinePtr = std::make_unique<agentsim::app::Timeline>(next);   // 新局重置时间轴
                std::printf("[serve] new run: seed=%llu nuclei=%zu cyano=%zu\n",
                            (unsigned long long)next.world.seed,
                            next.colony.nuclei, next.cyanoNodeCount);
            }
        }
        // R6-C：/load 载入（构造期实体用存档 cfg，实体态从 save.bin 恢复）。
        {
            agentsim::SimConfig lcfg;
            std::string lname;
            if (bridge.takeLoadRequest(lcfg, lname)) {
                simPtr = std::make_unique<agentsim::Simulation>(lcfg);
                simRaw = simPtr.get();
                std::string serr;
                const bool ok = agentsim::app::SaveStore::load(lname, *simRaw, lcfg, lname, serr);
                timelinePtr = std::make_unique<agentsim::app::Timeline>(lcfg);
                std::printf("[serve] load '%s': %s (tick=%llu)\n", lname.c_str(),
                            ok ? "ok" : serr.c_str(),
                            (unsigned long long)simRaw->tick());
            }
        }
        // R6-C：/seek 回放（暂停态；从最近检查点恢复 + 补帧到目标）。
        {
            std::uint64_t target = 0;
            if (bridge.takeSeekRequest(target)) {
                std::string serr;
                if (timelinePtr->seek(target, *simRaw, serr)) {
                    while (simRaw->tick() < target) simRaw->step();
                    std::printf("[serve] seek to %llu (from checkpoint)\n",
                                (unsigned long long)target);
                } else {
                    std::printf("[serve] seek failed: %s\n", serr.c_str());
                }
            }
        }
        // R6-C：存档请求（HTTP 线程入队，本帧执行——sim 独占在主循环）。
        {
            std::string saveName;
            if (bridge.takeSaveRequest(saveName)) {
                std::string serr;
                const bool ok = agentsim::app::SaveStore::save(saveName, *simRaw, liveCfg,
                                                     liveCfg.policy.name, serr);
                bridge.publishSaveResult(ok, ok ? "" : serr);
                std::printf("[serve] save '%s': %s\n", saveName.c_str(),
                            ok ? "ok" : serr.c_str());
            }
        }
        // 1) 取 HTTP 线程排队的参数变更（cfgDirty 才拷贝）。
        // 播放控制：暂停态下不推进（除非本帧是"单步"请求）。
        const bool advance = bridge.shouldAdvance();
        if (!advance) {
            // 仍要发布快照，让前端知道"暂停了"而不是"断了"。
            // R8 修复：发布**当前世界**的新快照（paused=true），不能发 bridge.state()
            // 旧缓存——/load、/seek、/run 换局后暂停态下 HTTP /state 会返回载入前的
            // 旧世界（实测 load 后 tick 显示旧值，resume 才跳到真世界）。
            const std::vector<unsigned char> snapPaused =
                buildStateSnapshot(*simRaw, liveCfg, true,
                                   serveSunEstimate(liveCfg.sun, simRaw->tick()), "paused");
            bridge.publish(simRaw->tick(), simRaw->nucleusCount(), snapPaused, bridge.envJson());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (bridge.takeCfgDirty()) {
            SimConfig next;
            bridge.copyCfg(liveCfg, next);
            simRaw->applyConfig(next);
            // render_interval 的运行时落点（HTTP 线程写、本循环读经拷贝）。
            if (next.runtime.renderInterval > 0) renderInterval = next.runtime.renderInterval;
        }
        // 2) 事件（上一帧 HTTP 排队）：能量走 ledger，审计失败即 exit 3。
        {
            std::vector<EnvPendingEvent> batch;
            bridge.takeEvents(batch);
            if (!batch.empty()) executePendingEvents(*simRaw, batch);
        }
        // 3) 帧推进 + 每帧审计（红线）。
        simRaw->step();
        if (!simRaw->auditOk()) {
            std::fprintf(stderr, "error: audit failed at tick %llu - exiting\n",
                         static_cast<unsigned long long>(simRaw->tick()));
            server.stop();
            return 3;
        }
        // 4) 发布快照（renderInterval 节流；bridge 锁内换入）。
        if (simRaw->tick() % static_cast<std::uint64_t>(renderInterval) == 0) {
            // R11-P1-6：结束原因（灭绝 / 到达帧上限；runForever 模式只会是灭绝）
            const char* runReason = "running";
            if (simRaw->nucleusCount() == 0) runReason = "extinct";
            else if (!runForever && simRaw->tick() >= maxTicks) runReason = "max_ticks";
            const std::vector<unsigned char> snap =
                buildStateSnapshot(*simRaw, liveCfg, false, serveSunEstimate(liveCfg.sun, simRaw->tick()), runReason);
            const std::string envJson = EnvControl::toJson(
                envSt, true, false, static_cast<int>(simRaw->cyanoNodes().size()),
                static_cast<int>(simRaw->cyanoLinks().size()));
            // R6-C：回放检查点（200 帧一个，供 /timeline 与 /seek）。
        if (simRaw->tick() % timelinePtr->checkpointInterval() == 0) {
            timelinePtr->maybeCheckpoint(*simRaw, simRaw->tick(), simRaw->stateHash());
        }
        bridge.publish(simRaw->tick(), simRaw->nucleusCount(), snap, envJson);
        }
        // 5) 限速：maxFps > 0 按其限速；永久模式默认 60fps（可 POST max_fps 覆盖）。
        double fps = liveCfg.runtime.maxFps;
        if (fps <= 0.0 && runForever) fps = 60.0;
        if (fps > 0.0) {
            const auto frameBudget =
                std::chrono::microseconds(static_cast<long long>(1000000.0 / fps));
            const auto now = std::chrono::steady_clock::now();
            if (now - lastFrame < frameBudget) {
                std::this_thread::sleep_for(frameBudget - (now - lastFrame));
            }
            lastFrame = std::chrono::steady_clock::now();
        }
    }

    server.stop();
    std::printf("[serve] done tick=%llu nuclei=%zu cyano=%zu total_energy=%.8f audit=%s\n",
                static_cast<unsigned long long>(simRaw->tick()),
                simRaw->nucleusCount(), simRaw->cyanoNodes().size(), simRaw->totalEnergy(),
                simRaw->auditOk() ? "ok" : "fail");
    return simRaw->auditOk() ? 0 : 3;
}

} // namespace

int main(int argc, char** argv) {
    // ---- --serve [port] 预提取（Cli.cpp 不许改；serve 模式不进批跑解析）----
    // rest/argvStore 必须活到 main 结束：argv 被重定向到其存储，
    // 块作用域变量会在块尾析构（悬垂指针 → parseCli 读到垃圾参数，
    // 实测表现为 check.ps1 的 live-param 探针全部 "unexpected positional
    // argument: <乱码>"）。
    bool serve = false;
    bool serveTicksGiven = false;
    int servePort = 8080;
    bool noOpen = false;  // R8-A：--no-open 抑制自动拉起浏览器
    std::vector<char*> argvStore;
    {
        std::vector<char*>& rest = argvStore;
        rest.push_back(argv[0]);
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--serve") {
                serve = true;
                // 可选端口参数：下一个 token 是纯数字且不是选项则消费。
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    int p = 0;
                    if (parseIntArg(argv[i + 1], p)) {
                        servePort = p;
                        serveTicksGiven = true;
                        ++i;
                    }
                }
                continue;
            }
            if (a == "--no-open") {  // R8-A
                noOpen = true;
                continue;
            }
            if (startsWith(a, "--serve=")) {
                serve = true;
                int p = 0;
                if (!parseIntArg(a.substr(8), p)) {
                    std::fprintf(stderr, "error: invalid --serve port: %s\n", a.c_str());
                    return 2;
                }
                servePort = p;
                continue;
            }
            rest.push_back(argv[i]);
        }
        argc = static_cast<int>(rest.size());
        argv = rest.data();
    }

    agentsim::CliOptions opts;
    std::string err;
    if (!agentsim::parseCli(argc, argv, opts, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    if (opts.help) { printHelp(); return 0; }
    if (opts.version) { std::printf("agentsim 3.0.0\n"); return 0; }

    // R8-A：无参双击 = 实时服务模式（此前走批跑 maxTicks=1000 即退，被用户判为"秒退"）。
    // 任何显式参数（含 --help/--version）保持既有批跑语义；--no-open 仅抑制浏览器拉起。
    // R10：单实例——已有实例在跑时，第二个双击不再起第二个服务（端口回退只会堆进程），
    // 而是：向已有实例的 HTTP 打一个 /state 探活，活着就拉起浏览器指向它并退出。
    if (argc == 1) {
        // 探活 8080..8090（R10：单 WSAStartup + 非阻塞 connect/300ms select 超时。
        // 同步 connect 在防火墙静默丢 SYN 的环境里每个端口要 20s+，11 个端口 = 数分钟假死。）
        {
            WSADATA wsa;
            bool wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
            for (int p = 8080; wsaOk && p <= 8090; ++p) {
                SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (sock == INVALID_SOCKET) break;
                // 非阻塞 connect + select 300ms
                u_long nb = 1;
                ioctlsocket(sock, FIONBIO, &nb);
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(static_cast<unsigned short>(p));
                inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
                bool alive = false;
                if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) {
                        fd_set wset, eset;
                        FD_ZERO(&wset); FD_ZERO(&eset);
                        FD_SET(sock, &wset); FD_SET(sock, &eset);
                        timeval tv{0, 300000};  // 300ms
                        const int sel = select(0, nullptr, &wset, &eset, &tv);
                        alive = (sel > 0 && FD_ISSET(sock, &wset));
                    }
                } else alive = true;
                if (alive) {
                    const char* req = "GET /state HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
                    send(sock, req, static_cast<int>(strlen(req)), 0);
                    char rsp[64] = {};
                    // 读侧恢复阻塞语义前先给接收一个短窗（仍非阻塞，select 等待）
                    fd_set rset;
                    FD_ZERO(&rset); FD_SET(sock, &rset);
                    timeval tvr{0, 300000};
                    if (select(0, &rset, nullptr, nullptr, &tvr) > 0) {
                        const int n = recv(sock, rsp, sizeof(rsp) - 1, 0);
                        alive = n > 12 && rsp[9] == '2' && rsp[10] == '0' && rsp[11] == '0';
                    } else alive = false;
                }
                closesocket(sock);
                if (alive) {
                    char front[64];
                    std::snprintf(front, sizeof(front), "http://127.0.0.1:%d/", p);
                    if (!noOpen) ShellExecuteA(nullptr, "open", front, nullptr, nullptr, SW_SHOWNORMAL);
                    std::printf("[serve] already running at %s%s — exiting\n", front,
                                noOpen ? "" : " (opened browser)");
                    WSACleanup();
                    return 0;
                }
            }
            if (wsaOk) WSACleanup();
        }
        serve = true;
        servePort = 8080;
        // R10 发布形态：无参双击 = 单窗口（浏览器 UI）。
        // FreeConsole 尝试已回退（疑似导致 spawn 场景下 HTTP 线程/stdio 初始化
        // 失常——服务端口不监听、日志零输出）。单窗口体验改为文档指引：
        // 用户手动最小化控制台即可。诚实记录 LOG。
    }
    if (serve) return serveMode(opts, servePort, serveTicksGiven, !noOpen);

    std::ofstream statsFile;
    if (opts.stats) {
        const std::string path =
            opts.statsPath.empty() ? std::string("build/stats.csv") : opts.statsPath;
        statsFile.open(path.c_str(), std::ios::out | std::ios::trunc);
        if (!statsFile.is_open()) { // F4: open failure must not be silently ignored.
            std::fprintf(stderr, "error: cannot open stats file: %s\n", path.c_str());
            // 错误码契约（GOAL §6）：2 = 参数错误，4 = I/O 失败。
            // 文件打不开是 I/O 失败，不是参数错误。此前这里返回 2，而同类情形
            // 的 --trace 返回 4，属双重标准（红队 R7 报 P1）。统一为 4。
            return 4;
        }
        // C7: 表头含 state_hash 列，双跑哈希比较可执行（每行一列）。
        statsFile << "tick,nuclei,cyano,total_energy,audit_ok,deaths,state_hash\n";
    }

    // 红线（GOAL §6）：任何 CLI 参数改值都必须能观察到输出变化，否则是死参数。
    // --trace-interval 只有在 --trace 存在时才有意义；单独给定时此前被静默忽略。
    // 现在显式警告（不报错：脚本里可能先拼参再决定要不要 trace），
    // 保证"被忽略"这件事对调用者是可见的。
    if (!opts.trace && opts.traceInterval != 1) {
        std::fprintf(stderr,
                     "warning: --trace-interval has no effect without --trace\n");
    }
    app::TraceWriter traceW;
    if (opts.trace) {
        if (!traceW.open(opts.tracePath)) { // I/O error contract: exit code 4.
            std::fprintf(stderr, "error: cannot open trace file: %s\n",
                         opts.tracePath.c_str());
            return 4;
        }
        traceW.setMeta(static_cast<long long>(opts.config.world.seed),
                       static_cast<long long>(opts.config.world.maxTicks),
                       opts.config.world.width, opts.config.world.height,
                       opts.config.policy.name);
        traceW.setGenomeEnabled(opts.traceGenome);
        traceW.beginBounds(0.0, 0.0, opts.config.world.width,
                           opts.config.world.height);
    }

    agentsim::Simulation sim(opts.config);
    const std::uint64_t endTick = opts.config.world.maxTicks;
    bool auditFailed = false;
    while (sim.tick() < endTick) {
        sim.step();
        // GOAL §6 红线：每 tick 审计，失败即终止（退出码 3）。
        // 此前主循环从不检查 auditOk()，坏账本会一路跑满 20k tick 才在末尾退出，
        // 期间所有统计与轨迹都建立在已破的守恒式上（红队 R7 报 P1）。改为立即跳出。
        // 顺序说明：先写 stats/trace 再判断是否中止，这样失败的那一帧
        // 能被记录下来（否则事后无法定位破账的 tick）。
        // 因此这里只置标志，真正的 break 放到帧尾。
        if (!sim.auditOk()) { auditFailed = true; }
        if (opts.stats && statsFile.is_open() &&
            (sim.tick() % static_cast<std::uint64_t>(opts.statsInterval)) == 0) {
            statsFile << sim.tick() << ',' << sim.nucleusCount() << ','
                      << sim.cyano().aliveCount() << ','
                      << sim.totalEnergy() << ','
                      << (sim.auditOk() ? 1 : 0) << ','
                      << sim.deathCount() << ','
                      << sim.stateHash() << '\n';
        }
        if (traceW.isOpen()) {
            // series: one row per tick, never downsampled (contract v2).
            // T29：追加 cyanoAlive/cyanoEnergy（存活蓝细菌数/能量总和，
            // CyanoField::aliveCount()/totalEnergy() 已只算 alive，口径与账本一致）。
            traceW.addSeries(static_cast<long long>(sim.tick()),
                             static_cast<long long>(sim.nucleusCount()),
                             static_cast<long long>(sim.cyano().aliveCount()),
                             sim.cyano().totalEnergy(),
                             sim.totalEnergy(),
                             sim.ledger().totalInjected(),
                             sim.ledger().totalDissipated(),
                             sim.auditOk());
            // frames: spatial snapshot, downsampled by --trace-interval.
            if ((sim.tick() % static_cast<std::uint64_t>(opts.traceInterval)) == 0) {
                const double w = opts.config.world.width;
                const double h = opts.config.world.height;
                traceW.beginFrame(static_cast<long long>(sim.tick()));
                for (const agentsim::Nucleus& n : sim.nuclei()) {
                    const agentsim::Vec2 np = wrapTorus(n.pos, w, h);
                    traceW.addNucleus(static_cast<long long>(n.id), np.x, np.y,
                                      n.energy, n.cap, n.speed, n.alive);
                    for (const agentsim::Ball& b : n.balls) {
                        // S19：Ball 现在自带世界坐标 pos（第一性状态），不再由
                        // 极坐标反推。仍做一次 wrap 以保证落在 [0,w) 内。
                        const agentsim::Vec2 bp = wrapTorus(b.pos, w, h);
                        traceW.addBall(static_cast<long long>(b.id),
                                       static_cast<long long>(n.id), bp.x, bp.y,
                                       b.energy, b.radius,
                                       static_cast<int>(b.type)); // S18: 真实球类型
                    }
                }
                // S19/S20：自由球（ownerId == 0）单独导出，ownerId 写 0。
                // 不导出它们的话，"球脱离了"在轨迹里完全不可见。
                for (const agentsim::Ball& b : sim.freeBalls()) {
                    const agentsim::Vec2 bp = wrapTorus(b.pos, w, h);
                    traceW.addBall(static_cast<long long>(b.id), 0LL, bp.x, bp.y,
                                   b.energy, b.radius, static_cast<int>(b.type));
                }
                // R1-B（契约 v3）：食物导出删除，"f" 改为蓝细菌连线拓扑 [[idA,idB],...]
                //（cyanoLinks()：成交边有序对，端点为节点 id，按下标升序）。
                for (const auto& lk : sim.cyanoLinks()) {
                    traceW.addLink(static_cast<long long>(lk.first),
                                   static_cast<long long>(lk.second));
                }
                // R1-B（契约 v3）：蓝细菌 6 元组 [id,x,y,energy,alive,hue]。pos 由
                // CyanoField 内部 wrap（seed/分裂/产球全路径），无需再 wrap；容器只含
                // 存活节点（饿死/被捕食节点帧末即压缩）。顺序 = nodes_ 下标（双跑一致）。
                for (const agentsim::CyanoNode& cn : sim.cyanoNodes()) {
                    traceW.addCyano(static_cast<long long>(cn.id), cn.pos.x,
                                    cn.pos.y, cn.energy, cn.alive, cn.hue);
                }
                // v3+（--trace-genome）：每核 24 个基因 + hue，顺序 = Genome.h 声明序。
                // 与 addNucleus 同源同序（都遍历 sim.nuclei()），双跑一致。
                // 依赖 R2-A 给 Nucleus 挂上 Genome 成员；用它落地前这段保持关闭
                // （AGENTSIM_HAS_GENOME 由 build 定义，避免主代理抢跑 R2-A 的接线）。
// R2-A 已交付（Nucleus 挂 Genome），--trace-genome 可以真正导出。
// 宏在源文件内定义而不是靠 /D 传：build.ps1 的 /D 经 PowerShell->cmd 两次解析后
// 会把后续的 /I src 和 /std:c++17 吃掉（症状：C1083 找不到头文件 + C2429 说
// 需要 /std:c++17）。源码内 #define 最可靠，且不影响命令行。
#define AGENTSIM_HAS_GENOME 1

#if defined(AGENTSIM_HAS_GENOME)
                if (opts.traceGenome) {
                    traceW.beginGenome();
                    for (const agentsim::Nucleus& n : sim.nuclei()) {
                        // 顺序 = Genome.h 的字段声明序（唯一权威；Genome.h 顶部明写
                        // "字段声明顺序 = 随机化/变异的固定遍历顺序……不得重排"）。
                        // 曾把 nucleus*/force* 写在 maxSpeed 之前——那是错的，D 子代理
                        // 用 Genome.h 逐字段比对后纠正。导出端与分析端必须同序，
                        // 否则判据 3/4 会拿错列当基因算。
                        const double genes[24] = {
                            // 1-4) 四个按类型数组，各 3 项
                            n.genome.affinity[0], n.genome.affinity[1], n.genome.affinity[2],
                            n.genome.orbitRadius[0], n.genome.orbitRadius[1], n.genome.orbitRadius[2],
                            n.genome.absorbPreference[0], n.genome.absorbPreference[1], n.genome.absorbPreference[2],
                            n.genome.repelStrength[0], n.genome.repelStrength[1], n.genome.repelStrength[2],
                            // 5-10) 机动与繁殖
                            n.genome.maxSpeed, n.genome.maxAccel,
                            n.genome.energyThreshold, n.genome.mutationRate,
                            // 11-12) 捕食
                            n.genome.cyanoPreference, n.genome.cyanoDigestion,
                            // 13-16) 对核社会参数
                            n.genome.nucleusSightRadius, n.genome.nucleusPersonalSpace,
                            n.genome.nucleusRepelStrength, n.genome.nucleusCohesion,
                            // 17-18) 施力能力
                            n.genome.forceRange, n.genome.forceCostK};
                        traceW.addGenome(static_cast<long long>(n.id), genes, 24, n.genome.hue);
                    }
                    traceW.endGenome();
                }
#else
                if (opts.traceGenome) {
                    std::fprintf(stderr,
                        "warning: --trace-genome requires the genome wiring (R2-A); ignored\n");
                }
#endif
                traceW.endFrame();
            }
        }
        // 帧尾中止：本帧的 stats/trace 已落盘，失败帧可被事后定位。
        if (auditFailed) break;
    }
    if (statsFile.is_open()) statsFile.close();
    if (traceW.isOpen() && !traceW.finalize()) {
        std::fprintf(stderr, "error: failed to write trace file\n");
        return 4;
    }

    std::printf("done tick=%llu nuclei=%zu cyano=%zu total_energy=%.8f audit=%s policy=%s\n",
                static_cast<unsigned long long>(sim.tick()),
                sim.nucleusCount(), sim.cyano().aliveCount(), sim.totalEnergy(),
                sim.auditOk() ? "ok" : "fail", opts.config.policy.name.c_str());
    // 审计失败：无论是中途跳出（auditFailed）还是跑到末尾才发现，一律 exit 3。
    // 中途跳出时 trace/stats 已写完已观测的那部分，便于事后定位破账的 tick。
    if (auditFailed || !sim.auditOk()) return 3;
    // 【红队 R9-P1 修正 2026-10-31】种群灭绝必须可观测。
    // 旧问题：nuclei=0 时 audit 恒 ok、exit 恒 0，灭绝与繁荣在退出码上不可区分
    // —— pushFreeBall 的静默灭绝回归正是借道此缺口逃过所有门禁。
    // S14 的验收口径本来就是 nuclei>0；这里把"灭绝"变成机器可判定的失败（exit 5）。
    if (sim.nucleusCount() == 0) {
        std::fprintf(stderr, "error: population extinct at tick %llu\n",
                     static_cast<unsigned long long>(sim.tick()));
        return 5;
    }
    return 0;
}
