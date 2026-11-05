// agentsim - tests/unit/test_save.cpp（R5-C 存档系统单测）
// 构建脚本说明（同 test_cli.cpp 的既有先例）：tools/build.ps1 的单测目标只收集
// src/app 之外的库源，SaveStore/Timeline 不在 unit_tests.exe 的链接列表里。
// 直接把两个 .cpp 并入本翻译单元；若日后构建脚本把 src/app 纳入单测源集，
// 必须删除下面两行 #include，否则产生符号重复。
#include "app/SaveStore.cpp"
#include "app/Timeline.cpp"

#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sim/Simulation.h"

namespace fs = std::filesystem;

namespace {

// 单测进程的 cwd 是仓库根（check.ps1/build.ps1 都在根上运行）：
// 把 saves/ 指到 build/test_saves/，测完整个删掉。SaveStore 用相对路径
// "saves/<name>"，这里直接用目录重定向不可行——改为在每个用例前后清理
// saves/ 下的专属前缀目录，并保证用例名互不相同。
struct Scratch {
    explicit Scratch(const std::string& n) : name(n) {
        std::error_code ec;
        fs::remove_all("saves/" + name, ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all("saves/" + name, ec);
    }
    std::string name;
};

agentsim::SimConfig smallCfg() {
    agentsim::SimConfig c{};
    c.world.width = 128.0;
    c.world.height = 128.0;
    c.world.seed = 7u;
    c.world.maxTicks = 1000u;
    c.colony.nuclei = 4u;
    c.colony.maxNuclei = 20u;
    c.cyanoNodeCount = 6u;
    c.cyanoMaxNodes = 12u;
    return c;
}

} // namespace

// ================= 1) roundtrip：stateHash 逐位一致（最强断言）=================
TEST(save_roundtrip_state_hash_identical) {
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(137u));
    const std::uint64_t h0 = sim.stateHash();
    std::ostringstream os;
    CHECK(sim.serializeState(os));
    const std::string blob = os.str();
    CHECK(!blob.empty());

    agentsim::Simulation back(smallCfg());
    std::istringstream is(blob);
    std::string err;
    CHECK(back.deserializeState(is, err));
    CHECK(err.empty());
    CHECK(back.stateHash() == h0);
    // 存档头随 blob 一起流走：读侧必须精确收口（多字节/少字节都算格式漂移）。
    CHECK(is.peek() == std::istringstream::traits_type::eof());
}

// ================= 2) 读档后 auditOk() 立即成立 =================
TEST(save_deserialize_audit_ok_immediately) {
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(333u));
    CHECK(sim.auditOk());
    std::ostringstream os;
    CHECK(sim.serializeState(os));
    agentsim::Simulation back(smallCfg());
    std::istringstream is(os.str());
    std::string err;
    CHECK(back.deserializeState(is, err));
    // 任务卡红线：deserialize 后 auditOk() 必须立即成立。
    CHECK(back.auditOk());
    CHECK(back.tick() == 333u);
}

// ================= 3) 确定性红线：读档续跑 == 不存档直接跑 =================
TEST(save_resume_identical_to_uninterrupted_run) {
    agentsim::SimConfig cfg = smallCfg();
    // 基线：一口气跑到底。
    agentsim::Simulation ref(cfg);
    CHECK(ref.runTo(200u));

    // 实验组：跑 50 -> 存 -> 读 -> 续跑 150。
    agentsim::Simulation simA(cfg);
    CHECK(simA.runTo(50u));
    std::ostringstream os;
    CHECK(simA.serializeState(os));
    agentsim::Simulation simB(cfg);
    std::istringstream is(os.str());
    std::string err;
    CHECK(simB.deserializeState(is, err));
    CHECK(simB.runTo(200u));
    CHECK(simB.tick() == ref.tick());
    CHECK(simB.stateHash() == ref.stateHash());
    CHECK(simB.totalEnergy() == ref.totalEnergy());
    CHECK(simB.auditOk());
}

