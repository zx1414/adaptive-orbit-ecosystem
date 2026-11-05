// agentsim - app/SaveStore.cpp（R5-C 存档系统实现）
// save.bin 二进制布局（小端）：
//   u32 magic=0x41534153 ("ASAV") ; u32 version=1
//   u8 seedIsDefault ; u8[3] pad                       // 存档时刻上界快照
//   -- cfg 块（savedetail::writeCfgBlock，save.bin 与 timeline.bin 共用同一节）--
//   -- Simulation::serializeState 全量实体态（布局见 Simulation.cpp 注释）--
// 全部标量小端、无对齐填充；f64 按 IEEE-754 位原样。
#include "app/SaveStore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <iterator>
#include <ostream>
#include <sstream>

#include "sim/Simulation.h"

namespace agentsim {
namespace app {
namespace {

constexpr std::uint32_t kMagic = 0x41534153u;   // "ASAV"
constexpr std::uint32_t kVersion = 1u;
constexpr std::size_t kNameLimit = 64;
constexpr std::size_t kPolicyNameLimit = 64;

std::string savesRoot() { return std::string("saves"); }
std::string saveDir(const std::string& name) { return savesRoot() + "/" + name; }

// ---- 小端 IO（与 Simulation.cpp 的序列化通道同一约定；此处是存档包装层）----
void wU32(std::ostream& os, std::uint32_t v) {
    unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                          static_cast<unsigned char>((v >> 8) & 0xFFu),
                          static_cast<unsigned char>((v >> 16) & 0xFFu),
                          static_cast<unsigned char>((v >> 24) & 0xFFu)};
    os.write(reinterpret_cast<const char*>(b), 4);
}

void wU64(std::ostream& os, std::uint64_t v) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (i * 8)) & 0xFFu);
    os.write(reinterpret_cast<const char*>(b), 8);
}

void wF64(std::ostream& os, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    wU64(os, bits);
}

void wBytes(std::ostream& os, const void* p, std::size_t n) {
    os.write(static_cast<const char*>(p), static_cast<std::streamsize>(n));
}

bool rBytes(std::istream& is, void* p, std::size_t n) {
    is.read(static_cast<char*>(p), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(is.gcount()) == n;
}

bool rU32(std::istream& is, std::uint32_t& v) {
    unsigned char b[4];
    if (!rBytes(is, b, 4)) return false;
    v = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

bool rU64(std::istream& is, std::uint64_t& v) {
    unsigned char b[8];
    if (!rBytes(is, b, 8)) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[i]) << (i * 8);
    return true;
}

bool rF64(std::istream& is, double& v) {
    std::uint64_t bits = 0;
    if (!rU64(is, bits)) return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
}

std::string metaEscape(const std::string& s) {
    std::string r;
    for (const char c : s) {
        if (c == '\\' || c == '\n' || c == '\r') r += '\\';
        r += c;
    }
    return r;
}

std::string nowTimestamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

bool isKnownPolicy(const std::string& name) {
    static const char* const kKnown[] = {"rule", "stub", "perceptron", "thermal",
                                         "hysteresis", "foragebank", "bandit",
                                         "riskaverse"};
    return std::any_of(std::begin(kKnown), std::end(kKnown),
                       [&](const char* k) { return name == k; });
}

void applyPolicyName(SimConfig& cfg, const std::string& policyName) {
    cfg.policy.name = policyName;
    if (policyName == "perceptron") cfg.policy.usePerceptron = true;
    if (policyName == "thermal") cfg.policy.useThermal = true;
    cfg.policy.mutation = (policyName == "rule") ? 0.05 : 0.0;
}

} // namespace

