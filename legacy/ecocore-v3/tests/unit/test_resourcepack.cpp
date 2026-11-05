// agentsim - tests/unit/test_resourcepack.cpp
// R7-A：ResourcePackStore 单测。与 test_http.cpp 同理：tools/build.ps1 的单测目标
// 不链 src/app，直接 #include 翻译单元（日后若构建脚本把 app 源加进单测链接集，
// 必须删除这些 #include，否则符号重复）。
// 约定（与 test_cli.cpp 相同）：每个 app/*.cpp 只允许被一个测试翻译单元并源；
// 本文件只并 ResourcePack.cpp。
#include "../src/app/ResourcePack.cpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <vector>

#include "test_framework.h"

namespace {

namespace fs = std::filesystem;

int g_tempCounter = 0;

// 每个测试独立临时目录（计数器保证唯一；先清残留再建）。
std::string makeTempRoot() {
    ++g_tempCounter;
    const std::string root = fs::temp_directory_path().string() +
                             "/agentsim_rp_test_" + std::to_string(g_tempCounter);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    f << content;
}

// 缺 key 时返回空串而不是抛异常（.at() 抛 std::out_of_range 会让不接异常的
// tests/unit/main.cpp 直接 terminate，吞掉整套套件的后续结果）。
const std::string& colorAt(const std::map<std::string, std::string>& m, const char* key) {
    static const std::string kEmpty;
    const auto it = m.find(key);
    return it == m.end() ? kEmpty : it->second;
}

// 项目根探测（build.ps1 / check.ps1 都把 CWD 定到项目根跑单测；再兜底上溯两层）。
std::string projectRoot() {
    static const char* const kCands[] = {".", "..", "../.."};
    for (const char* const cand : kCands) {
        const std::string probe = std::string(cand) + "/README.md";
        std::ifstream pf(probe.c_str());
        if (pf.good()) return cand;
    }
    return "";
}

}  // namespace

// 1) 真实示例包解析出全部 7 个颜色键（卡面要求）。
TEST(resourcepack_parse_sample_seven_color_keys) {
    const std::string root = projectRoot();
    const bool rootOk = !root.empty();
    CHECK(rootOk);
    if (!rootOk) return;
    std::string err;
    const agentsim::app::ResourcePack p =
        agentsim::app::ResourcePackStore::parse(root + "/resourcepacks", "classic_flat", err);
    CHECK(err.empty());
    CHECK(p.name == "classic_flat");
    CHECK(p.version == "1.0");
    CHECK(!p.desc.empty());
    CHECK(!p.author.empty());
    CHECK(p.colors.size() == 7u);
    CHECK(p.colors.count("mode") == 1u);
    CHECK(p.colors.count("background") == 1u);
    CHECK(p.colors.count("ball_shield") == 1u);
    CHECK(p.colors.count("ball_worker") == 1u);
    CHECK(p.colors.count("ball_scout") == 1u);
    CHECK(p.colors.count("free_ball") == 1u);
    CHECK(p.colors.count("nucleus") == 1u);
    CHECK(colorAt(p.colors, "mode") == "type_color");
    CHECK(colorAt(p.colors, "background") == "10,14,26");
    CHECK(colorAt(p.colors, "ball_shield") == "255,207,94");
    CHECK(colorAt(p.colors, "ball_worker") == "69,224,216");
    CHECK(colorAt(p.colors, "ball_scout") == "183,139,255");
    CHECK(colorAt(p.colors, "free_ball") == "200,200,210");
    CHECK(colorAt(p.colors, "nucleus") == "255,255,255");
}

