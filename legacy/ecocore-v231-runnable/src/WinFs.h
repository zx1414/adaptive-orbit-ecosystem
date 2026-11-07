#pragma once
#include <string>
#include <vector>

// WinFs：UTF-8 路径的文件/目录操作。
// Windows 下内部转 UTF-16 走 Win32 API（std::filesystem 在此工具链对 GBK/中文路径
// 的转换不可靠，故显式处理）；非 Windows 平台直接透传 UTF-8。
namespace WinFs {

// 递归创建目录（已存在不算错）。
bool mkdirs(const std::string& utf8Path);

// 递归删除文件/目录。
bool removeAll(const std::string& utf8Path);

// 重命名（目录或文件）。
bool renamePath(const std::string& utf8From, const std::string& utf8To);

// 是否存在。
bool exists(const std::string& utf8Path);

// 列出子目录名（UTF-8）。
std::vector<std::string> listDirs(const std::string& utf8Dir);

// 转成 ANSI（GBK）窄字符路径：仅供 CRT 的 ofstream/ifstream 使用，
// 内部一律保持 UTF-8。
std::string toAnsi(const std::string& utf8);
}
