// agentsim - app/ResourcePack.cpp : 材质包（resourcepacks/<名>/pack.txt + colors.txt）
// R7-A。行式解析语义照旧版 v2.3.1 web/app.js parseColors()：
//   - 空行 / '#' 注释行跳过；首个 '=' 分 key/value（两侧 trim）；
//   - mode / 字符串键 / 标量键原样存；其余键必须是 r,g,b 三元组（>=3 段取前 3 段、
//     全部为合法整数，归一化为 "r,g,b"），不合法整行丢弃；
//   - 合并语义：以内置默认打底，enabled 顺序叠加，后者覆盖前者。
#include "app/ResourcePack.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace agentsim::app {
namespace {

std::string trimCopy(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// 去 UTF-8 BOM（本仓写出的 pack 文件不带 BOM，但用户手编文件可能带）。
void stripBom(std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

bool readFileLines(const std::string& path, std::vector<std::string>& lines, std::string& err) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) {
            stripBom(line);
            first = false;
        }
        lines.push_back(line);
    }
    if (f.bad()) {
        err = "read error on " + path;
        return false;
    }
    return true;
}

// 首个 '=' 分 key/value；没有 '=' 返回 false（照旧版 parseColors 的静默跳过语义）。
bool splitKeyValue(const std::string& line, std::string& key, std::string& val) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = trimCopy(line.substr(0, eq));
    val = trimCopy(line.substr(eq + 1));
    return true;
}

// 严格十进制整数（可选 +/- 前缀）；拒绝空串/杂字符。
bool parseIntStrict(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-' || s[0] == '+') ? 1u : 0u;
    if (i >= s.size()) return false;
    long long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        if (v > 100000000LL) return false;   // 防溢出（颜色分量远用不到这个量级）
    }
    out = static_cast<int>(s[0] == '-' ? -v : v);
    return true;
}

// 标量键（不带逗号的数值），集合照抄旧版 app.js 的 SCALAR_KEYS。
bool isScalarKey(const std::string& k) {
    static const char* const kKeys[] = {
        "size_ball_shield", "size_ball_worker", "size_ball_scout",
        "size_free_ball", "size_nucleus", "cyano_node_size", "cyano_link_alpha",
    };
    for (const char* const cand : kKeys) {
        if (k == cand) return true;
    }
    return false;
}

// 字符串值键（枚举），照抄旧版 app.js 的 STRING_KEYS。
bool isStringKey(const std::string& k) {
    return k == "cyano_color_mode";
}

std::vector<std::string> splitBy(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// 颜色值："r, g, b"（>=3 段取前 3 段、全部合法整数）-> 归一化 "r,g,b"。
// 每段先 trim 再 parse（照旧版 parseColors 的 parseInt(s.trim(), 10)）——
// "10, 14, 26" 的 " 14" 不 trim 会被当成非整数整行丢弃，示例包全色失效。
bool parseColorTriple(const std::string& val, std::string& out) {
    const std::vector<std::string> parts = splitBy(val, ',');
    if (parts.size() < 3u) return false;
    int rgb[3] = {0, 0, 0};
    for (std::size_t i = 0; i < 3u; ++i) {
        if (!parseIntStrict(trimCopy(parts[i]), rgb[i])) return false;
    }
    out = std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]);
    return true;
}

void applyColorsLine(ResourcePack& p, const std::string& key, const std::string& val) {
    if (key == "mode" || isScalarKey(key) || isStringKey(key)) {
        p.colors[key] = val;
        return;
    }
    std::string rgb;
    if (parseColorTriple(val, rgb)) p.colors[key] = rgb;
    // 不合法颜色行：整行丢弃（照旧版 parseColors 的静默语义）。
}

}  // namespace

ResourcePack ResourcePackStore::parse(const std::string& dir, const std::string& packName,
                                      std::string& err) {
    err.clear();
    ResourcePack p;
    p.name = packName;
    const std::string base = dir + "/" + packName;

    std::vector<std::string> lines;
    std::string sub;
    if (!readFileLines(base + "/pack.txt", lines, sub)) {
        err = "resource pack '" + packName + "': " + sub;
        return p;
    }
    for (const std::string& raw : lines) {
        const std::string line = trimCopy(raw);
        if (line.empty() || line[0] == '#') continue;
        std::string key;
        std::string val;
        if (!splitKeyValue(line, key, val)) continue;
        if (key == "name") {
            if (!val.empty()) p.name = val;
        } else if (key == "desc") {
            p.desc = val;
        } else if (key == "version") {
            p.version = val;
        } else if (key == "author") {
            p.author = val;
        }
        // 未知键忽略。
    }

    lines.clear();
    if (!readFileLines(base + "/colors.txt", lines, sub)) {
        err = "resource pack '" + packName + "': " + sub;
        return p;
    }
    for (const std::string& raw : lines) {
        const std::string line = trimCopy(raw);
        if (line.empty() || line[0] == '#') continue;
        std::string key;
        std::string val;
        if (!splitKeyValue(line, key, val)) continue;
        applyColorsLine(p, key, val);
    }
    return p;
}

