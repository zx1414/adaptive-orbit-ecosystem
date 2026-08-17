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
