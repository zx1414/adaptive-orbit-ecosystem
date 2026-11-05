#include "TraceWriter.h"

#include <cmath>
#include <iomanip>

namespace app {

TraceWriter::TraceWriter(std::size_t maxFrames, std::size_t maxFrameBytes)
    : maxFrames_(maxFrames), maxFrameBytes_(maxFrameBytes) {
    // Defect A: stream flags do not propagate; every stream that may hold a
    // float must set fixed/setprecision(6) itself (contract hard rule 2).
    series_ << std::fixed << std::setprecision(6);
    frames_ << std::fixed << std::setprecision(6);
    n_ << std::fixed << std::setprecision(6);
    b_ << std::fixed << std::setprecision(6);
    f_ << std::fixed << std::setprecision(6);
    c_ << std::fixed << std::setprecision(6);
}

// Contract hard rule 3: NaN/Inf must never reach the file; collapse to 0.
double TraceWriter::safe(double v) { return std::isfinite(v) ? v : 0.0; }

bool TraceWriter::open(const std::string& path) {
    if (path.empty()) return false;
    out_.open(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out_.is_open()) return false;
    out_ << std::fixed << std::setprecision(6);
    return out_.good();
}

void TraceWriter::setMeta(long long seed, long long ticks, double width,
                          double height, const std::string& policy) {
    seed_ = seed;
    ticks_ = ticks;
    width_ = width;
    height_ = height;
    policy_ = policy;
}

void TraceWriter::beginBounds(double minX, double minY, double maxX, double maxY) {
    minX_ = minX;
    minY_ = minY;
    maxX_ = maxX;
    maxY_ = maxY;
}

// Defect C: a frame is captured unless a cap is already exceeded. Once any
// frame is dropped, collection stops for good; finalize() marks it in meta.
bool TraceWriter::frameOverflow() {
    if (frameDropped_) return true;
    const std::streamoff buffered = frames_.tellp();
    if (framesCaptured_ >= maxFrames_ ||
        (buffered > 0 &&
         static_cast<std::size_t>(buffered) >= maxFrameBytes_)) {
        frameDropped_ = true;
    }
    return frameDropped_;
}

// v3 键序：tick, nuclei, cyanoAlive, cyanoEnergy, totalEnergy, injected,
// dissipated, auditOk（"food" 列已随 Food 实体删除；其余相对顺序不变）。
void TraceWriter::addSeries(long long tick, long long nuclei,
                            long long cyanoAlive, double cyanoEnergy,
                            double totalEnergy, double injected,
                            double dissipated, bool auditOk) {
    if (!seriesFirst_) series_ << ',';
    seriesFirst_ = false;
    series_ << "{\"tick\":" << tick
            << ",\"nuclei\":" << nuclei
            << ",\"cyanoAlive\":" << cyanoAlive
            << ",\"cyanoEnergy\":" << safe(cyanoEnergy)
            << ",\"totalEnergy\":" << safe(totalEnergy)
            << ",\"injected\":" << safe(injected)
            << ",\"dissipated\":" << safe(dissipated)
            << ",\"auditOk\":" << jbool(auditOk) << '}';
}

// v3+（--trace-genome）: 每核 25 个数 [id, 24 基因..., hue]。
// genes 指向调用方栈上数组（24 项，顺序 = Genome.h 的声明序），count 必须是 24。
// 不在内部做任何排序/过滤：顺序即容器下标，双跑一致性红线。
void TraceWriter::beginGenome() {
    g_.str(std::string());
    g_.clear();
    gFirst_ = true;
}
void TraceWriter::addGenome(long long id, const double* genes, int count, double hue) {
    if (!genomeEnabled_ || genes == nullptr || count <= 0) return;
    if (!gFirst_) g_ << ',';
    gFirst_ = false;
    g_ << '[' << id;
    for (int i = 0; i < count; ++i) g_ << ',' << safe(genes[i]);
    g_ << ',' << safe(hue) << ']';
}
void TraceWriter::endGenome() {}  // 无收尾动作；endFrame 负责拼接

void TraceWriter::beginFrame(long long tick) {
    // n_/b_/f_/c_ are reused via str(""); str() does not touch flags, but the
    // %.6f state is re-asserted anyway so row streams never drift (defect A).
    n_.str("");
    n_.clear();
    b_.str("");
    b_.clear();
    f_.str("");
    f_.clear();
    c_.str("");
    c_.clear();
    nFirst_ = true;
    bFirst_ = true;
    fFirst_ = true;
    cFirst_ = true;
    if (frameOverflow()) {
        ++framesDropped_;
        return;  // caps hit: series keeps flowing, frame is skipped.
    }
    if (!frameFirst_) frames_ << ',';
    frameFirst_ = false;
    frames_ << "{\"tick\":" << tick;
}

void TraceWriter::addNucleus(long long id, double x, double y, double energy,
                             double cap, double speed, bool alive) {
    if (frameDropped_) return;
    if (!nFirst_) n_ << ',';
    nFirst_ = false;
    n_ << '[' << id << ',' << safe(x) << ',' << safe(y) << ',' << safe(energy)
       << ',' << safe(cap) << ',' << safe(speed) << ',' << jbool(alive) << ']';
}

void TraceWriter::addBall(long long id, long long owner, double x, double y,
                          double energy, double radius, int type) {
    if (frameDropped_) return;
    if (!bFirst_) b_ << ',';
    bFirst_ = false;
    b_ << '[' << id << ',' << owner << ',' << safe(x) << ',' << safe(y) << ','
       << safe(energy) << ',' << safe(radius) << ',' << type << ']';
}

// v3："f" = 蓝细菌连线拓扑 [idA, idB]（整型有序对，无浮点位）。
void TraceWriter::addLink(long long idA, long long idB) {
    if (frameDropped_) return;
    if (!fFirst_) f_ << ',';
    fFirst_ = false;
    f_ << '[' << idA << ',' << idB << ']';
}

void TraceWriter::addCyano(long long id, double x, double y, double energy,
                           bool alive, double hue) {
    if (frameDropped_) return;
    if (!cFirst_) c_ << ',';
    cFirst_ = false;
    // v3 六元组：[id, x, y, energy, alive, hue]（R1-B 遗传色相）。
    c_ << '[' << id << ',' << safe(x) << ',' << safe(y) << ',' << safe(energy)
       << ',' << jbool(alive) << ',' << safe(hue) << ']';
}

void TraceWriter::endFrame() {
    if (frameDropped_) return;
    // Contract key order inside a frame: tick, n, b, f, c（键序 v1/v2/v3 不变；
    // v3 中 "f" 的载荷从食物元组换成连线拓扑，"c" 从 5 项扩为 6 项）。
    frames_ << ",\"n\":[" << n_.str() << "],\"b\":[" << b_.str()
            << "],\"f\":[" << f_.str() << "],\"c\":[" << c_.str() << "]";
    // v3+：--trace-genome 时追加 "g"（每核 25 数：id + 24 基因 + hue）。
    // 键序在 c 之后（追加，不改既有键序）；未开启时该键完全不出现，
    // 让分析器能区分"没开基因组导出"与"这一帧没有核"。
    if (genomeEnabled_) frames_ << ",\"g\":[" << g_.str() << "]";
    frames_ << "}";
    ++framesCaptured_;
}

namespace {
// Minimal JSON string escape for the policy name (validated upstream, but the
// writer must never emit a malformed document regardless of input).
std::string jescape(const std::string& s) {
    std::string r = "\"";
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            r += '\\';
            r += ch;
        } else if (c < 0x20) {
            static const char* kHex = "0123456789abcdef";
            r += "\\u00";
            r += kHex[(c >> 4) & 0xF];
            r += kHex[c & 0xF];
        } else {
            r += ch;
        }
    }
    r += '"';
    return r;
}
} // namespace

