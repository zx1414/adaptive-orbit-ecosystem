// agentsim - app/HttpServer.cpp
// R1-C：零依赖 HTTP/1.1 服务器实现（Windows=WinSock2 / POSIX=BSD socket）。
// 语义细节：
//   - 请求行解析后 path 先剥查询串再百分号解码（支持中文路径）；
//     query 保留原始未解码片段交给路由层。
//   - Content-Length 在读 body 之前检查：声明值 > 1 MiB 立即 413，
//     不为异常客户端白耗带宽。
//   - 目录穿越防护：解码后的路径任何位置出现 ".." 一律拒绝（400/403）。
//   - 路由优先于静态文件；router 返回 false 且方法非 GET 时不回落静态。
//   - stop()：running_=false → shutdown+closesocket(监听) → join 线程。
//     顺序不能反：accept 阻塞在监听套接字上，必须先踢醒它再 join，
//     否则 Windows 上析构会永久卡住。
#include "HttpServer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// R1-C 约束：tools/build.ps1 只读，无法在脚本里追加 ws2_32.lib；
// MSVC 专属的 #pragma comment 让本翻译单元自带链接指令。
// 非 Windows 编译器不认识该 pragma（被 #ifdef 隔开），POSIX 下 libc 自带 socket。
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define closesocket close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace agentsim::app {
namespace {

constexpr std::size_t kMaxHeaderBytes = 64u * 1024u;  // 请求头上限（防异常客户端）

// 状态码 → 状态行文本。
const char* statusText(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 413: return "413 Payload Too Large";
        case 500: return "500 Internal Server Error";
        default: return "200 OK";
    }
}

// 扩展名 → MIME。未知扩展名一律 application/octet-stream。
std::string mimeFor(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".mjs") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".wasm") return "application/wasm";
    return "application/octet-stream";
}

// URL 路径 → webRoot 下的文件路径；空串表示非法（穿越/形态不对）。
// 注意：调用方传入的 urlPath 已经百分号解码，"..\" 与 "../" 都要拦。
std::string safeFilePath(const std::string& root, const std::string& urlPath) {
    if (urlPath.empty() || urlPath[0] != '/') return "";
    if (urlPath.find("..") != std::string::npos) return "";  // 防目录穿越
    std::string rel = urlPath.substr(1);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    // R10：根路径优先服务单文件发布版 agentsim.html（双击体验=完整单页游戏），
    // 不存在才回落 index.html（开发版多文件）。带查询串/子路径不变。
    if (rel.empty()) rel = "agentsim.html";
    if (rel.back() == '/') rel += "agentsim.html";
    return root + "/" + rel;
}

// 百分号解码（%XX → 字节；+ → 空格）。非法转义原样保留。
std::string percentDecode(const std::string& s) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hexVal(s[i + 1]);
            const int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// 小写化（头部键名比较用）。
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Content-Length 严格解析：整个字段必须是十进制非负整数；非法返回 false。
bool parseContentLength(const std::string& s, std::size_t& out) {
    if (s.empty() || s.size() > 19) return false;
    std::size_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<std::size_t>(c - '0');
    }
    out = value;
    return true;
}

} // namespace

HttpServer::HttpServer() {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) wsaReady_ = true;
#endif
}

HttpServer::~HttpServer() {
    stop();
#ifdef _WIN32
    if (wsaReady_) WSACleanup();
#endif
}

int HttpServer::start(int port) {
    if (running_) return port_;
    if (port < 0 || port > 65535) return -1;
#ifdef _WIN32
    if (!wsaReady_) return -1;
#endif
    // 端口占用自动 +1 重试，最多 20 次。
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int p = port + attempt;
        if (p > 65535) break;
        const NativeSocket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return -1;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 只绑 127.0.0.1，不对外
        addr.sin_port = htons(static_cast<std::uint16_t>(p));
        const int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                   sizeof(reuse));
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closesocket(s);
            continue;  // 端口被占，试下一个
        }
        if (listen(s, 8) != 0) {
            closesocket(s);
            continue;
        }
        listenSocket_ = s;
        port_ = p;
        running_ = true;
        thread_ = std::thread([this] { acceptLoop(); });
        return p;
    }
    return -1;
}

void HttpServer::stop() {
    running_ = false;
    if (listenSocket_ != static_cast<NativeSocket>(~static_cast<std::uintptr_t>(0))) {
        // 先 shutdown 再 closesocket：确保阻塞中的 accept 立刻被唤醒返回错误。
#ifdef _WIN32
        shutdown(listenSocket_, SD_BOTH);
#else
        shutdown(listenSocket_, SHUT_RDWR);
#endif
        closesocket(listenSocket_);
        listenSocket_ = static_cast<NativeSocket>(~static_cast<std::uintptr_t>(0));
    }
    if (thread_.joinable()) thread_.join();
}

void HttpServer::acceptLoop() {
    while (running_) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = static_cast<int>(sizeof(from));
#else
        socklen_t fromLen = sizeof(from);
#endif
        const NativeSocket c = accept(listenSocket_, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (c == INVALID_SOCKET) {
            if (running_) {
                // 监听套接字被 stop() 关闭时会走到这里，此时 running_ 已翻转。
                // 真正的偶发错误短暂退避后继续（本地单客户端，代价可忽略）。
#ifdef _WIN32
                Sleep(5);
#else
                usleep(5000);
#endif
            }
            continue;
        }
        handleClient(c);  // 同步处理：本地单客户端场景，无锁无竞态
        closesocket(c);
    }
}

