#pragma once
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "HttpServer.h"
#include "World.h"
#include "mod_api.h"

class WebRenderer;
class Sampler;

// 时间轴检查点：保存某一帧的完整世界状态（World::writeState 的原始字节）。
// 用于「读档后拖动进度条查看任意已记录帧」与「从当前帧变动世界线」。
struct TimelinePoint {
    int frame = 0;
    std::string bytes;
};

// 启动器应用：菜单 → 运行 → 结束 的状态机 + HTTP 路由。
// 模拟主循环跑在主线程（rng 单线程确定性），HTTP 在服务器工作线程；
// 跨线程共享数据一律经 mutex_ 保护或用原子量。
class LauncherApp {
public:
    LauncherApp(std::string webRoot, std::string baseDir, int port);
    ~LauncherApp();

    // 启动 HTTP 服务器；openBrowser 时自动打开浏览器。返回实际端口，失败 -1。
    int start(bool openBrowser);
    // 阻塞运行主循环（主线程调用），直到退出指令。
    void runMainLoop();
    void stop();

private:
    enum class State : int { Menu = 0, Running = 1, Finished = 2 };

    // ---- HTTP 路由（服务器线程）----
    bool route(const std::string& method, const std::string& path,
               const std::string& body, std::string& resp,
               std::string& contentType, int& status);
    std::string statusText() const;
    std::string buildSchemaJson();
    std::string buildModsJson();
    std::string buildSavesJson();
    std::string buildPacksJson();
    std::vector<uint8_t> snapshotBytes();

    // ---- 环境 API（v2.2.0，GET/POST /env）----
    // 服务器线程只读 envJson_ 缓存或投递环境信箱；World 只由主线程访问。
    std::string handleEnvMenu(const std::map<std::string, std::string>& cmds);
    std::string handleEnvLive(const std::map<std::string, std::string>& cmds);

    // ---- 请求处理（服务器线程，只投递请求/写配置，不动世界）----
    void onControl(const std::string& cmd);
    void onRun(const std::string& body);          // key=value 行 → runRequest
    void onSaveConfig(const std::string& body);   // 写 env_config.txt
    void onSetMods(const std::string& body);      // 写 mods.list
    void onSaveRequest(const std::string& body);  // 存档请求 → 主线程执行
    void onLoadRequest(const std::string& body);  // 读档请求 → 主线程执行
    void onDeleteSave(const std::string& body);   // 删档（纯文件操作，本线程即可）
    void onRenameSave(const std::string& body);   // 重命名（纯文件操作）
    void onSaveThumb(const std::string& body);    // 缩略图 base64 → thumb.png
    void onSetPacks(const std::string& body);     // 写 packs.list

    // ---- 主线程助手 ----
    bool takeRunRequest(std::map<std::string, std::string>& out);
    bool takeLoadRequest(std::string& name);
    void createWorld(const std::map<std::string, std::string>& params);
    void loadWorldFromSave(const std::string& name, std::string& err);
    void destroyWorld();
    void doEnding();
    bool doSave(const std::string& name, std::string& err);
    bool consumeStep();
    // 环境缓存/信箱（主线程侧）
    void refreshEnvCache();                       // 主线程：重建 envJson_（每 10 帧/暂停时）
    void processEnvRequest();                     // 主线程：应用信箱命令并回填结果
    void rejectEnvRequest(const std::string& why);  // 主线程：拒绝未处理命令（结束/菜单态）

    // ---- 时间轴 / 读档拖动 / 世界线分支（v2.3.0）----
    void processTimelineRequest();               // 主线程：处理 seek/branch 信箱
    void rejectTimelineRequest(const std::string& why);
    bool restoreCheckpoint(int frame, std::string& err);
    bool branchFromCurrent(const std::map<std::string, std::string>& params,
                           int runFrames, std::string& err);
    void captureTimelinePoint();                  // 主线程：必要时记录当前帧状态
    void resetTimeline(const std::string& source, int savedFrame);
    void refreshTimelineCache();                  // 主线程：发布 /timeline JSON
    bool loadTimelineFile(const std::string& dir, int savedFrame);
    bool writeTimelineFile(const std::string& dir) const;
    std::string uniqueSaveName(const std::string& requested) const;
    // 服务器线程：投递请求并等待主线程结果（阻塞，超时返回错误 JSON）。
    std::string handleSeekLive(int frame);
    std::string handleBranchLive(const std::map<std::string, std::string>& body);

