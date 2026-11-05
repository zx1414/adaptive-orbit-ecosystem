// agentsim - app/Launcher.cpp : 发布形态入口（无控制台窗口）。
// 双击 agentsimw.exe → 以 CREATE_NO_WINDOW 派生同目录 agentsim.exe（全部参数透传）
// → 启动器立即退出。真正的服务生命周期/单实例探活/浏览器拉起都在 agentsim.exe 内。
// 设计取舍：不合并进 agentsim.exe（/SUBSYSTEM:WINDOWS 会让批跑 CLI 的 stdout 契约
// 和 PowerShell 管道语义复杂化）；启动器是纯 Win32 壳，与仿真零耦合。
#include <windows.h>
#pragma comment(lib, "user32.lib")  // MessageBoxW（/SUBSYSTEM:WINDOWS 不自动链 user32）
#include <string>
#include <shellapi.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    // 同目录 agentsim.exe
    std::wstring dir(exePath);
    const std::size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir = dir.substr(0, slash + 1);
    const std::wstring target = dir + L"agentsim.exe";

    // 命令行透传（去掉 argv[0]）
    LPWSTR cmdRaw = GetCommandLineW();
    // 简单跳过第一个 token（考虑引号）
    std::wstring args;
    bool inQuote = false;
    bool started = false;
    for (const wchar_t* p = cmdRaw; *p; ++p) {
        if (*p == L'"') inQuote = !inQuote;
        if (!started && !inQuote && *p == L' ') { started = true; continue; }
        if (started) args += *p;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + target + L"\"" +
        (args.empty() ? std::wstring() : L" " + args);
    // CREATE_NO_WINDOW：控制台子进程不弹窗；DETACHED_PROCESS 亦可，取 NO_WINDOW 语义更直观。
    const BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW | DETACHED_PROCESS,
                                   nullptr, dir.c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        // 罕见：agentsim.exe 缺失。无控制台可说话——消息框兜底（双击场景用户看得见）。
        MessageBoxW(nullptr, L"agentsim.exe not found next to this launcher.",
                    L"agentsim", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