// ---- savedetail：cfg 块（save.bin 与 timeline.bin 共用）----
namespace savedetail {

bool writeCfgBlock(std::ostream& os, const SimConfig& cfg,
                   const std::string& policyName, std::string& err) {
    err.clear();
    if (policyName.size() > kPolicyNameLimit) {
        err = "policy name too long";
        return false;
    }
    if (!policyName.empty() && !isKnownPolicy(policyName)) {
        err = "unknown policy name: " + policyName;
        return false;
    }
    wF64(os, cfg.world.width);
    wF64(os, cfg.world.height);
    wU64(os, cfg.world.seed);
    wU64(os, cfg.world.maxTicks);
    wU64(os, cfg.colony.nuclei);
    wU64(os, cfg.colony.maxNuclei);
    wF64(os, cfg.colony.mutationCoefficient);
    wU64(os, cfg.cyanoNodeCount);
    wU64(os, cfg.cyanoMaxNodes);
    // SunConfig
    wF64(os, cfg.sun.baseLevel);
    wF64(os, cfg.sun.dayAmplitude);
    wF64(os, cfg.sun.dayPeriodFrames);
    wF64(os, cfg.sun.verticalGradient);
    wF64(os, cfg.sun.noiseAmplitude);
    wF64(os, cfg.sun.noiseFrequency);
    wF64(os, cfg.sun.noiseSpeed);
    // PolicyConfig
    wF64(os, cfg.policy.senseRadius);
    wF64(os, cfg.policy.damping);
    wF64(os, cfg.policy.mutation);
    const unsigned char pnLen = static_cast<unsigned char>(policyName.size());
    os.put(static_cast<char>(pnLen));
    wBytes(os, policyName.data(), pnLen);
    wU64(os, cfg.policy.perceptronSeed);
    wF64(os, cfg.policy.perceptronScale);
    wF64(os, cfg.policy.thermalPreferredTemp);
    wF64(os, cfg.policy.thermalWThermal);
    wF64(os, cfg.policy.thermalWCaution);
    // GenomeRanges（字段序 = Genome.h 声明序）
    const GenomeRanges& gr = cfg.colony.genomeRanges;
    const double* const grVals[] = {
        &gr.affinityInitMin, &gr.affinityInitMax, &gr.affinityClampMin, &gr.affinityClampMax,
        &gr.orbitRadiusInitMin, &gr.orbitRadiusInitMax, &gr.orbitRadiusClampMin, &gr.orbitRadiusClampMax,
        &gr.absorbPreferenceInitMin, &gr.absorbPreferenceInitMax, &gr.absorbPreferenceClampMin, &gr.absorbPreferenceClampMax,
        &gr.repelStrengthInitMin, &gr.repelStrengthInitMax, &gr.repelStrengthClampMin, &gr.repelStrengthClampMax,
        &gr.maxSpeedInitMin, &gr.maxSpeedInitMax, &gr.maxSpeedClampMin, &gr.maxSpeedClampMax,
        &gr.maxAccelInitMin, &gr.maxAccelInitMax, &gr.maxAccelClampMin, &gr.maxAccelClampMax,
        &gr.energyThresholdInitMin, &gr.energyThresholdInitMax, &gr.energyThresholdClampMin, &gr.energyThresholdClampMax,
        &gr.mutationRateInitMin, &gr.mutationRateInitMax, &gr.mutationRateClampMin, &gr.mutationRateClampMax,
        &gr.cyanoPreferenceInitMin, &gr.cyanoPreferenceInitMax, &gr.cyanoPreferenceClampMin, &gr.cyanoPreferenceClampMax,
        &gr.cyanoDigestionInitMin, &gr.cyanoDigestionInitMax, &gr.cyanoDigestionClampMin, &gr.cyanoDigestionClampMax,
        &gr.nucleusSightRadiusInitMin, &gr.nucleusSightRadiusInitMax, &gr.nucleusSightRadiusClampMin, &gr.nucleusSightRadiusClampMax,
        &gr.nucleusPersonalSpaceInitMin, &gr.nucleusPersonalSpaceInitMax, &gr.nucleusPersonalSpaceClampMin, &gr.nucleusPersonalSpaceClampMax,
        &gr.nucleusRepelStrengthInitMin, &gr.nucleusRepelStrengthInitMax, &gr.nucleusRepelStrengthClampMin, &gr.nucleusRepelStrengthClampMax,
        &gr.nucleusCohesionInitMin, &gr.nucleusCohesionInitMax, &gr.nucleusCohesionClampMin, &gr.nucleusCohesionClampMax,
        &gr.forceRangeInitMin, &gr.forceRangeInitMax, &gr.forceRangeClampMin, &gr.forceRangeClampMax,
        &gr.forceCostKInitMin, &gr.forceCostKInitMax, &gr.forceCostKClampMin, &gr.forceCostKClampMax,
        &gr.hueInitMin, &gr.hueInitMax, &gr.hueJitterSigma};
    for (const double* p : grVals) wF64(os, *p);
    // policyName（存档面权威重复：策略名属于世界，不只是 cfg 字段）
    const unsigned char pn2 = static_cast<unsigned char>(policyName.size());
    os.put(static_cast<char>(pn2));
    wBytes(os, policyName.data(), pn2);
    return os.good();
}

bool readCfgBlock(std::istream& is, SimConfig& cfg,
                  std::string& policyName, std::string& err) {
    err.clear();
    double width = 0.0, height = 0.0;
    std::uint64_t seed = 0, maxTicks = 0, nuclei = 0, maxNuclei = 0;
    double mutationCoefficient = 0.0;
    std::uint64_t cyanoNodeCount = 0, cyanoMaxNodes = 0;
    if (!rF64(is, width) || !rF64(is, height) || !rU64(is, seed) ||
        !rU64(is, maxTicks) || !rU64(is, nuclei) || !rU64(is, maxNuclei) ||
        !rF64(is, mutationCoefficient) || !rU64(is, cyanoNodeCount) ||
        !rU64(is, cyanoMaxNodes)) {
        err = "cannot read world config"; return false;
    }
    if (!(width > 0.0) || !(height > 0.0)) {
        err = "corrupt save: non-positive world size"; return false;
    }
    if (nuclei > (1u << 20) || maxNuclei > (1u << 20)) {
        err = "corrupt save: absurd colony size"; return false;
    }
    cfg.world.width = width;
    cfg.world.height = height;
    cfg.world.seed = seed;
    cfg.world.maxTicks = maxTicks;
    cfg.colony.nuclei = static_cast<std::size_t>(nuclei);
    cfg.colony.maxNuclei = static_cast<std::size_t>(maxNuclei);
    cfg.colony.mutationCoefficient = mutationCoefficient;
    cfg.cyanoNodeCount = static_cast<std::size_t>(cyanoNodeCount);
    cfg.cyanoMaxNodes = static_cast<std::size_t>(cyanoMaxNodes);
    double sb = 0.0, sa = 0.0, sp = 0.0, svg = 0.0, sna = 0.0, snf = 0.0, sns = 0.0;
    if (!rF64(is, sb) || !rF64(is, sa) || !rF64(is, sp) || !rF64(is, svg) ||
        !rF64(is, sna) || !rF64(is, snf) || !rF64(is, sns)) {
        err = "cannot read sun config"; return false;
    }
    cfg.sun.baseLevel = sb;
    cfg.sun.dayAmplitude = sa;
    cfg.sun.dayPeriodFrames = sp;
    cfg.sun.verticalGradient = svg;
    cfg.sun.noiseAmplitude = sna;
    cfg.sun.noiseFrequency = snf;
    cfg.sun.noiseSpeed = sns;
    double sr = 0.0, dm = 0.0, mu = 0.0;
    unsigned char nLen = 0;
    if (!rF64(is, sr) || !rF64(is, dm) || !rF64(is, mu) || !rBytes(is, &nLen, 1) ||
        nLen > kPolicyNameLimit) {
        err = "cannot read policy config"; return false;
    }
    std::string storedName(nLen, '\0');
    if (nLen > 0 && !rBytes(is, &storedName[0], nLen)) {
        err = "cannot read policy name"; return false;
    }
    cfg.policy.senseRadius = sr;
    cfg.policy.damping = dm;
    cfg.policy.mutation = mu;
    std::uint64_t pseed = 0;
    double pscale = 0.0, tpref = 0.0, twt = 0.0, twc = 0.0;
    if (!rU64(is, pseed) || !rF64(is, pscale) || !rF64(is, tpref) ||
        !rF64(is, twt) || !rF64(is, twc)) {
        err = "cannot read policy config (2)"; return false;
    }
    cfg.policy.perceptronSeed = pseed;
    cfg.policy.perceptronScale = pscale;
    cfg.policy.thermalPreferredTemp = tpref;
    cfg.policy.thermalWThermal = twt;
    cfg.policy.thermalWCaution = twc;
    GenomeRanges& gr = cfg.colony.genomeRanges;
    double* const grTargets[] = {
        &gr.affinityInitMin, &gr.affinityInitMax, &gr.affinityClampMin, &gr.affinityClampMax,
        &gr.orbitRadiusInitMin, &gr.orbitRadiusInitMax, &gr.orbitRadiusClampMin, &gr.orbitRadiusClampMax,
        &gr.absorbPreferenceInitMin, &gr.absorbPreferenceInitMax, &gr.absorbPreferenceClampMin, &gr.absorbPreferenceClampMax,
        &gr.repelStrengthInitMin, &gr.repelStrengthInitMax, &gr.repelStrengthClampMin, &gr.repelStrengthClampMax,
        &gr.maxSpeedInitMin, &gr.maxSpeedInitMax, &gr.maxSpeedClampMin, &gr.maxSpeedClampMax,
        &gr.maxAccelInitMin, &gr.maxAccelInitMax, &gr.maxAccelClampMin, &gr.maxAccelClampMax,
        &gr.energyThresholdInitMin, &gr.energyThresholdInitMax, &gr.energyThresholdClampMin, &gr.energyThresholdClampMax,
        &gr.mutationRateInitMin, &gr.mutationRateInitMax, &gr.mutationRateClampMin, &gr.mutationRateClampMax,
        &gr.cyanoPreferenceInitMin, &gr.cyanoPreferenceInitMax, &gr.cyanoPreferenceClampMin, &gr.cyanoPreferenceClampMax,
        &gr.cyanoDigestionInitMin, &gr.cyanoDigestionInitMax, &gr.cyanoDigestionClampMin, &gr.cyanoDigestionClampMax,
        &gr.nucleusSightRadiusInitMin, &gr.nucleusSightRadiusInitMax, &gr.nucleusSightRadiusClampMin, &gr.nucleusSightRadiusClampMax,
        &gr.nucleusPersonalSpaceInitMin, &gr.nucleusPersonalSpaceInitMax, &gr.nucleusPersonalSpaceClampMin, &gr.nucleusPersonalSpaceClampMax,
        &gr.nucleusRepelStrengthInitMin, &gr.nucleusRepelStrengthInitMax, &gr.nucleusRepelStrengthClampMin, &gr.nucleusRepelStrengthClampMax,
        &gr.nucleusCohesionInitMin, &gr.nucleusCohesionInitMax, &gr.nucleusCohesionClampMin, &gr.nucleusCohesionClampMax,
        &gr.forceRangeInitMin, &gr.forceRangeInitMax, &gr.forceRangeClampMin, &gr.forceRangeClampMax,
        &gr.forceCostKInitMin, &gr.forceCostKInitMax, &gr.forceCostKClampMin, &gr.forceCostKClampMax,
        &gr.hueInitMin, &gr.hueInitMax, &gr.hueJitterSigma};
    for (double* t : grTargets) {
        if (!rF64(is, *t)) { err = "cannot read genome ranges"; return false; }
    }
    unsigned char pn2 = 0;
    if (!rBytes(is, &pn2, 1) || pn2 > kPolicyNameLimit) {
        err = "cannot read policyName"; return false;
    }
    policyName.assign(pn2, '\0');
    if (pn2 > 0 && !rBytes(is, &policyName[0], pn2)) {
        err = "cannot read policyName bytes"; return false;
    }
    // 白名单：未知策略名读档即报错（装配面不存在静默回退）。
    if (!isKnownPolicy(policyName)) {
        err = "unknown policy in save: " + policyName;
        return false;
    }
    applyPolicyName(cfg, policyName);
    return true;
}

} // namespace savedetail

