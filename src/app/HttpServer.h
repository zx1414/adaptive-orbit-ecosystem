// agentsim - app/HttpServer.h
// R1-C：零依赖 HTTP/1.1 实时服务（只绑 127.0.0.1，供浏览器/本地工具访问）。
//
// 设计要点（docs/V2-SPEC.md §H + 任务卡 R1-C）：
//   - 只绑 127.0.0.1，不对外；端口被占自动 +1 重试（最多 20 次）。
//   - HTTP/1.1 + Connection: close，无 keep-alive；单线程 accept + 每连接同步处理
//     （本地单客户端足够，换来零锁零竞态）。
//   - 路由优先于静态文件；router 返回 true 表示已处理。
//   - GET 静态文件带目录穿越防护（拒绝含 ".." 的路径）与 MIME 映射，
//     未知扩展名一律 application/octet-stream。
//   - 请求体上限 1 MB，超限在读 body 之前返回 413。
//   - Windows 用 WinSock2（ws2_32 由 HttpServer.cpp 的 #pragma comment 链接，
//     免改 tools/build.ps1）；非 Windows 用 POSIX socket。零第三方依赖。
//   - stop() 先 shutdown+closesocket 唤醒阻塞中的 accept，再 join 线程，
//     保证析构干净关闭（顺序反了 Windows 上会卡死）。
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace agentsim::app {

#ifdef _WIN32
using NativeSocket = std::uintptr_t;  // SOCKET（UINT_PTR）
#else
using NativeSocket = int;
#endif

class HttpServer {
public:
    // 通用路由：返回 true 表示已处理（resp/contentType/status 由路由填写）。
    // path 已去掉查询串并做百分号解码；query 为原始查询串（不含 '?'，未解码）。
    using RouteFn = std::function<bool(const std::string& method, const std::string& path,
                                       const std::string& body, const std::string& query,
                                       std::string& resp, std::string& contentType, int& status)>;

    // 请求体（Content-Length 声明值）上限：1 MiB。
    static constexpr std::size_t kMaxBodyBytes = 1024u * 1024u;

    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    void setWebRoot(const std::string& dir) { webRoot_ = dir; }
    void setRouter(RouteFn f) { router_ = std::move(f); }

    // 绑定 127.0.0.1:port；端口占用自动 +1 重试（最多 20 次）。
    // 返回实际端口；失败返回 -1。已在运行时重复调用返回当前端口。
    int start(int port);

    // 停止接受连接并收线（幂等；析构函数兜底调用）。
    void stop();

    // 实际监听端口；未启动返回 -1，停止后保留最后一次成功值便于诊断。
    int port() const { return port_; }

private:
    void acceptLoop();
    void handleClient(NativeSocket s);
    void sendAll(NativeSocket s, const char* data, std::size_t len);
    void sendResponse(NativeSocket s, int status, const std::string& contentType,
                      const void* body, std::size_t len);
    void sendText(NativeSocket s, int status, const std::string& contentType,
                  const std::string& text);
    void serveStatic(NativeSocket s, const std::string& urlPath);

    RouteFn router_;
    std::string webRoot_ = "web";
    NativeSocket listenSocket_ = static_cast<NativeSocket>(~static_cast<std::uintptr_t>(0));
    std::thread thread_;
    bool running_ = false;
    int port_ = -1;
    bool wsaReady_ = false;
};

} // namespace agentsim::app