// ================= 4) rng 状态被保存（删掉这条的等价物 = 用例 3 必失败）=====
TEST(save_rng_state_roundtrips) {
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(77u));
    std::ostringstream os;
    CHECK(sim.serializeState(os));
    const std::string blob = os.str();
    // rng state/inc 字节进 blob（roundtrip 已证；这里直接证明不同 rng 位置的
    // blob 不同——rng 没被序列化的话两个位置会撞同一段输出）。
    agentsim::Simulation sim2(smallCfg());
    CHECK(sim2.runTo(78u));
    std::ostringstream os2;
    CHECK(sim2.serializeState(os2));
    CHECK(blob != os2.str());
    // 读档后必须能继续推进且审计成立（rng 状态有效性被行为层消费验证）。
    agentsim::Simulation back(smallCfg());
    std::istringstream is(blob);
    std::string err;
    CHECK(back.deserializeState(is, err));
    CHECK(back.runTo(back.tick() + 30u));
    CHECK(back.auditOk());
}

// ================= 5) 完整生态段（600 tick）+ 捕食/繁殖路径后 roundtrip =====
TEST(save_roundtrip_after_ecology_run) {
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(600u));
    const std::uint64_t h0 = sim.stateHash();
    std::ostringstream os;
    CHECK(sim.serializeState(os));
    agentsim::Simulation back(smallCfg());
    std::istringstream is(os.str());
    std::string err;
    CHECK(back.deserializeState(is, err));
    CHECK(back.stateHash() == h0);
    CHECK(back.auditOk());
    // 续跑 20 帧也不炸（蓝细菌分裂/产球、核捕食、自由球回收全路径已激活）。
    CHECK(back.runTo(back.tick() + 20u));
    CHECK(back.auditOk());
}

// ================= 6) 名字校验：全部拒绝路径 =================
TEST(save_name_validation_rejects) {
    std::string err;
    // 空
    CHECK(!agentsim::app::SaveStore::validName("", err));
    CHECK(err.find("empty") != std::string::npos);
    // 65 字符超长
    std::string longName(65, 'a');
    CHECK(!agentsim::app::SaveStore::validName(longName, err));
    CHECK(err.find("too long") != std::string::npos);
    // 路径分隔符与 Windows 非法字符
    for (const char* bad : {"a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b"}) {
        CHECK(!agentsim::app::SaveStore::validName(bad, err));
    }
    // ..（任意位置）
    CHECK(!agentsim::app::SaveStore::validName("..", err));
    CHECK(!agentsim::app::SaveStore::validName("a..b", err));
    CHECK(!agentsim::app::SaveStore::validName("../etc", err));
    CHECK(!agentsim::app::SaveStore::validName("..\\x", err));
    // 前后空格 / 结尾点
    CHECK(!agentsim::app::SaveStore::validName(" lead", err));
    CHECK(!agentsim::app::SaveStore::validName("trail ", err));
    CHECK(!agentsim::app::SaveStore::validName("end.", err));
    // 控制字符
    std::string ctrl = "a\x01b";
    CHECK(!agentsim::app::SaveStore::validName(ctrl, err));
    // Windows 保留设备名（任意大小写）
    CHECK(!agentsim::app::SaveStore::validName("CON", err));
    CHECK(!agentsim::app::SaveStore::validName("nul", err));
    CHECK(!agentsim::app::SaveStore::validName("Com1", err));
    // 合法名字必须通过
    CHECK(agentsim::app::SaveStore::validName("hello-world_42", err));
    CHECK(err.empty());
    // 非法名字必须一路被拒绝（save/load/remove/rename 全入口）
    agentsim::Simulation sim(smallCfg());
    agentsim::SimConfig cfg = smallCfg();
    std::string pn;
    CHECK(!agentsim::app::SaveStore::save("../evil", sim, cfg, "rule", err));
    CHECK(!agentsim::app::SaveStore::load("", sim, cfg, pn, err));
    CHECK(!agentsim::app::SaveStore::remove("a/b", err));
    CHECK(!agentsim::app::SaveStore::rename("x", "..y", err));
}