// ---- 名字校验（全拒绝路径都有 err 说明）----
bool SaveStore::validName(const std::string& name, std::string& err) {
    err.clear();
    if (name.empty()) { err = "save name is empty"; return false; }
    if (name.size() > kNameLimit) {
        err = "save name too long (max 64): " + std::to_string(name.size());
        return false;
    }
    for (const char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) {
            err = "save name contains control character";
            return false;
        }
        switch (c) {
            case '/': case '\\': case ':': case '*': case '?': case '"':
            case '<': case '>': case '|':
                err = std::string("save name contains forbidden character: '") + c + "'";
                return false;
            default: break;
        }
    }
    if (name.find("..") != std::string::npos) {
        err = "save name must not contain \"..\"";
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        err = "save name must not start/end with space or end with dot";
        return false;
    }
    static const char* const kReserved[] = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
        "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    std::string upper;
    upper.reserve(name.size());
    for (const char c : name) {
        upper += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    for (const char* const r : kReserved) {
        if (upper == r) { err = "save name is a reserved device name: " + name; return false; }
    }
    return true;
}

// ---- 存档 ----
bool SaveStore::save(const std::string& name, const Simulation& sim,
                     const SimConfig& cfg, const std::string& policyName,
                     std::string& err) {
    err.clear();
    if (!validName(name, err)) return false;
    // ---- 先在内存里组完整 payload，全部成功后才碰文件系统 ----
    // 教训（单测 save_unknown_policy_rejected_on_save 抓到）：旧写法先建目录/
    // 打开文件再校验 cfg 块，非法策略名会在"已截断的空 save.bin"落地后才返回
    // false——拒绝路径留下了半成品存档。现在：cfg 块与实体态先各自序列化进
    // 内存缓冲，两者都成功才 create_directories + 落盘（失败路径零副作用）。
    std::ostringstream payload;
    wU32(payload, kMagic);
    wU32(payload, kVersion);
    // seedIsDefault：v1 声明性快照位（track default-ness 的正式通道留 v2）。
    const unsigned char seedIsDefault = cfg.world.seed == 42u ? 1u : 0u;
    payload.put(static_cast<char>(seedIsDefault));
    for (int i = 0; i < 3; ++i) payload.put('\0');
    if (!savedetail::writeCfgBlock(payload, cfg, policyName, err)) return false;
    if (!sim.serializeState(payload)) {
        err = "serializeState failed on stream";
        return false;
    }
    const std::string bytes = payload.str();

    std::error_code ec;
    std::filesystem::create_directories(saveDir(name), ec);
    if (ec) { err = "cannot create save dir: " + ec.message(); return false; }
    const std::string binPath = saveDir(name) + "/save.bin";
    std::ofstream os(binPath, std::ios::binary | std::ios::trunc);
    if (!os.is_open()) { err = "cannot open " + binPath; return false; }
    os.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    os.close();
    if (os.fail()) { err = "write failed on " + binPath; return false; }

    // meta.txt（人可读）
    const std::string metaPath = saveDir(name) + "/meta.txt";
    std::ofstream meta(metaPath, std::ios::out | std::ios::trunc);
    if (!meta.is_open()) { err = "cannot open " + metaPath; return false; }
    meta << "name=" << metaEscape(name) << "\n";
    meta << "frame=" << static_cast<long long>(sim.tick()) << "\n";
    meta << "seed=" << cfg.world.seed << "\n";
    meta << "aliveNuclei=" << sim.nucleusCount() << "\n";
    meta << "savedAt=" << nowTimestamp() << "\n";
    meta.close();
    if (meta.fail()) { err = "write failed on " + metaPath; return false; }
    return true;
}