// 2) 堆叠顺序：两个包同键后者覆盖（卡面要求）。
TEST(resourcepack_merge_later_enabled_pack_overrides) {
    const std::string root = makeTempRoot();
    fs::create_directories(root + "/a");
    writeFile(root + "/a/pack.txt",
              "name = pack_a\ndesc = A\nversion = 1.0\nauthor = t\n");
    writeFile(root + "/a/colors.txt", "mode = type_color\nball_shield = 10, 20, 30\nbackground = 1, 2, 3\n");
    fs::create_directories(root + "/b");
    writeFile(root + "/b/pack.txt",
              "name = pack_b\ndesc = B\nversion = 1.0\nauthor = t\n");
    writeFile(root + "/b/colors.txt", "mode = type_color\nball_shield = 99, 100, 110\nnucleus = 250, 251, 252\n");

    std::string err;
    const auto packs =
        agentsim::app::ResourcePackStore::loadAll(root, {"pack_a", "pack_b"}, err);
    CHECK(err.empty());
    CHECK(packs.size() == 2u);
    const agentsim::app::ResourcePack merged =
        agentsim::app::ResourcePackStore::mergeEnabled(packs, {"pack_a", "pack_b"});
    CHECK(colorAt(merged.colors, "ball_shield") == "99,100,110");   // 后者覆盖前者
    CHECK(colorAt(merged.colors, "background") == "1,2,3");         // 只有前者有 → 保留
    CHECK(colorAt(merged.colors, "nucleus") == "250,251,252");      // 只有后者有 → 叠加
    const agentsim::app::ResourcePack mergedRev =
        agentsim::app::ResourcePackStore::mergeEnabled(packs, {"pack_b", "pack_a"});
    CHECK(colorAt(mergedRev.colors, "ball_shield") == "10,20,30");  // 反序 → 结果随之翻转
}

// 3) colors.txt 缺 key 时用内置默认兜底（卡面要求）。
TEST(resourcepack_missing_key_falls_back_to_default) {
    const std::string root = makeTempRoot();
    fs::create_directories(root + "/p");
    writeFile(root + "/p/pack.txt", "name = p\n");
    writeFile(root + "/p/colors.txt", "mode = type_color\nbackground = 40, 50, 60\n");
    std::string err;
    const auto packs = agentsim::app::ResourcePackStore::loadAll(root, {"p"}, err);
    CHECK(err.empty());
    const auto merged = agentsim::app::ResourcePackStore::mergeEnabled(packs, {"p"});
    CHECK(colorAt(merged.colors, "background") == "40,50,60");      // 包里有的生效
    CHECK(colorAt(merged.colors, "ball_shield") == "255,207,94");   // 缺 key → 内置默认
    CHECK(colorAt(merged.colors, "nucleus") == "255,255,255");
    CHECK(colorAt(merged.colors, "mode") == "type_color");
}

// 4) pack.txt 缺字段时 desc/author/version 为空串、不报错（卡面要求）。
TEST(resourcepack_missing_pack_fields_default_empty) {
    const std::string root = makeTempRoot();
    fs::create_directories(root + "/bare");
    writeFile(root + "/bare/pack.txt", "name = bare\n");
    writeFile(root + "/bare/colors.txt", "nucleus = 1, 2, 3\n");
    std::string err;
    const agentsim::app::ResourcePack p =
        agentsim::app::ResourcePackStore::parse(root, "bare", err);
    CHECK(err.empty());
    CHECK(p.desc.empty());
    CHECK(p.author.empty());
    CHECK(p.version.empty());
    CHECK(colorAt(p.colors, "nucleus") == "1,2,3");
}

// 5) 非法目录 → err（卡面要求）。
TEST(resourcepack_invalid_dir_reports_err) {
    {
        std::string err;
        agentsim::app::ResourcePackStore::parse("definitely/not/here", "nope", err);
        CHECK(!err.empty());
    }
    {   // 目录存在但缺 colors.txt → err
        const std::string root = makeTempRoot();
        fs::create_directories(root + "/half");
        writeFile(root + "/half/pack.txt", "name = half\n");
        std::string err;
        agentsim::app::ResourcePackStore::parse(root, "half", err);
        CHECK(!err.empty());
    }
    {
        std::string err;
        const auto packs =
            agentsim::app::ResourcePackStore::loadAll("definitely/not/here", {}, err);
        CHECK(!err.empty());
        CHECK(packs.empty());
    }
}

// 6) enabled 顺序决定覆盖（loadAll 输出跟随 enabled 序；卡面要求）。
TEST(resourcepack_enabled_order_decides_override) {
    const std::string root = makeTempRoot();
    fs::create_directories(root + "/first");
    fs::create_directories(root + "/second");
    writeFile(root + "/first/pack.txt", "name = first\n");
    writeFile(root + "/first/colors.txt", "ball_worker = 11, 12, 13\n");
    writeFile(root + "/second/pack.txt", "name = second\n");
    writeFile(root + "/second/colors.txt", "ball_worker = 21, 22, 23\n");
    std::string err;
    const auto packs =
        agentsim::app::ResourcePackStore::loadAll(root, {"second", "first"}, err);
    CHECK(err.empty());
    CHECK(packs.size() == 2u);
    CHECK(packs[0].name == "second");   // 输出顺序跟随 enabled
    CHECK(packs[1].name == "first");
    const auto merged =
        agentsim::app::ResourcePackStore::mergeEnabled(packs, {"second", "first"});
    CHECK(colorAt(merged.colors, "ball_worker") == "11,12,13");   // enabled 序里 first 在后 → 覆盖
}