// ================= 7) save/load/list/remove 真实生效 =================
TEST(save_store_roundtrip_and_list) {
    Scratch scratch("unit_save_a");
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(120u));
    const std::uint64_t hashAtSave = sim.stateHash();
    std::string err;
    CHECK(agentsim::app::SaveStore::save("unit_save_a", sim, smallCfg(), "rule", err));
    CHECK(err.empty());
    CHECK(fs::exists("saves/unit_save_a/save.bin"));
    CHECK(fs::exists("saves/unit_save_a/meta.txt"));

    // list：能列出且字段正确
    std::vector<agentsim::app::SaveStore::Meta> all = agentsim::app::SaveStore::list();
    bool found = false;
    for (const auto& m : all) {
        if (m.name == "unit_save_a") {
            found = true;
            CHECK(m.frame == 120);
            CHECK(m.seed == 7u);
            CHECK(m.aliveNuclei == sim.nucleusCount());
            CHECK(m.savedAt.size() == 19u);  // "YYYY-MM-DD HH:MM:SS"
        }
    }
    CHECK(found);

    // load：完整恢复（cfg + 实体态 + hash）
    agentsim::SimConfig cfg{};
    std::string pn;
    CHECK(agentsim::app::SaveStore::loadHeader("unit_save_a", cfg, pn, err));
    CHECK(pn == "rule");
    CHECK(cfg.world.seed == 7u);
    CHECK(cfg.world.width == 128.0);
    CHECK(cfg.colony.nuclei == 4u);
    CHECK(cfg.cyanoNodeCount == 6u);
    agentsim::Simulation back(cfg);
    CHECK(agentsim::app::SaveStore::load("unit_save_a", back, cfg, pn, err));
    CHECK(err.empty());
    CHECK(back.stateHash() == hashAtSave);
    CHECK(back.tick() == 120u);
    CHECK(back.auditOk());

    // remove：真删
    CHECK(agentsim::app::SaveStore::remove("unit_save_a", err));
    CHECK(!fs::exists("saves/unit_save_a"));
    all = agentsim::app::SaveStore::list();
    found = false;
    for (const auto& m : all) {
        if (m.name == "unit_save_a") found = true;
    }
    CHECK(!found);
    // remove 不存在的存档必须报错
    CHECK(!agentsim::app::SaveStore::remove("unit_save_a", err));
    CHECK(err.find("not found") != std::string::npos);
}

// ================= 8) rename 真实生效 =================
TEST(save_rename_moves_dir_and_meta) {
    Scratch s1("unit_ren_old");
    Scratch s2("unit_ren_new");
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(40u));
    std::string err;
    CHECK(agentsim::app::SaveStore::save("unit_ren_old", sim, smallCfg(), "rule", err));
    CHECK(fs::exists("saves/unit_ren_old/meta.txt"));
    CHECK(agentsim::app::SaveStore::rename("unit_ren_old", "unit_ren_new", err));
    CHECK(!fs::exists("saves/unit_ren_old"));
    CHECK(fs::exists("saves/unit_ren_new/save.bin"));
    // meta.txt 的 name= 字段同步改写
    std::ifstream meta("saves/unit_ren_new/meta.txt");
    CHECK(meta.is_open());
    std::string line;
    bool nameOk = false;
    while (std::getline(meta, line)) {
        if (line.rfind("name=", 0) == 0 && line == "name=unit_ren_new") nameOk = true;
    }
    meta.close();
    CHECK(nameOk);
    // rename 后读档仍完整可用
    agentsim::SimConfig cfg{};
    std::string pn;
    agentsim::Simulation back(cfg);
    CHECK(agentsim::app::SaveStore::load("unit_ren_new", back, cfg, pn, err));
    CHECK(back.stateHash() == sim.stateHash());
    // 重名拒绝 / 源不存在拒绝
    CHECK(!agentsim::app::SaveStore::rename("unit_ren_new", "unit_ren_new", err));
    CHECK(err.find("already exists") != std::string::npos);
    CHECK(!agentsim::app::SaveStore::rename("unit_ren_old", "whatever", err));
    CHECK(err.find("not found") != std::string::npos);
}