std::vector<ResourcePack> ResourcePackStore::loadAll(const std::string& dir,
                                                     const std::vector<std::string>& enabled,
                                                     std::string& err) {
    err.clear();
    std::vector<ResourcePack> out;

    std::error_code ec;
    std::vector<std::string> dirNames;
    for (std::filesystem::directory_iterator dit(dir, ec), ditEnd;
         !ec && dit != ditEnd; dit.increment(ec)) {
        std::error_code ec2;
        if (dit->is_directory(ec2) && !ec2) {
            dirNames.push_back(dit->path().filename().string());
        }
    }
    if (ec) {
        err = "cannot scan resourcepacks dir: " + dir + " (" + ec.message() + ")";
        return out;
    }
    std::sort(dirNames.begin(), dirNames.end());

    // 逐目录解析；半成品目录（缺 pack.txt / colors.txt）跳过并记入 err 诊断。
    std::map<std::string, ResourcePack> found;   // 以 pack.txt 的 name 为准；同名首见者胜
    std::vector<std::string> order;              // 发现顺序（目录名排序后）
    for (const std::string& dn : dirNames) {
        std::string perr;
        ResourcePack p = parse(dir, dn, perr);
        if (!perr.empty()) {
            if (!err.empty()) err += "; ";
            err += perr;
            continue;
        }
        if (found.find(p.name) == found.end()) order.push_back(p.name);
        found.emplace(p.name, p);
    }

    // 输出顺序：enabled 优先（按 enabled 顺序），其余按发现顺序殿后。
    std::map<std::string, bool> pushed;
    std::map<std::string, bool> missingSeen;
    std::string missing;
    for (const std::string& en : enabled) {
        const auto it = found.find(en);
        if (it != found.end()) {
            if (!pushed[it->first]) {
                out.push_back(it->second);
                pushed[it->first] = true;
            }
        } else if (!missingSeen[en]) {
            missingSeen[en] = true;
            if (!missing.empty()) missing += ", ";
            missing += en;
        }
    }
    for (const std::string& n : order) {
        const auto it = found.find(n);
        if (it == found.end() || pushed[it->first]) continue;
        out.push_back(it->second);
        pushed[it->first] = true;
    }
    if (!missing.empty()) {
        if (!err.empty()) err += "; ";
        err += "enabled pack(s) not found: " + missing;
    }
    return out;
}

ResourcePack ResourcePackStore::builtInDefaults() {
    ResourcePack p;
    p.name = "classic_flat";
    p.desc = "built-in classic flat palette (fallback)";
    p.version = "1.0";
    p.author = "built-in";
    p.colors["mode"] = "type_color";
    p.colors["background"] = "10,14,26";
    p.colors["ball_shield"] = "255,207,94";
    p.colors["ball_worker"] = "69,224,216";
    p.colors["ball_scout"] = "183,139,255";
    p.colors["free_ball"] = "200,200,210";
    p.colors["nucleus"] = "255,255,255";
    return p;
}

ResourcePack ResourcePackStore::mergeEnabled(const std::vector<ResourcePack>& all,
                                             const std::vector<std::string>& enabled) {
    ResourcePack m = builtInDefaults();
    m.name = "active";
    m.desc = "merged active palette (built-in defaults + enabled packs)";
    for (const std::string& en : enabled) {
        for (const ResourcePack& p : all) {
            if (p.name != en) continue;
            for (const auto& kv : p.colors) m.colors[kv.first] = kv.second;
        }
    }
    return m;
}

std::vector<std::string> ResourcePackStore::parseEnabledList(const std::string& csv) {
    std::vector<std::string> out;
    for (const std::string& tok : splitBy(csv, ',')) {
        const std::string t = trimCopy(tok);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

std::vector<std::string> ResourcePackStore::readListFile(const std::string& path,
                                                         std::string& err) {
    err.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};   // 不存在 = 只用内置默认色（不是错误）
    }
    std::vector<std::string> lines;
    std::string sub;
    if (!readFileLines(path, lines, sub)) {
        err = sub;
        return {};
    }
    std::vector<std::string> out;
    for (const std::string& raw : lines) {
        const std::string line = trimCopy(raw);
        if (line.empty() || line[0] == '#') continue;
        std::string key;
        std::string val;
        if (!splitKeyValue(line, key, val)) continue;
        if (key != "enabled") continue;
        out = parseEnabledList(val);   // 多行 enabled= 时后行覆盖
    }
    return out;
}

bool ResourcePackStore::writeListFile(const std::string& path,
                                      const std::vector<std::string>& enabled,
                                      std::string& err) {
    err.clear();
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot write " + path;
        return false;
    }
    f << "# agentsim resource packs (enabled= names in order; later overrides earlier)" << static_cast<char>(0x0A);
    f << "enabled=";
    for (std::size_t i = 0; i < enabled.size(); ++i) {
        if (i) f << ",";
        f << enabled[i];
    }
    f << static_cast<char>(0x0A);
    f.flush();
    if (!f) {
        err = "write failed on " + path;
        return false;
    }
    return true;
}

std::string ResourcePackStore::jsonEscape(const std::string& s) {
    static const char* kHex = "0123456789abcdef";
    const char kQuote = static_cast<char>(0x22);
    const char kBkSlash = static_cast<char>(0x5C);
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == 0x22u) {
            out += kBkSlash;
            out += kQuote;
        } else if (c == 0x5Cu) {
            out += kBkSlash;
            out += kBkSlash;
        } else if (c < 0x20u) {
            switch (c) {
                case 0x08u: out += kBkSlash; out += 'b'; break;
                case 0x09u: out += kBkSlash; out += 't'; break;
                case 0x0Au: out += kBkSlash; out += 'n'; break;
                case 0x0Cu: out += kBkSlash; out += 'f'; break;
                case 0x0Du: out += kBkSlash; out += 'r'; break;
                default:
                    out += kBkSlash;
                    out += 'u';
                    out += '0';
                    out += '0';
                    out += kHex[(c >> 4) & 0xFu];
                    out += kHex[c & 0xFu];
                    break;
            }
        } else {
            out += ch;
        }
    }
    return out;
}

}  // namespace agentsim::app
