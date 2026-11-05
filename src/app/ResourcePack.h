#pragma once
#include <string>
#include <vector>
#include <map>
namespace agentsim::app {
struct ResourcePack {
    std::string name, desc, version, author;
    std::map<std::string, std::string> colors;   // key -> "r,g,b"（mode/标量键为原样字符串）
};
class ResourcePackStore {
public:
    // 扫 resourcepacks/<名>/pack.txt + colors.txt；enabled 列表决定加载顺序
    //（后者覆盖前者）。内置 classic_flat。
    static std::vector<ResourcePack> loadAll(const std::string& dir,
                                             const std::vector<std::string>& enabled,
                                             std::string& err);
    // 解析 colors.txt：mode = type_color + key = r,g,b 行式
    static ResourcePack parse(const std::string& dir, const std::string& packName,
                              std::string& err);
    // ---- R7-A 扩展（实现/路由所需，均为纯函数便于单测）----
    // 内置默认调色板（classic_flat 语义；resourcepacks 目录缺失/为空时的兜底）。
    static ResourcePack builtInDefaults();
    // 以内置默认打底，按 enabled 顺序叠加 all 中同名包的颜色（后者覆盖前者）。
    static ResourcePack mergeEnabled(const std::vector<ResourcePack>& all,
                                     const std::vector<std::string>& enabled);
    // "a, b,,c" -> ["a","b","c"]（去空白、丢空段）。
    static std::vector<std::string> parseEnabledList(const std::string& csv);
    // resourcepacks.list 行式：enabled=a,b；文件不存在 = 空列表且不报错。
    static std::vector<std::string> readListFile(const std::string& path, std::string& err);
    static bool writeListFile(const std::string& path,
                              const std::vector<std::string>& enabled, std::string& err);
    // 最小 JSON 字符串转义（" \ 控制字符，其余原样 UTF-8 直出）。
    static std::string jsonEscape(const std::string& s);
};
}