// ================= 9) 版本不符报错 =================
TEST(save_version_mismatch_rejected) {
    Scratch scratch("unit_ver");
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(10u));
    std::string err;
    CHECK(agentsim::app::SaveStore::save("unit_ver", sim, smallCfg(), "rule", err));
    // 篡改 version 字段（偏移 4 的 u32）为 99。
    {
        std::fstream f("saves/unit_ver/save.bin", std::ios::in | std::ios::out | std::ios::binary);
        CHECK(f.is_open());
        f.seekp(4, std::ios::beg);
        const unsigned char v99[4] = {99u, 0u, 0u, 0u};
        f.write(reinterpret_cast<const char*>(v99), 4);
        f.close();
    }
    agentsim::SimConfig cfg{};
    std::string pn;
    CHECK(!agentsim::app::SaveStore::loadHeader("unit_ver", cfg, pn, err));
    CHECK(err.find("version") != std::string::npos);
    agentsim::Simulation back(cfg);
    CHECK(!agentsim::app::SaveStore::load("unit_ver", back, cfg, pn, err));
    CHECK(err.find("version") != std::string::npos);
}

// ================= 10) magic 损坏报错 =================
TEST(save_bad_magic_rejected) {
    Scratch scratch("unit_magic");
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(5u));
    std::string err;
    CHECK(agentsim::app::SaveStore::save("unit_magic", sim, smallCfg(), "rule", err));
    {
        std::fstream f("saves/unit_magic/save.bin", std::ios::in | std::ios::out | std::ios::binary);
        CHECK(f.is_open());
        f.seekp(0, std::ios::beg);
        const unsigned char junk[4] = {'X', 'Y', 'Z', 'Z'};
        f.write(reinterpret_cast<const char*>(junk), 4);
        f.close();
    }
    agentsim::SimConfig cfg{};
    std::string pn;
    CHECK(!agentsim::app::SaveStore::loadHeader("unit_magic", cfg, pn, err));
    CHECK(err.find("magic") != std::string::npos);
}

// ================= 11) 截断流/尾随垃圾被拒绝 =================
TEST(save_truncated_and_trailing_rejected) {
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(64u));
    std::ostringstream os;
    CHECK(sim.serializeState(os));
    const std::string blob = os.str();

    // 截断一半：必须报错且不崩。
    agentsim::Simulation t(smallCfg());
    std::istringstream is1(blob.substr(0, blob.size() / 2));
    std::string err;
    CHECK(!t.deserializeState(is1, err));
    CHECK(!err.empty());

    // 尾随垃圾：必须报错（格式从严）。
    agentsim::Simulation t2(smallCfg());
    std::istringstream is2(blob + std::string("\x00\x01\x02", 3));
    CHECK(!t2.deserializeState(is2, err));
    CHECK(err.find("trailing") != std::string::npos);
}

// ================= 12) 默认参数下的存活核数 > 0（保底健全性）===============
TEST(save_baseline_world_alive) {
    agentsim::Simulation sim(agentsim::SimConfig{});
    CHECK(sim.runTo(200u));
    CHECK(sim.auditOk());
    CHECK(sim.nucleusCount() > 0u);
}

