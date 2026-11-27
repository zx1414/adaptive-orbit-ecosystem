#include <windows.h>
#include <shlobj.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#ifndef AGENTSIM_VERSION
#define AGENTSIM_VERSION dev
#endif
#define AGENTSIM_VERSION_TEXT(x) #x
#define AGENTSIM_VERSION_VALUE(x) AGENTSIM_VERSION_TEXT(x)

namespace {
namespace fs = std::filesystem;

struct Asset {
    int id;
    const char* path;
    bool refresh;
};
constexpr Asset kAssets[] = {
#define ASSET(id, path, refresh) {id, path, refresh},
#include "StandaloneAssets.inc"
#undef ASSET
};

HRSRC findAsset(HMODULE module, int id) {
    return FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
}

bool sameBytes(const fs::path& path, const char* data, DWORD size) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(size)) return false;
    input.seekg(0);
    std::array<char, 8192> chunk{};
    for (std::size_t offset = 0; offset < size;) {
        const auto remaining = static_cast<std::size_t>(size) - offset;
        const auto count = remaining < chunk.size() ? remaining : chunk.size();
        if (!input.read(chunk.data(), static_cast<std::streamsize>(count)) ||
            std::memcmp(chunk.data(), data + offset, count) != 0) return false;
        offset += count;
    }
    return true;
}

bool extractAsset(HMODULE module, const Asset& asset, const fs::path& root, std::wstring& failure) {
    const auto target = root / asset.path;
    std::error_code error;
    if (!asset.refresh && fs::is_regular_file(target, error)) return true;
    const HRSRC resource = findAsset(module, asset.id);
    const DWORD size = resource ? SizeofResource(module, resource) : 0;
    const HGLOBAL payload = size ? LoadResource(module, resource) : nullptr;
    const auto* data = payload ? static_cast<const char*>(LockResource(payload)) : nullptr;
    if (!data) {
        failure = L"无法读取内置资源 #" + std::to_wstring(asset.id);
        return false;
    }
    if (sameBytes(target, data, size)) return true;

    fs::create_directories(target.parent_path(), error);
    if (error) {
        failure = L"无法创建目录：" + target.parent_path().native() +
                  L"（错误 " + std::to_wstring(error.value()) + L"）";
        return false;
    }
    fs::path temporary = target;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const bool written = static_cast<bool>(output.write(data, static_cast<std::streamsize>(size)));
    output.close();
    const bool installed = written && output &&
        MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (installed) return true;
    const DWORD moveError = GetLastError();
    fs::remove(temporary, error);
    if (sameBytes(target, data, size)) return true;
    failure = L"无法写入：" + target.native();
    if (written && output) failure += L"（错误 " + std::to_wstring(moveError) + L"）";
    return false;
}

fs::path gameDirectory(HMODULE module, std::wstring& failure) {
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        failure = L"无法读取启动器目录。";
        return {};
    }
    const auto executableDir = fs::path(path).parent_path();
    if (!findAsset(module, kAssets[0].id)) return executableDir;

    constexpr char version[] = AGENTSIM_VERSION_VALUE(AGENTSIM_VERSION);
    const auto unpack = [&](const fs::path& root) {
        for (const auto& asset : kAssets) {
            if (!extractAsset(module, asset, root, failure)) return false;
        }
        return true;
    };

    wchar_t localData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localData))) {
        const auto root = fs::path(localData) / "agentsim" / "portable" / version;
        if (unpack(root)) return root;
    }
    const auto fallback = executableDir / ".agentsim" / version;
    return unpack(fallback) ? fallback : fs::path{};
}

const wchar_t* commandTail() {
    const wchar_t* tail = GetCommandLineW();
    bool inQuote = false;
    while (*tail != L'\0') {
        if (*tail == L'"') inQuote = !inQuote;
        if (!inQuote && (*tail == L' ' || *tail == L'\t')) break;
        ++tail;
    }
    return tail;
}
} // namespace

int WINAPI WinMain(HINSTANCE module, HINSTANCE, LPSTR, int) {
    std::wstring failure;
    const auto root = gameDirectory(module, failure);
    bool started = false;
    if (!root.empty()) {
        const auto executable = root / "agentsim.exe";
        std::wstring command = L"\"" + executable.native() + L"\"" + commandTail();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process) != FALSE;
        if (started) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            failure = L"无法启动模拟程序（错误 " + std::to_wstring(GetLastError()) + L"）。\n" +
                      executable.native();
        }
    }
    if (!started) MessageBoxW(nullptr, failure.c_str(), L"agentsim", MB_OK | MB_ICONERROR);
    return started ? 0 : 1;
}