void HttpServer::handleClient(NativeSocket s) {
    // ---- 读请求头（到空行为止），同时兜住前 64 KiB 内已到达的 body 前缀 ----
    std::string req;
    req.reserve(4096);
    char buf[4096];
    bool haveHeader = false;
    while (req.size() < kMaxHeaderBytes) {
        const int n = recv(s, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) return;  // 对端关闭 / 出错
        req.append(buf, static_cast<std::size_t>(n));
        if (req.find("\r\n\r\n") != std::string::npos) {
            haveHeader = true;
            break;
        }
    }
    if (!haveHeader) return;  // 头超限：直接丢弃连接

    const size_t headerEnd = req.find("\r\n\r\n");
    std::string header = req.substr(0, headerEnd);
    std::string body = req.substr(headerEnd + 4);

    // ---- 请求行：METHOD SP PATH SP VERSION ----
    const size_t lineEnd = header.find("\r\n");
    const std::string requestLine =
        lineEnd == std::string::npos ? header : header.substr(0, lineEnd);
    std::string method;
    std::string rawPath;
    {
        size_t sp1 = requestLine.find(' ');
        if (sp1 == std::string::npos) return;
        size_t sp2 = requestLine.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) sp2 = requestLine.size();
        method = requestLine.substr(0, sp1);
        rawPath = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    }
    if (method.empty() || rawPath.empty()) return;

    // 剥查询串：path 参与静态文件映射与路由匹配（解码后），query 原样给路由。
    std::string query;
    std::string pathPart = rawPath;
    const size_t qpos = pathPart.find('?');
    if (qpos != std::string::npos) {
        query = pathPart.substr(qpos + 1);
        pathPart = pathPart.substr(0, qpos);
    }
    const std::string path = percentDecode(pathPart);

    // ---- 头部字段（只关心 content-length）----
    bool tooLarge = false;
    std::size_t contentLength = 0;
    if (lineEnd != std::string::npos) {
        size_t pos = lineEnd + 2;
        while (pos < header.size()) {
            size_t eol = header.find("\r\n", pos);
            if (eol == std::string::npos) eol = header.size();
            const std::string line = header.substr(pos, eol - pos);
            pos = eol + 2;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string k = toLower(line.substr(0, colon));
            std::string v = line.substr(colon + 1);
            // 去掉值两侧空白
            const size_t a = v.find_first_not_of(" \t");
            if (a != std::string::npos) {
                const size_t b = v.find_last_not_of(" \t");
                v = v.substr(a, b - a + 1);
            } else {
                v.clear();
            }
            if (k == "content-length") {
                if (!parseContentLength(v, contentLength)) {
                    sendText(s, 400, "text/plain; charset=utf-8", "bad content-length");
                    return;
                }
                if (contentLength > kMaxBodyBytes) tooLarge = true;  // 413 在读 body 前返回
            }
        }
    }

    if (tooLarge) {
        // R1-C：请求体上限 1 MiB。不等 body 到齐（可能根本没有），立即拒绝。
        sendText(s, 413, "text/plain; charset=utf-8", "request body exceeds 1 MiB");
        return;
    }

    // ---- 读满 body（头一次 recv 里已带来的前缀计入）----
    while (body.size() < contentLength) {
        const int n = recv(s, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) return;  // 半截 body：放弃
        body.append(buf, static_cast<std::size_t>(n));
    }
    if (body.size() > contentLength) body.resize(contentLength);

    // ---- 路由优先 ----
    if (router_) {
        std::string resp;
        std::string contentType = "application/json; charset=utf-8";
        int status = 200;
        if (router_(method, path, body, query, resp, contentType, status)) {
            sendResponse(s, status, contentType, resp.data(), resp.size());
            return;
        }
    }

    // ---- 静态文件（只对 GET 开放；目录穿越在 safeFilePath 里拦成 400/403）----
    if (method == "GET" || method == "HEAD") {
        serveStatic(s, path);
        return;
    }

    sendText(s, 404, "text/plain; charset=utf-8", "not found");
}

void HttpServer::sendAll(NativeSocket s, const char* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const int chunk = static_cast<int>(
            (len - sent > 0x40000000u) ? 0x40000000u : (len - sent));
        const int n = send(s, data + sent, chunk, 0);
        if (n <= 0) return;  // 对端断了：放弃剩余字节（Connection: close 语义）
        sent += static_cast<std::size_t>(n);
    }
}

void HttpServer::sendResponse(NativeSocket s, int status, const std::string& contentType,
                              const void* body, std::size_t len) {
    char head[512];
    const int n = std::snprintf(head, sizeof(head),
                                "HTTP/1.1 %s\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: %llu\r\n"
                                "Connection: close\r\n"
                                "Cache-Control: no-store\r\n"
                                "Server: agentsim\r\n"
                                "\r\n",
                                statusText(status), contentType.c_str(),
                                static_cast<unsigned long long>(len));
    if (n > 0) sendAll(s, head, static_cast<std::size_t>(n));
    if (len > 0 && body != nullptr) sendAll(s, static_cast<const char*>(body), len);
}

void HttpServer::sendText(NativeSocket s, int status, const std::string& contentType,
                          const std::string& text) {
    sendResponse(s, status, contentType, text.data(), text.size());
}

void HttpServer::serveStatic(NativeSocket s, const std::string& urlPath) {
    const std::string filePath = safeFilePath(webRoot_, urlPath);
    if (filePath.empty()) {
        // 400：形态不对或含 ".."（目录穿越一律拒绝，测试里 400/403 都算通过）。
        sendText(s, 400, "text/plain; charset=utf-8", "bad path");
        return;
    }
    std::ifstream f(filePath.c_str(), std::ios::binary);
    if (!f.good()) {
        sendText(s, 404, "text/plain; charset=utf-8", "not found: " + urlPath);
        return;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sendResponse(s, 200, mimeFor(filePath), data.data(), data.size());
}

} // namespace agentsim::app