bool TraceWriter::finalize() {
    if (!out_.is_open()) return false;
    // Contract: top-level key order is fixed and frames come last because the
    // per-tick series must be accumulated while the run is still going.
    // v3：meta 里冗余写入 contractVersion=3（与顶层 schemaVersion 同步），
    // 前端不必再从顶层键里扒版本号。Defect C: when any frame was dropped,
    // meta records framesTruncated=true plus framesCaptured=N so the file
    // self-describes the cut. Frames stay last so the meta keys are written
    // once, before the bulk payload.
    out_ << "{\"schemaVersion\":3,"
         << "\"meta\":{\"seed\":" << seed_
         << ",\"ticks\":" << ticks_
         << ",\"world\":{\"width\":" << safe(width_)
         << ",\"height\":" << safe(height_) << "},"
         << "\"policy\":" << jescape(policy_)
         << ",\"generator\":\"agentsim\""
         << ",\"contractVersion\":3"
         << ",\"framesCaptured\":" << framesCaptured_
         << ",\"framesTruncated\":" << jbool(framesTruncated()) << "},"
         << "\"bounds\":{\"minX\":" << safe(minX_)
         << ",\"minY\":" << safe(minY_)
         << ",\"maxX\":" << safe(maxX_)
         << ",\"maxY\":" << safe(maxY_) << "},"
         << "\"series\":[" << series_.str() << "],"
         << "\"frames\":[" << frames_.str() << "]}\n";
    out_.flush();
    const bool ok = out_.good();
    out_.close();
    return ok;
}

} // namespace app