    std::string webRoot_;
    std::string baseDir_;  // exe 所在目录（env_config.txt / mods.list 等落点）
    int port_ = 8765;

    HttpServer server_;

    std::mutex mutex_;
    WorldConfig pendingCfg_;                       // 参数页"当前"配置
    std::map<std::string, std::string> runRequest_;
    bool runPending_ = false;
    std::string loadName_;
    bool loadPending_ = false;
    std::vector<std::string> enabledMods_;         // mods.list 有序列表 = 优先级
    std::vector<std::string> enabledPacks_;        // packs.list 有序列表（材质包堆叠）

    // 存档请求：服务器线程投递，主线程执行后通过条件变量通知结果。
    std::mutex saveMutex_;
    std::condition_variable saveCond_;
    std::string saveRequest_;
    bool savePending_ = false;
    bool saveOk_ = false;
    std::string saveErr_;

    // —— 环境 API（v2.2.0）——
    // GET /env 缓存：主线程每 10 帧（暂停时每轮）用 EnvControl::toJson 重建，
    // 服务器线程只读；World 绝不跨线程访问。
    std::mutex envCacheMutex_;
    std::string envJson_;
    bool envCacheValid_ = false;
    // 环境信箱：服务器线程投递命令 → 主线程在 world_->step() 前应用 → 条件变量回填结果。
    // 与存档信箱分开，避免共用锁导致 /save 与 /env 相互阻塞。
    std::mutex envMutex_;
    std::condition_variable envCond_;
    std::map<std::string, std::string> envRequest_;
    bool envPending_ = false;
    bool envDone_ = false;
    EnvApplyResult envResult_;
    EnvState envResultState_;

    // —— 时间轴 / seek / branch（v2.3.0）——
    // timeline_ 只由主线程读写；/timeline 读取 timelineJson_ 缓存（timelineMutex_ 保护）。
    std::vector<TimelinePoint> timeline_;
    std::string timelineSource_;
    int timelineSavedFrame_ = 0;
    std::mutex timelineMutex_;
    std::string timelineJson_;
    bool timelineCacheValid_ = false;
    int timelinePublishedFrame_ = -1;   // JSON 最近发布的 current 帧（避免暂停时每 10ms 重建）
    // seek/branch 信箱：服务器线程投递 → 主线程执行 → 条件变量回填。
    enum class TimelineRequest { None, Seek, Branch };
    std::mutex timelineReqMutex_;
    std::condition_variable timelineReqCond_;
    TimelineRequest timelineReq_ = TimelineRequest::None;
    int timelineSeekFrame_ = 0;
    std::map<std::string, std::string> timelineBranchParams_;
    int timelineBranchFrames_ = 100000;
    bool timelineReqPending_ = false;
    bool timelineReqDone_ = false;
    bool timelineReqOk_ = false;
    std::string timelineReqErr_;
    int timelineReqResultFrame_ = 0;

    std::unique_ptr<World> world_;
    std::unique_ptr<WebRenderer> web_;
    std::unique_ptr<Sampler> sampler_;
    // ModAPI 必须比 World 活得久：mod 订阅回调捕获它的引用（见 mod_api.h 约定）。
    std::unique_ptr<ModAPI> modApi_;

    std::atomic<int> state_{0};                    // State
    std::atomic<bool> exitRequested_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> toMenuRequested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> singleStep_{0};
    std::atomic<double> maxFps_{0.0};              // 0 = 不限速
    std::string finishReason_;
};