// 7) resourcepacks.list 读写回路（不存在 = 空列表且不报错）。
TEST(resourcepack_list_file_roundtrip) {
    const std::string root = makeTempRoot();
    const std::string path = root + "/resourcepacks.list";
    {
        std::string err;
        const auto enabled = agentsim::app::ResourcePackStore::readListFile(path, err);
        CHECK(err.empty());
        CHECK(enabled.empty());
    }
    std::string err;
    CHECK(agentsim::app::ResourcePackStore::writeListFile(path, {"classic_flat", "big_beads"}, err));
    CHECK(err.empty());
    const auto enabled = agentsim::app::ResourcePackStore::readListFile(path, err);
    CHECK(err.empty());
    CHECK(enabled.size() == 2u);
    CHECK(enabled[0] == "classic_flat");
    CHECK(enabled[1] == "big_beads");
}

// 8) colors.txt 行式格式边界（照旧版 parseColors 语义）。
TEST(resourcepack_colors_line_formats) {
    const std::string root = makeTempRoot();
    fs::create_directories(root + "/mix");
    writeFile(root + "/mix/pack.txt", "name = mix\n");
    writeFile(root + "/mix/colors.txt",
              "# comment line\n"
              "\n"
              "mode = type_color\n"
              "background = 8,12,20\n"
              "ball_shield = 1, 2, 3, 4\n"      // >=3 段取前 3 段
              "size_ball_shield = 5\n"           // 标量键原样
              "cyano_color_mode = colony\n"      // 字符串键原样
              "no_eq_line\n"                     // 无 = → 丢弃
              "bad = 1,2,x\n"                    // 非整数 → 整行丢弃
              "short = 1,2\n"                    // 段数不足 → 丢弃
              "junk = abc\n"                     // 非颜色 → 丢弃
              "nucleus = +7, -8, 9\n");          // 符号允许，归一化去 +
    std::string err;
    const auto p = agentsim::app::ResourcePackStore::parse(root, "mix", err);
    CHECK(err.empty());
    CHECK(p.colors.size() == 6u);
    CHECK(colorAt(p.colors, "mode") == "type_color");
    CHECK(colorAt(p.colors, "background") == "8,12,20");
    CHECK(colorAt(p.colors, "ball_shield") == "1,2,3");
    CHECK(colorAt(p.colors, "size_ball_shield") == "5");
    CHECK(colorAt(p.colors, "cyano_color_mode") == "colony");
    CHECK(p.colors.count("bad") == 0u);
    CHECK(p.colors.count("short") == 0u);
    CHECK(p.colors.count("junk") == 0u);
    CHECK(colorAt(p.colors, "nucleus") == "7,-8,9");
}

// 9) 内置默认调色板 7 键齐全；CSV/JSON 转义工具。
TEST(resourcepack_builtin_defaults_and_utils) {
    const auto d = agentsim::app::ResourcePackStore::builtInDefaults();
    CHECK(d.colors.size() == 7u);
    CHECK(colorAt(d.colors, "background") == "10,14,26");
    CHECK(colorAt(d.colors, "ball_shield") == "255,207,94");
    CHECK(colorAt(d.colors, "ball_worker") == "69,224,216");
    CHECK(colorAt(d.colors, "ball_scout") == "183,139,255");
    CHECK(colorAt(d.colors, "free_ball") == "200,200,210");
    CHECK(colorAt(d.colors, "nucleus") == "255,255,255");
    CHECK(colorAt(d.colors, "mode") == "type_color");

    const auto lst = agentsim::app::ResourcePackStore::parseEnabledList(" a , ,b,, c ");
    CHECK(lst.size() == 3u);
    CHECK(lst[0] == "a");
    CHECK(lst[1] == "b");
    CHECK(lst[2] == "c");

    const std::string in = std::string("x") + static_cast<char>(0x22) + "y" +
                           static_cast<char>(0x5C) + "z" + static_cast<char>(0x01);
    const std::string esc = agentsim::app::ResourcePackStore::jsonEscape(in);
    const std::string want = std::string("x") + "\\" + static_cast<char>(0x22) + "y\\" +
                             "\\" + "z" + "\\u0001";
    CHECK(esc == want);
}