// ================= 13) Timeline：检查点写入 + seek 回放逐位一致 ============
TEST(timeline_maybe_and_seek) {
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation sim(cfg);
    agentsim::app::Timeline tl(cfg);
    tl.setCheckpointInterval(10u);
    for (std::uint64_t t = 1u; t <= 50u; ++t) {
        sim.step();
        CHECK(sim.auditOk());
        tl.maybeCheckpoint(sim, sim.tick(), sim.stateHash());
    }
    CHECK(tl.size() == 5u);
    CHECK(tl.frames() == 50u);
    // 回到 30（精确命中检查点）：seek 后的世界必须与"一口气跑到 30"逐位一致
    //（检查点 payload 是全量实体态，含 tick_/rng_；stateHash 含 tick_）。
    std::string err;
    CHECK(tl.seek(30u, sim, err));
    CHECK(sim.tick() == 30u);
    agentsim::Simulation ref(cfg);
    CHECK(ref.runTo(30u));
    CHECK(sim.stateHash() == ref.stateHash());
    // seek 回 30 后续跑到 50：沿确定性轨迹重新前进（合法且审计成立）。
    CHECK(sim.runTo(50u));
    CHECK(sim.auditOk());
}

// ================= 14) Timeline：任意帧 seek = 回到最近的过去检查点 =========
TEST(timeline_seek_snaps_to_nearest_past) {
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation sim(cfg);
    agentsim::app::Timeline tl(cfg);
    tl.setCheckpointInterval(10u);
    for (std::uint64_t t = 1u; t <= 35u; ++t) {
        sim.step();
        tl.maybeCheckpoint(sim, sim.tick(), sim.stateHash());
    }
    CHECK(tl.size() == 3u);
    // 32 > 30（最近检查点）：应回到 30。
    std::string err;
    CHECK(tl.seek(32u, sim, err));
    CHECK(sim.tick() == 30u);
    agentsim::Simulation ref(cfg);
    CHECK(ref.runTo(30u));
    CHECK(sim.stateHash() == ref.stateHash());
    // 早于首检查点：拒绝
    CHECK(!tl.seek(5u, sim, err));
    CHECK(!err.empty());
}

// ================= 15) Timeline：抽稀保首尾（塞 2500 个）====================
TEST(timeline_thinning_keeps_ends) {
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation sim(cfg);
    agentsim::app::Timeline tl(cfg);
    tl.setCheckpointInterval(1u);
    for (std::uint64_t t = 1u; t <= 2500u; ++t) {
        sim.step();
        tl.maybeCheckpoint(sim, sim.tick(), sim.stateHash());
    }
    // 抽稀结果（确定性，逐位可复现）：t=2001 时 2001>2000 触发隔一去一
    //（保留下标 0,2,4,... = 1001 个，保首），t=2002..2500 再各 push 一个 = 1500 个。
    // 硬顶 <= 2000 恒成立（任务卡验收口径）。
    CHECK(tl.size() == 1500u);
    CHECK(tl.size() <= agentsim::app::Timeline::kMaxCheckpoints);
    // 首尾保留（任务卡硬要求）
    const agentsim::app::Timeline::Checkpoint& first = *tl.begin();
    const agentsim::app::Timeline::Checkpoint& last = *(tl.end() - 1);
    CHECK(first.frame == 1u);
    CHECK(last.frame == 2500u);
    // 抽稀后仍可 seek 回首帧并逐位还原：与"恰好跑 1 帧的参考世界"比对
    //（检查点在 step() 之后捕获，帧号 = 捕获时 tick_）。
    std::string err;
    CHECK(tl.seek(1u, sim, err));
    CHECK(sim.tick() == 1u);
    {
        agentsim::Simulation ref(cfg);
        ref.step();
        CHECK(sim.stateHash() == ref.stateHash());
    }
    // 尾帧也可恢复
    CHECK(tl.seek(2500u, sim, err));
    CHECK(sim.tick() == 2500u);
}

