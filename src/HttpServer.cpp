#include "HttpServer.h"
#include "WinFs.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace {
std::string mimeFor(const std::string& path) {
    auto ext = [&]() -> std::string {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return "";
        std::string e = path.substr(dot);
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return e;
    }();
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// 把 URL 路径安全映射到 web 根目录下的文件路径；返回空串表示非法。
std::string safeFilePath(const std::string& root, const std::string& urlPath) {
    if (urlPath.empty() || urlPath[0] != '/') return "";
    std::string rel = urlPath.substr(1);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    // 拒绝路径穿越
    if (rel.find("..") != std::string::npos) return "";
    if (rel.empty()) rel = "index.html";
    return root + "/" + rel;
}

// 百分号解码（%XX → 字节，+ → 空格），用于带中文名的存档/材质包路径。
std::string percentDecode(const std::string& s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            out.push_back((char)(hex(s[i + 1]) * 16 + hex(s[i + 2])));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}
}  // namespace

HttpServer::HttpServer() {
#ifdef _WIN32
    WSADATA wsa;
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
    for (int attempt = 0; attempt < 20; ++attempt) {
        int p = port + attempt;
        NativeSocket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return -1;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 仅本机
        addr.sin_port = htons((u_short)p);
        int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(s);
            continue;  // 端口占用，试下一个
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
    if (listenSocket_ != (NativeSocket)~(NativeSocket)0) {
        closesocket(listenSocket_);
        listenSocket_ = (NativeSocket)~(NativeSocket)0;
    }
    if (thread_.joinable()) thread_.join();
}

void HttpServer::acceptLoop() {
    while (running_) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        NativeSocket c = accept(listenSocket_, (sockaddr*)&from, &fromLen);
        if (c == INVALID_SOCKET) {
            if (running_) {
                // 监听套接字被关闭（stop）或偶发错误；偶发错误短暂退避后继续。
#ifdef _WIN32
                Sleep(5);
#else
                usleep(5000);
#endif
            }
            continue;
        }
        handleClient(c);
        closesocket(c);
    }
}

void HttpServer::handleClient(NativeSocket s) {
    // 读请求头（直到空行）。限制大小防异常客户端。
    std::string req;
    req.reserve(4096);
    char buf[2048];
    while (req.size() < 64 * 1024) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return;  // 对端关闭/出错
        req.append(buf, (size_t)n);
        if (req.find("\r\n\r\n") != std::string::npos) break;
    }
    size_t headerEnd = req.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return;

    std::string header = req.substr(0, headerEnd);
    std::string body = req.substr(headerEnd + 4);

    std::istringstream hs(header);
    std::string line;
    if (!std::getline(hs, line)) return;
    // 请求行：METHOD SP PATH SP VERSION
    std::string method, path, version;
    {
        std::istringstream ls(line);
        ls >> method >> path >> version;
    }
    if (path.empty()) return;
    // 去掉查询串，并百分号解码（支持中文路径）。
    size_t q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);
    path = percentDecode(path);

    // 读取 Content-Length 对应的剩余 body（POST 控制指令）。
    size_t contentLength = 0;
    {
        std::string hl;
        while (std::getline(hs, hl)) {
            if (hl.size() > 2 && hl.back() == '\r') hl.pop_back();
            auto pos = hl.find(':');
            if (pos == std::string::npos) continue;
            std::string k = hl.substr(0, pos);
            std::transform(k.begin(), k.end(), k.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (k == "content-length") {
                contentLength = (size_t)std::atoll(hl.substr(pos + 1).c_str());
            }
        }
    }
    while (body.size() < contentLength) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return;
        body.append(buf, (size_t)n);
    }
    if (body.size() > contentLength) body.resize(contentLength);

    if (method == "GET" && path == "/state") {
        if (!stateProvider_) {
            sendText(s, "404 Not Found", "text/plain; charset=utf-8", "no state");
            return;
        }
        std::vector<uint8_t> data = stateProvider_();
        sendResponse(s, "200 OK", "application/octet-stream", data.data(), data.size());
        return;
    }

    if (method == "POST" && path == "/control") {
        if (!controlHandler_) {
            sendText(s, "404 Not Found", "text/plain; charset=utf-8", "no handler");
            return;
        }
        // body 形如 {"cmd":"pause"} —— 极简提取 cmd 值。
        std::string cmd;
        auto pos = body.find("\"cmd\"");
        if (pos != std::string::npos) {
            pos = body.find(':', pos);
            if (pos != std::string::npos) {
                size_t q0 = body.find('"', pos);
                if (q0 != std::string::npos) {
                    size_t q1 = body.find('"', q0 + 1);
                    if (q1 != std::string::npos) cmd = body.substr(q0 + 1, q1 - q0 - 1);
                }
            }
        }
        controlHandler_(cmd);
        sendText(s, "200 OK", "application/json; charset=utf-8", "{\"ok\":true}");
        return;
    }

    // 通用路由（/status、/schema、/run 等），未命中则回落到静态文件。
    if (router_) {
        std::string resp, contentType;
        int status = 200;
        if (router_(method, path, body, resp, contentType, status)) {
            const char* statusLine =
                status == 404 ? "404 Not Found"
                              : (status == 400 ? "400 Bad Request" : "200 OK");
            sendResponse(s, statusLine, contentType, resp.data(), resp.size());
            return;
        }
    }

    if (method == "GET") {
        sendFile(s, path);
        return;
    }

    sendText(s, "404 Not Found", "text/plain; charset=utf-8", "not found");
}

void HttpServer::sendResponse(NativeSocket s, const std::string& status,
                              const std::string& contentType,
                              const void* body, size_t len) {
    char head[512];
    int n = std::snprintf(head, sizeof(head),
                          "HTTP/1.1 %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "Cache-Control: no-store\r\n"
                          "\r\n",
                          status.c_str(), contentType.c_str(), len);
    if (n > 0) ::send(s, head, (size_t)n, 0);
    if (len > 0) ::send(s, (const char*)body, (int)len, 0);
}

void HttpServer::sendText(NativeSocket s, const std::string& status,
                          const std::string& contentType, const std::string& text) {
    sendResponse(s, status, contentType, text.data(), text.size());
}

void HttpServer::sendFile(NativeSocket s, const std::string& urlPath) {
    std::string filePath = safeFilePath(webRoot_, urlPath);
    if (filePath.empty()) {
        sendText(s, "400 Bad Request", "text/plain; charset=utf-8", "bad path");
        return;
    }
    // webRoot 为 UTF-8（含中文路径时 CRT 窄字符文件 API 会按 ANSI 解释），转 ANSI 再打开。
    std::ifstream f(WinFs::toAnsi(filePath), std::ios::binary);
    if (!f.good()) {
        sendText(s, "404 Not Found", "text/plain; charset=utf-8", "not found: " + urlPath);
        return;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sendResponse(s, "200 OK", mimeFor(filePath), data.data(), data.size());
}
