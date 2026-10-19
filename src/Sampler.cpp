#include "Sampler.h"
#include <fstream>
#include <iomanip>
#include <sstream>

Sampler::Sampler(std::string dir) : dir_(std::move(dir)) {}

namespace {
std::string padFrame(int frame) {
    std::ostringstream ss;
    ss << std::setw(6) << std::setfill('0') << frame;
    return ss.str();
}

const char* trendHeader() {
    return "frame,nucleus_id,energy,affinity0,affinity1,affinity2,orbitRadius0,orbitRadius1,orbitRadius2,"
           "absorbPreference0,absorbPreference1,absorbPreference2,repelStrength0,repelStrength1,"
           "repelStrength2,attackRange,attackStrength,avoidRange,avoidStrength,maxSpeed,"
           "energyThreshold,mutationRate,cyanoPreference,cyanoDigestion,maxAccel";
}

void writeTrendRow(std::ostream& os, int frame, const Nucleus& n) {
    const NucleusParams& p = n.params;
    os << std::fixed << std::setprecision(6);
    os << frame << ',' << n.id << ',' << n.energy << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.affinity[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.orbitRadius[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.absorbPreference[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT - 1; ++i) os << p.repelStrength[i] << ',';
    os << p.repelStrength[BALL_TYPE_COUNT - 1] << ',';
    os << p.attackRange << ',' << p.attackStrength << ',' << p.avoidRange << ','
       << p.avoidStrength << ',' << p.maxSpeed << ',' << p.energyThreshold << ','
       << p.mutationRate << ','
       // ---- 蓝细菌相关遗传列（**追加在最后**，旧 22 列顺序与含义不变）----
       << p.cyanoPreference << ',' << p.cyanoDigestion << ',' << p.maxAccel << '\n';
}

const char* nucleiHeader() {
    return "x,y,energy,affinity0,affinity1,affinity2,orbitRadius0,orbitRadius1,orbitRadius2,"
           "absorbPreference0,absorbPreference1,absorbPreference2,repelStrength0,repelStrength1,"
           "repelStrength2,attackRange,attackStrength,avoidRange,avoidStrength,maxSpeed,"
           "energyThreshold,mutationRate,cyanoPreference,cyanoDigestion,maxAccel";
}

void writeNucleusRow(std::ostream& os, const Nucleus& n) {
    const NucleusParams& p = n.params;
    os << std::fixed << std::setprecision(6);
    os << n.pos.x << ',' << n.pos.y << ',' << n.energy << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.affinity[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.orbitRadius[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT; ++i) os << p.absorbPreference[i] << ',';
    for (int i = 0; i < BALL_TYPE_COUNT - 1; ++i) os << p.repelStrength[i] << ',';
    os << p.repelStrength[BALL_TYPE_COUNT - 1] << ',';
    os << p.attackRange << ',' << p.attackStrength << ',' << p.avoidRange << ','
       << p.avoidStrength << ',' << p.maxSpeed << ',' << p.energyThreshold << ','
       << p.mutationRate << ','
       // ---- 蓝细菌相关遗传列（**追加在最后**，旧 22 列顺序与含义不变）----
       << p.cyanoPreference << ',' << p.cyanoDigestion << ',' << p.maxAccel << '\n';
}

// ---- 蓝细菌节点采样（cyano_<frame>.csv）----
// 列语义：
//   link_count = 该节点**实际**的连接数（只计两端都存活的 links_ 条目）。
//   alive      = 仅输出存活节点，故恒为 1（保留列以便与快照 24B 记录口径对照）。
//   colony_id  = 所属聚落；age = 存活帧数。
//
// ⚠️ 为什么不用 CyanoNode::linkCount 缓存：捕食发生在 World::step() 的 absorbBalls()
//    阶段（把节点标为 alive=0），而「移除死节点 + 重映射 links_ + 重算 linkCount 缓存」
//    发生在**下一次 step() 的开头**（冻结契约 A5 红线 1：consumeNode 只标死不立即移除，
//    以免 World 手上的下标失效）。采样恰好落在这一帧内窗口里 → 此刻缓存仍计入
//    「指向刚被吃掉节点」的连接，而导出的连接语义里这些连接并不存在。
//    因此这里按 links_ **现场统计**存活度数，保证 link_count 列与连接语义严格一致
//    （也与快照头 cyanoLinkCount = aliveLinkCount 的口径同源）。
std::vector<uint16_t> aliveDegrees(const Cyanobacteria& cy) {
    const std::vector<CyanoNode>& ns = cy.nodes();
    std::vector<uint16_t> deg(ns.size(), 0);
    for (size_t li = 0; li < cy.links().size(); ++li) {
        const CyanoLink& l = cy.links()[li];
        if (l.a >= ns.size() || l.b >= ns.size()) continue;
        if (ns[l.a].alive == 0 || ns[l.b].alive == 0) continue;  // 只计两端都存活
        if (deg[l.a] < 0xFFFFu) ++deg[l.a];
        if (deg[l.b] < 0xFFFFu) ++deg[l.b];
    }
    return deg;
}

const char* cyanoHeader() {
    return "x,y,energy,link_count,alive,colony_id,age";
}

void writeCyanoRow(std::ostream& os, const CyanoNode& n, uint16_t linkCount) {
    os << std::fixed << std::setprecision(6);
    os << n.pos.x << ',' << n.pos.y << ',' << n.energy << ','
       << linkCount << ',' << (int)n.alive << ',' << n.colonyId << ',' << n.age << '\n';
}
}  // namespace

void Sampler::sample(const World& world) {
    std::string padded = padFrame(world.frame());

    {
        std::ofstream f(dir_ + "/balls_" + padded + ".csv");
        f << "x,y,type,ownerId\n";
        f << std::fixed << std::setprecision(6);
        for (const Ball& b : world.balls()) {
            f << b.pos.x << ',' << b.pos.y << ','
              << static_cast<int>(b.type) << ',' << b.ownerId << '\n';
        }
    }

    {
        std::ofstream f(dir_ + "/nuclei_" + padded + ".csv");
        f << nucleiHeader() << '\n';
        for (const Nucleus& n : world.nuclei()) {
            if (!n.alive) continue;
            writeNucleusRow(f, n);
        }
    }

    // 蓝细菌节点采样：仅在开启（群落非空）时输出，关闭态**零文件、零开销**。
    if (!world.cyano().empty()) {
        std::ofstream f(dir_ + "/cyano_" + padded + ".csv");
        f << cyanoHeader() << '\n';
        // 现场统计存活度数（见 aliveDegrees 注释）：避免读到「指向本帧刚被吃掉节点」的陈旧缓存。
        const std::vector<uint16_t> deg = aliveDegrees(world.cyano());
        const std::vector<CyanoNode>& ns = world.cyano().nodes();
        for (size_t i = 0; i < ns.size(); ++i) {
            if (!ns[i].alive) continue;  // 只输出存活节点（alive 列恒为 1）
            writeCyanoRow(f, ns[i], (i < deg.size()) ? deg[i] : 0);
        }
    }
}

void Sampler::writeSurvivors(const World& world) {
    std::ofstream f(dir_ + "/survivors.csv");
    f << nucleiHeader() << '\n';
    for (const Nucleus& n : world.nuclei()) {
        if (n.alive) writeNucleusRow(f, n);
    }
}

bool Sampler::openTrend(const std::string& path) {
    trendOut_.open(path);
    if (!trendOut_.is_open()) {
        return false;
    }
    trendOut_ << trendHeader() << '\n';
    return true;
}

void Sampler::sampleTrend(const World& world) {
    if (!trendOut_.is_open()) return;
    int frame = world.frame();
    for (const Nucleus& n : world.nuclei()) {
        if (n.alive) writeTrendRow(trendOut_, frame, n);
    }
}

void Sampler::closeTrend() {
    if (trendOut_.is_open()) trendOut_.close();
}