// ================= 16) Timeline：branch 保留实体状态、只换运行参数 ==========
TEST(timeline_branch_preserves_entities) {
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation sim(cfg);
    CHECK(sim.runTo(30u));
    const std::uint64_t hashBefore = sim.stateHash();
    const double energyBefore = sim.totalEnergy();

    // branch：换阳光基线 + 策略名；几何/种子强制保留。
    agentsim::SimConfig newCfg = cfg;
    newCfg.sun.baseLevel = 0.9;
    newCfg.policy.name = "thermal";
    newCfg.world.width = 999.0;   // 试图改几何：必须被拒绝（保留旧值）
    newCfg.world.seed = 12345u;   // 试图改种子：必须被拒绝
    agentsim::app::Timeline tl(cfg);
    tl.branch(newCfg);
    CHECK(tl.cfg().sun.baseLevel == 0.9);
    CHECK(tl.cfg().policy.name == "thermal");
    CHECK(tl.cfg().world.width == cfg.world.width);
    CHECK(tl.cfg().world.seed == cfg.world.seed);
    CHECK(tl.branched());
    CHECK(tl.size() == 0u);      // 分支清空检查点
    CHECK(tl.frames() == 0u);
    CHECK(tl.checkpointInterval() == 200u);  // 新 Timeline 默认间隔

    // branch() 本身不触碰 sim：实体状态（位置/能量/基因）原样保留。
    CHECK(sim.tick() == 30u);          // 时钟未动（restartClock 由调用方显式）
    CHECK(sim.stateHash() == hashBefore);
    CHECK(sim.totalEnergy() == energyBefore);
    CHECK(sim.nuclei().size() > 0u);
    // 实体 genome 未被重掷（genome 是构造/遗传产物，branch 不碰实体）：
    // 用 seed=7 的初始种群里 hue 的总体特征做粗校验——branch 前后逐位一致已由
    // stateHash 断言覆盖（hash 混入 genome 影响的一切物理量）。

    // 调用方显式完成分支三步走：applyConfig + restartClock + 续跑。
    sim.applyConfig(tl.cfg());
    sim.restartClock();
    CHECK(sim.tick() == 0u);
    CHECK(sim.runTo(10u));
    CHECK(sim.auditOk());
    CHECK(sim.nucleusCount() > 0u);
}

// ================= 17) Timeline：持久化往返 =================
TEST(timeline_export_import_roundtrip) {
    Scratch scratch("unit_tl_bin");
    // exportTo 不负责建父目录（SaveStore 目录语义：save() 建目录，timeline.bin
    // 是"已存在存档目录里的第三件套"）。测试自行建目录，保证导出可写。
    std::error_code mkEc;
    std::filesystem::create_directories("saves/unit_tl_bin", mkEc);
    CHECK(!mkEc);
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation sim(cfg);
    agentsim::app::Timeline tl(cfg);
    tl.setCheckpointInterval(20u);
    for (std::uint64_t t = 1u; t <= 100u; ++t) {
        sim.step();
        tl.maybeCheckpoint(sim, sim.tick(), sim.stateHash());
    }
    CHECK(tl.size() == 5u);
    std::string err;
    CHECK(tl.exportTo("saves/unit_tl_bin/timeline.bin", err));
    CHECK(fs::exists("saves/unit_tl_bin/timeline.bin"));

    agentsim::app::Timeline tl2(cfg);
    CHECK(tl2.importFrom("saves/unit_tl_bin/timeline.bin", err));
    CHECK(tl2.size() == 5u);
    CHECK(tl2.frames() == 100u);
    CHECK(tl2.checkpointInterval() == 20u);
    CHECK(tl2.cfg().world.seed == cfg.world.seed);
    // payload 逐位一致 + 可用于真实 seek（size==0 时禁止解引用 begin()）
    CHECK(tl2.size() == tl.size());
    if (tl2.size() > 0u) {
        CHECK(tl2.begin()->payload == tl.begin()->payload);
    }
    agentsim::Simulation sim2(cfg);
    sim2.step();
    CHECK(tl2.seek(20u, sim2, err));
    CHECK(sim2.tick() == 20u);
    // 版本不符
    {
        std::fstream f("saves/unit_tl_bin/timeline.bin",
                       std::ios::in | std::ios::out | std::ios::binary);
        CHECK(f.is_open());
        f.seekp(4, std::ios::beg);
        const unsigned char v99[4] = {99u, 0u, 0u, 0u};
        f.write(reinterpret_cast<const char*>(v99), 4);
        f.close();
    }
    agentsim::app::Timeline tl3(cfg);
    CHECK(!tl3.importFrom("saves/unit_tl_bin/timeline.bin", err));
    CHECK(err.find("version") != std::string::npos);
}

