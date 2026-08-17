#include "WinFs.h"
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &w[0], n);
    return w;
}
std::string fromWide(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
}  // namespace

bool WinFs::mkdirs(const std::string& utf8Path) {
    std::wstring w = toWide(utf8Path);
    if (w.empty()) return false;
    // 逐个组件创建（支持 / 与 \ 混合）。
    std::wstring cur;
    for (size_t i = 0; i <= w.size(); ++i) {
        if (i < w.size() && w[i] != L'/' && w[i] != L'\\') {
            cur += w[i];  // 普通字符：累积当前组件
            continue;
        }
        if (cur.empty()) {            // 以分隔符开头（UNC 等）
            cur += L'\\';
            continue;
        }
        if (cur.size() == 2 && cur[1] == L':') {  // 盘符根 "D:"
            cur += L'\\';
            continue;
        }
        if (cur.back() == L'\\') continue;  // 连续分隔符
        if (!CreateDirectoryW(cur.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
        cur += L'\\';
    }
    return true;
}

bool WinFs::removeAll(const std::string& utf8Path) {
    std::wstring w = toWide(utf8Path);
    if (w.empty()) return false;
    DWORD attr = GetFileAttributesW(w.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;  // 不存在即视为完成
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return DeleteFileW(w.c_str()) != 0;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((w + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring child = w + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeAll(fromWide(child));
            } else {
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(w.c_str()) != 0;
}

bool WinFs::renamePath(const std::string& utf8From, const std::string& utf8To) {
    std::wstring a = toWide(utf8From);
    std::wstring b = toWide(utf8To);
    return MoveFileW(a.c_str(), b.c_str()) != 0;
}

bool WinFs::exists(const std::string& utf8Path) {
    std::wstring w = toWide(utf8Path);
    return GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<std::string> WinFs::listDirs(const std::string& utf8Dir) {
    std::vector<std::string> out;
    std::wstring w = toWide(utf8Dir);
    if (w.empty()) return out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((w + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            out.push_back(fromWide(name));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

std::string WinFs::toAnsi(const std::string& utf8) {
    std::wstring w = toWide(utf8);
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return utf8;
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

#else  // 非 Windows：UTF-8 直接透传

#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

bool WinFs::mkdirs(const std::string& utf8Path) {
    // 简单递归：逐个前缀尝试。
    std::string cur;
    for (size_t i = 0; i <= utf8Path.size(); ++i) {
        if (i < utf8Path.size() && utf8Path[i] != '/' && utf8Path[i] != '\\') continue;
        if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
        cur += '/';
    }
    return true;
}
bool WinFs::removeAll(const std::string& p) {
    std::string cmd = "rm -rf \"" + p + "\"";
    return system(cmd.c_str()) == 0;
}
bool WinFs::renamePath(const std::string& a, const std::string& b) {
    return ::rename(a.c_str(), b.c_str()) == 0;
}
bool WinFs::exists(const std::string& p) { return ::access(p.c_str(), F_OK) == 0; }
std::vector<std::string> WinFs::listDirs(const std::string& d) {
    std::vector<std::string> out;
    DIR* dp = ::opendir(d.c_str());
    if (!dp) return out;
    struct dirent* e;
    while ((e = ::readdir(dp)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        if (e->d_type == DT_DIR) out.push_back(n);
    }
    ::closedir(dp);
    return out;
}
std::string WinFs::toAnsi(const std::string& utf8) { return utf8; }
#endif
