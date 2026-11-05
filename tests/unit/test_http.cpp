// agentsim - tests/unit/test_http.cpp
// R1-C：HttpServer 单测。与 test_cli.cpp 同理：tools/build.ps1 的单测目标
// 不链 src/app，直接 #include 翻译单元（日后若构建脚本把 app 源加进单测
// 链接集，必须删除这些 #include，否则符号重复）。
// 约定（与 test_cli.cpp 相同）：每个 app/*.cpp 只允许被一个测试翻译单元并源。
// EnvControl.cpp / ParamSchema.cpp 归 test_envcontrol.cpp；本文件只并 HttpServer.cpp。
#include "../src/app/HttpServer.cpp"

#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#define INVALID_SOCKET (-1)
#endif

#include "test_framework.h"

namespace {

using agentsim::app::HttpServer;

// ---- 测试自用的最小 WinSock 客户端 ----

bool clientSend(int port, const std::string& raw, std::string& resp) {
#ifdef _WIN32
    WSADATA wsa{};
    bool ownWsa = false;
    if (wsa.wVersion == 0) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        ownWsa = true;
    }
    struct WsaGuard {
        bool own;
        ~WsaGuard() { if (own) WSACleanup(); }
    } guard{ownWsa};
#endif
    const auto sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        return false;
    }
    int sent = 0;
    while (sent < static_cast<int>(raw.size())) {
        const int n = send(sock, raw.data() + sent,
                           static_cast<int>(raw.size() - sent), 0);
        if (n <= 0) break;
        sent += n;
    }
    char buf[4096];
    for (;;) {
        const int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, static_cast<size_t>(n));
    }
    closesocket(sock);
    return sent == static_cast<int>(raw.size());
}

// HTTP 回包头部必须以 CRLF 结束；这里取状态行。
std::string statusLineOf(const std::string& resp) {
    const size_t p = resp.find("\r\n");
    return p == std::string::npos ? resp : resp.substr(0, p);
}

// 项目根：基于 __FILE__ 推导（tests/unit/xxx.cpp 的上两级）。
// 注意：cl 按相对路径编译时 __FILE__ 是相对的（如 "tests\\unit\\test_http.cpp"），
// 推导结果不可信；用 README.md 实存探针校验，失败则回退到进程工作目录
// （build.ps1 / check.ps1 都先把 CWD 定到项目根，跑测试即根）。
std::string projectRoot() {
    std::string f = __FILE__;
    for (int i = 0; i < 3; ++i) {
        const size_t p = f.find_last_of("/\\");
        if (p == std::string::npos) break;
        f = f.substr(0, p);
    }
    {
        const std::string probe = (f.empty() ? std::string(".") : f) + "/README.md";
        std::ifstream pf(probe.c_str());
        if (pf.good()) return f.empty() ? "." : f;
    }
    return ".";
}

// 起一个带 /status 路由的服务器；返回实际端口（失败 -1）。
int startServerWithRouter(HttpServer& srv) {
    srv.setRouter([](const std::string& method, const std::string& path,
                     const std::string& body, const std::string& query,
                     std::string& resp, std::string& contentType, int& status) {
        (void)body;
        if (method == "GET" && path == "/status") {
            resp = "{\"ok\":true,\"echo\":\"" + path + "\",\"query\":\"" + query + "\"}";
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        if (method == "POST" && path == "/env") {
            resp = "posted:" + body;
            contentType = "text/plain; charset=utf-8";
            status = 200;
            return true;
        }
        if (path == "/boom") {
            resp = "boom";
            contentType = "text/plain; charset=utf-8";
            status = 500;
            return true;
        }
        return false;
    });
    return srv.start(18080);  // 被占会自动 +1
}

} // namespace

TEST(http_start_stop_and_port) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port >= 18080);
    CHECK(srv.port() == port);
    srv.stop();
    CHECK(srv.port() == port);  // stop 后保留诊断值
}

TEST(http_router_status_endpoint) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    std::string resp;
    CHECK(clientSend(port, "GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 200 OK");
    CHECK(resp.find("{\"ok\":true,\"echo\":\"/status\",\"query\":\"\"}") != std::string::npos);
    CHECK(resp.find("Connection: close") != std::string::npos);
    CHECK(resp.find("HTTP/1.1") != std::string::npos);
    srv.stop();
}