// ================= 18) checkpoint-interval=0 关闭 =================
TEST(timeline_interval_zero_disables) {
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation sim(cfg);
    agentsim::app::Timeline tl(cfg);
    tl.setCheckpointInterval(0u);
    for (std::uint64_t t = 1u; t <= 25u; ++t) {
        sim.step();
        tl.maybeCheckpoint(sim, sim.tick(), sim.stateHash());
    }
    CHECK(tl.size() == 0u);
}

// ================= 19) 两次不同 rng 位置的存档 blob 不同（防“空状态快照”）===
TEST(save_blob_differs_by_rng_position) {
    agentsim::SimConfig cfg = smallCfg();
    agentsim::Simulation a(cfg);
    CHECK(a.runTo(40u));
    agentsim::Simulation b(cfg);
    CHECK(b.runTo(41u));
    std::ostringstream oa;
    std::ostringstream ob;
    CHECK(a.serializeState(oa));
    CHECK(b.serializeState(ob));
    CHECK(oa.str() != ob.str());
}

// ================= 20) 存档不携带 cfg 之外的隐藏耦合：双存档互不污染 =========
TEST(save_two_saves_independent) {
    Scratch s1("unit_ind_1");
    Scratch s2("unit_ind_2");
    agentsim::SimConfig cfgA = smallCfg();
    cfgA.world.seed = 7u;
    agentsim::SimConfig cfgB = smallCfg();
    cfgB.world.seed = 99u;
    cfgB.colony.nuclei = 6u;
    agentsim::Simulation simA(cfgA);
    agentsim::Simulation simB(cfgB);
    CHECK(simA.runTo(25u));
    CHECK(simB.runTo(60u));
    std::string err;
    CHECK(agentsim::app::SaveStore::save("unit_ind_1", simA, cfgA, "rule", err));
    CHECK(agentsim::app::SaveStore::save("unit_ind_2", simB, cfgB, "rule", err));

    agentsim::SimConfig cfg1{};
    std::string pn1;
    agentsim::Simulation back1(cfg1);
    CHECK(agentsim::app::SaveStore::load("unit_ind_1", back1, cfg1, pn1, err));
    CHECK(cfg1.world.seed == 7u);
    CHECK(back1.stateHash() == simA.stateHash());

    agentsim::SimConfig cfg2{};
    std::string pn2;
    agentsim::Simulation back2(cfg2);
    CHECK(agentsim::app::SaveStore::load("unit_ind_2", back2, cfg2, pn2, err));
    CHECK(cfg2.world.seed == 99u);
    CHECK(cfg2.colony.nuclei == 6u);
    CHECK(back2.stateHash() == simB.stateHash());
    // 互不污染
    CHECK(back1.stateHash() != back2.stateHash());
}

// ================= 21) SaveStore 拒绝空/未知 policy 名（存档面）=============
TEST(save_unknown_policy_rejected_on_save) {
    Scratch scratch("unit_policy");
    agentsim::Simulation sim(smallCfg());
    CHECK(sim.runTo(10u));
    std::string err;
    CHECK(!agentsim::app::SaveStore::save("unit_policy", sim, smallCfg(), "nonsense", err));
    CHECK(err.find("unknown policy") != std::string::npos);
    CHECK(!fs::exists("saves/unit_policy/save.bin"));
}