// ---- 读档头 ----
// ---- 存档头解析（流级共享核心）----
// loadHeader 与 load 都走同一条解析路径：解析完恰好停在实体态段起点，
// "头部多长"只有一个权威答案，不存在 seekg(8) 式的隐式耦合（实测教训：
// cfg 块 300+ 字节，硬编码偏移会把 cfg 字节当成实体态魔法读，load 必败）。
bool parseHeaderStream(std::istream& is, SimConfig& cfg,
                       std::string& policyName, std::string& err) {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!rU32(is, magic) || !rU32(is, version)) { err = "cannot read header"; return false; }
    if (magic != kMagic) { err = "bad magic (not ASAV)"; return false; }
    if (version != kVersion) {
        err = "unsupported save version: " + std::to_string(version) +
              " (expected 1)";
        return false;
    }
    unsigned char seedIsDefault = 0;
    if (!rBytes(is, &seedIsDefault, 1)) { err = "cannot read flags"; return false; }
    (void)seedIsDefault;
    unsigned char pad[3];
    if (!rBytes(is, pad, 3)) { err = "cannot read header pad"; return false; }
    if (!savedetail::readCfgBlock(is, cfg, policyName, err)) return false;
    return true;
}

// ---- 读档头（公共 API：只解析 cfg 关键字段与 policyName，不碰 Simulation）----
bool SaveStore::loadHeader(const std::string& name, SimConfig& cfg,
                           std::string& policyName, std::string& err) {
    err.clear();
    if (!validName(name, err)) return false;
    const std::string binPath = saveDir(name) + "/save.bin";
    std::ifstream is(binPath, std::ios::binary);
    if (!is.is_open()) { err = "cannot open " + binPath; return false; }
    if (!parseHeaderStream(is, cfg, policyName, err)) return false;
    return true;
}