TEST(http_router_receives_query_and_post_body) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    std::string resp;
    CHECK(clientSend(port,
                     "GET /status?axis=light&value=3 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
                     resp));
    CHECK(resp.find("\"query\":\"axis=light&value=3\"") != std::string::npos);
    resp.clear();
    CHECK(clientSend(port,
                     "POST /env HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                     "Content-Type: text/plain\r\nContent-Length: 14\r\n\r\n"
                     "axis.light=3\n\n",
                     resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 200 OK");
    CHECK(resp.find("posted:axis.light=3") != std::string::npos);
    srv.stop();
}

TEST(http_unknown_path_404) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    std::string resp;
    CHECK(clientSend(port, "GET /definitely/not/here HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 404 Not Found");
    srv.stop();
}

TEST(http_router_error_status_passthrough) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    std::string resp;
    CHECK(clientSend(port, "GET /boom HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 500 Internal Server Error");
    srv.stop();
}

TEST(http_traversal_rejected) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    std::string resp;
    // 编码形态：%2E%2E 解码后是 ..，必须同样被拦。
    CHECK(clientSend(port, "GET /../secret.txt HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    const std::string line1 = statusLineOf(resp);
    CHECK(line1 == "HTTP/1.1 400 Bad Request" || line1 == "HTTP/1.1 403 Forbidden");
    resp.clear();
    CHECK(clientSend(port, "GET /%2E%2E/secret.txt HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    const std::string line2 = statusLineOf(resp);
    CHECK(line2 == "HTTP/1.1 400 Bad Request" || line2 == "HTTP/1.1 403 Forbidden");
    resp.clear();
    // 反斜杠变体同样拒绝。
    CHECK(clientSend(port, "GET /..%5Csecret.txt HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    const std::string line3 = statusLineOf(resp);
    CHECK(line3 == "HTTP/1.1 400 Bad Request" || line3 == "HTTP/1.1 403 Forbidden");
    srv.stop();
}

TEST(http_body_over_1mb_413) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    // 声明 1 MiB + 1：必须在读 body 之前 413（不等 1048577 字节到齐）。
    std::string req = "POST /env HTTP/1.1\r\nHost: x\r\nContent-Length: 1048577\r\n\r\n";
    std::string resp;
    CHECK(clientSend(port, req, resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 413 Payload Too Large");
    srv.stop();
}

TEST(http_static_file_served_with_mime) {
    HttpServer srv;
    srv.setWebRoot(projectRoot());
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    // 路由已注册的 /status 仍命中路由（路由优先）。
    std::string resp;
    CHECK(clientSend(port, "GET /status HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 200 OK");
    // 静态回退：本项目根下的 README.md 一定存在。
    resp.clear();
    CHECK(clientSend(port, "GET /README.md HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 200 OK");
    CHECK(resp.find("Content-Type: application/octet-stream") != std::string::npos);
    CHECK(resp.find("Content-Length: ") != std::string::npos);
    srv.stop();
}

TEST(http_port_conflict_auto_increments) {
    // 先占一个端口，再 start：服务器必须拿到 +1（或更多）的端口。
#ifdef _WIN32
    WSADATA wsa{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    const auto sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(sock != INVALID_SOCKET);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
    // 独占绑定：保证后续服务器 bind 同端口必然失败，从而触发自动 +1。
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&one), sizeof(one));
#endif
    CHECK(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(listen(sock, 1) == 0);

    HttpServer srv;
    srv.setWebRoot(projectRoot());
    const int port = srv.start(19000);  // 19000 被占 → 至少 19001
    CHECK(port >= 19001);
    CHECK(srv.port() == port);
    // 实际监听验证。
    std::string resp;
    CHECK(clientSend(port, "GET /definitely_missing HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 404 Not Found");
    srv.stop();
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

TEST(http_stop_is_idempotent_and_reusable) {
    HttpServer srv;
    const int port = startServerWithRouter(srv);
    CHECK(port > 0);
    srv.stop();
    srv.stop();  // 幂等
    // 再起一次：行为必须一致（析构兜底也覆盖）。
    const int port2 = startServerWithRouter(srv);
    CHECK(port2 > 0);
    std::string resp;
    CHECK(clientSend(port2, "GET /status HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 200 OK");
    srv.stop();
}

// HTTP → EnvControl 端到端（本轮两个模块的接缝冒烟）：
// 构造一个用 EnvControl 做 router 的服务器，验证"POST /env 改档位"全链路。
// 路由接缝冒烟：验证"行式 key=value 请求体 -> JSON 响应 + 状态码"这一
// HttpServer↔EnvControl 契约形态（EnvControl 本体在 test_envcontrol.cpp 全覆盖；
// 此处用同形协议的桩路由，避免跨 .cpp 并源导致符号重复）。
TEST(http_router_line_protocol_smoke) {
    HttpServer srv;
    srv.setRouter([](const std::string& method, const std::string& path,
                     const std::string& body, const std::string& query,
                     std::string& resp, std::string& contentType, int& status) {
        (void)query;
        static int applied = -1;  // 单线程同步处理，无并发访问
        if (method == "POST" && path == "/env") {
            std::string line = body;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                resp = "{\"ok\":false,\"err\":\"malformed\"}";
                contentType = "application/json; charset=utf-8";
                status = 400;
                return true;
            }
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            // 桩：记录一条合法的 axis.* 命令，产出两个变更。
            applied = (key.rfind("axis.", 0) == 0 && !value.empty()) ? 2 : 0;
            resp = applied > 0 ? "{\"ok\":true,\"applied\":2}"
                               : "{\"ok\":false,\"err\":\"unknown\"}";
            contentType = "application/json; charset=utf-8";
            status = applied > 0 ? 200 : 400;
            return true;
        }
        if (method == "GET" && path == "/env") {
            resp = "{\"applied\":" + std::to_string(applied) + "}";
            contentType = "application/json; charset=utf-8";
            status = 200;
            return true;
        }
        return false;
    });
    const int port = srv.start(18080);
    CHECK(port > 0);
    std::string resp;
    CHECK(clientSend(port, "POST /env HTTP/1.1\r\nHost: x\r\n"
                           "Content-Length: 14\r\n\r\naxis.light=3\n\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 200 OK");
    CHECK(resp.find("\"ok\":true") != std::string::npos);
    CHECK(resp.find("\"applied\":2") != std::string::npos);
    // 回读：路由侧状态在同进程内可见。
    resp.clear();
    CHECK(clientSend(port, "GET /env HTTP/1.1\r\nHost: x\r\n\r\n", resp));
    CHECK(resp.find("\"applied\":2") != std::string::npos);
    // 非法命令体：400 契约。
    resp.clear();
    CHECK(clientSend(port, "POST /env HTTP/1.1\r\nHost: x\r\n"
                           "Content-Length: 5\r\n\r\nbad!\n\n", resp));
    CHECK(statusLineOf(resp) == "HTTP/1.1 400 Bad Request");
    CHECK(resp.find("\"ok\":false") != std::string::npos);
    srv.stop();
}
