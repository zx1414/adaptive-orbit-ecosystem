#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
using NativeSocket = uintptr_t;  // SOCKET
#else
using NativeSocket = int;
#endif

// 极简 HTTP/1.1 服务器（Windows 用 WinSock，仅绑定 127.0.0.1）。
// 能力：GET 静态文件（web 目录）、GET /state 二进制快照、POST /control 控制指令。
// 简化：Connection: close（无 keep-alive）、同步处理（本地单客户端足够）。
class HttpServer {
public:
    using StateProvider = std::function<std::vector<uint8_t>()>;
    using ControlHandler = std::function<void(const std::string& cmd)>;
    // 通用路由：返回 true 表示已处理（resp 为响应体，status 默认 200）。
    using RouteFn = std::function<bool(const std::string& method, const std::string& path,
                                       const std::string& body, std::string& resp,
                                       std::string& contentType, int& status)>;

    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void setWebRoot(const std::string& dir) { webRoot_ = dir; }
    void setStateProvider(StateProvider p) { stateProvider_ = std::move(p); }
    void setControlHandler(ControlHandler h) { controlHandler_ = std::move(h); }
    void setRouter(RouteFn f) { router_ = std::move(f); }

    // 绑定 127.0.0.1:port；端口占用时自动 +1 重试（最多 20 次）。
    // 返回实际端口；失败返回 -1。
    int start(int port);

    // 停止接受并关闭监听；由析构兜底。
    void stop();

private:
    void acceptLoop();
    void handleClient(NativeSocket s);
    void sendResponse(NativeSocket s, const std::string& status,
                      const std::string& contentType,
                      const void* body, size_t len);
    void sendText(NativeSocket s, const std::string& status,
                  const std::string& contentType, const std::string& text);
    void sendFile(NativeSocket s, const std::string& urlPath);

    StateProvider stateProvider_;
    ControlHandler controlHandler_;
    RouteFn router_;
    std::string webRoot_ = "web";
    NativeSocket listenSocket_ = ~(NativeSocket)0;
    std::thread thread_;
    bool running_ = false;
    int port_ = 0;
    bool wsaReady_ = false;
};