// ---- 完整读档 ----
bool SaveStore::load(const std::string& name, Simulation& sim,
                     SimConfig& cfg, std::string& policyName, std::string& err) {
    err.clear();
    if (!validName(name, err)) return false;
    const std::string binPath = saveDir(name) + "/save.bin";
    std::ifstream is(binPath, std::ios::binary);
    if (!is.is_open()) { err = "cannot open " + binPath; return false; }
    // cfg 先行（构造期地形/温度场是确定性实体，必须由存档 cfg 重建）：
    // 同一个流上先解析头（解析完恰好停在实体态段起点），再交实体态。
    SimConfig hdr{};
    std::string pn;
    if (!parseHeaderStream(is, hdr, pn, err)) return false;
    cfg = hdr;
    policyName = pn;
    return sim.deserializeState(is, err);
}

// ---- 列表 ----
std::vector<SaveStore::Meta> SaveStore::list() {
    std::vector<Meta> out;
    std::error_code ec;
    if (!std::filesystem::exists(savesRoot(), ec)) return out;
    for (const std::filesystem::directory_entry& e :
         std::filesystem::directory_iterator(savesRoot(), ec)) {
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        const std::string metaPath = saveDir(name) + "/meta.txt";
        std::ifstream meta(metaPath, std::ios::in);
        if (!meta.is_open()) continue;
        Meta m;
        m.name = name;
        std::string line;
        while (std::getline(meta, line)) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq);
            const std::string v = line.substr(eq + 1);
            if (k == "frame") {
                m.frame = std::atoll(v.c_str());
            } else if (k == "seed") {
                m.seed = static_cast<std::uint64_t>(std::strtoull(v.c_str(), nullptr, 10));
            } else if (k == "aliveNuclei") {
                m.aliveNuclei = static_cast<std::size_t>(std::strtoull(v.c_str(), nullptr, 10));
            } else if (k == "savedAt") {
                m.savedAt = v;
            } else if (k == "name") {
                m.name = v;  // 文件内 name 优先于目录名
            }
        }
        if (m.name.empty()) m.name = name;
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const Meta& a, const Meta& b) {
        if (a.savedAt != b.savedAt) return a.savedAt > b.savedAt;  // savedAt 倒序
        return a.name < b.name;                                     // 同刻按名字升序
    });
    return out;
}

bool SaveStore::remove(const std::string& name, std::string& err) {
    err.clear();
    if (!validName(name, err)) return false;
    const std::string dir = saveDir(name);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) { err = "save not found: " + name; return false; }
    std::filesystem::remove_all(dir, ec);
    if (ec) { err = "remove failed: " + ec.message(); return false; }
    return true;
}

bool SaveStore::rename(const std::string& oldName, const std::string& newName,
                       std::string& err) {
    err.clear();
    if (!validName(oldName, err)) return false;
    if (!validName(newName, err)) return false;
    const std::string oldDir = saveDir(oldName);
    const std::string newDir = saveDir(newName);
    std::error_code ec;
    if (!std::filesystem::exists(oldDir, ec)) {
        err = "save not found: " + oldName;
        return false;
    }
    if (std::filesystem::exists(newDir, ec)) {
        err = "save already exists: " + newName;
        return false;
    }
    std::filesystem::rename(oldDir, newDir, ec);
    if (ec) { err = "rename failed: " + ec.message(); return false; }
    // meta.txt 里的 name= 字段同步改写（列表以文件内 name 为准）。
    const std::string metaPath = newDir + "/meta.txt";
    std::ifstream in(metaPath, std::ios::in);
    if (in.is_open()) {
        std::string body;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("name=", 0) == 0) line = "name=" + newName;
            body += line;
            body += '\n';
        }
        in.close();
        std::ofstream out(metaPath, std::ios::out | std::ios::trunc);
        if (out.is_open()) {
            out << body;
        }
    }
    return true;
}

} // namespace app
} // namespace agentsim
