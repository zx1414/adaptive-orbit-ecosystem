#pragma once
// agentsim - app/TraceWriter.h : JSON trace exporter for --trace.
// Contract: docs/TRACE-CONTRACT.md v3（R1-B 蓝细菌取代食物）. schemaVersion=3 且
// meta.contractVersion=3（两处必须同步写入）；top-level key order is fixed
// (schemaVersion, meta, bounds, series, frames); floats print %.6f;
// NaN/Inf collapse to 0.000000; entity order follows container index.
// v3 变更（R1-B，破坏性）：
//   · series 行删除 "food"（Food 实体已不存在），键序变为
//     tick, nuclei, cyanoAlive, cyanoEnergy, totalEnergy, injected, dissipated, auditOk。
//   · 帧内 "f" 数组的语义从食物元组改为**蓝细菌连线拓扑** [[idA,idB],...]
//    （键名保留 "f" 是刻意的：占位序 tick,n,b,f,c 不变，前端 diff 最小）。
//   · "c" 元组从 5 项 [id,x,y,energy,alive] 扩为 6 项 [id,x,y,energy,alive,hue]
//    （R1-B 遗传色相，hue ∈ [0,360)）。
// Rows buffer into per-part streams; finalize() dumps them in contract order.
// Defect-C caps: frame collection stops once framesCaptured_ reaches maxFrames_
// or the frames stream reaches maxFrameBytes_ (series is never capped);
// finalize() then emits "framesTruncated":true and "framesCaptured":N in meta.
// No crash, no silent data loss.
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

namespace app {

class TraceWriter {
public:
    static constexpr std::size_t kDefaultMaxFrames = 10000;
    static constexpr std::size_t kDefaultMaxFrameBytes = 64u * 1024u * 1024u;
    // Defect C: frames buffer caps (count + bytes); series is never capped.
    explicit TraceWriter(std::size_t maxFrames = kDefaultMaxFrames,
                         std::size_t maxFrameBytes = kDefaultMaxFrameBytes);
    bool open(const std::string& path);  // false when the file cannot be created
    void setMeta(long long seed, long long ticks, double width, double height,
                 const std::string& policy);
    void beginBounds(double minX, double minY, double maxX, double maxY);
    // series: one row per tick, never downsampled.
    // v3：cyanoAlive/cyanoEnergy 紧跟 nuclei（food 列已随 Food 实体删除）。
    void addSeries(long long tick, long long nuclei,
                   long long cyanoAlive, double cyanoEnergy,
                   double totalEnergy, double injected, double dissipated,
                   bool auditOk);
    // frames: spatial snapshot, downsampled via --trace-interval.
    void setGenomeEnabled(bool v) { genomeEnabled_ = v; }
    void beginFrame(long long tick);
    void addNucleus(long long id, double x, double y, double energy,
                    double cap, double speed, bool alive);
    void addBall(long long id, long long owner, double x, double y,
                 double energy, double radius, int type);
    // v3："f" = 蓝细菌连线拓扑（成交边有序对 [idA,idB]，双端为节点 id）。
    // addFood 已删除（Food 实体不存在）。
    void addLink(long long idA, long long idB);
    // v3：蓝细菌 6 元组 [id, x, y, energy, alive, hue]。beginFrame 后由 main 遍历
    // sim.cyanoNodes() 调用；顺序即容器下标（双跑一致性红线，不许排序）。
    // 坐标已是环面内坐标（CyanoField 内部 wrap），调用方无需再 wrap。
    // v3+（--trace-genome）：每核 25 个数 [id, 24 个基因按 Genome.h 声明序, hue]。
    // 顺序 = 容器下标（双跑一致性红线，不许排序）。只有 traceGenome 时才写，
    // 否则整个 "g" 键不出现（不是空数组——省体积且让分析器能区分"没开"与"没有核"）。
    void beginGenome();
    void addGenome(long long id, const double* genes, int count, double hue);
    void endGenome();
    void addCyano(long long id, double x, double y, double energy, bool alive,
                  double hue);
    void endFrame();
    bool finalize();  // writes buffered JSON to disk; false on I/O failure
    bool isOpen() const { return out_.is_open(); }
    long long framesCaptured() const {
        return static_cast<long long>(framesCaptured_);
    }
    bool framesTruncated() const { return framesDropped_ != 0; }

private:
    static double safe(double v);
    static const char* jbool(bool v) { return v ? "true" : "false"; }
    bool frameOverflow();  // true when a frame must be dropped (defect C caps)

    std::ofstream out_;
    std::ostringstream series_;
    std::ostringstream frames_;
    std::ostringstream n_;
    std::ostringstream b_;
    std::ostringstream f_;
    std::ostringstream c_;
    std::ostringstream g_;
    bool seriesFirst_ = true;
    bool frameFirst_ = true;
    bool nFirst_ = true;
    bool bFirst_ = true;
    bool fFirst_ = true;
    bool cFirst_ = true;
    bool gFirst_ = true;
    bool genomeEnabled_ = false;
    long long seed_ = 0;
    long long ticks_ = 0;
    double width_ = 0.0;
    double height_ = 0.0;
    double minX_ = 0.0;
    double minY_ = 0.0;
    double maxX_ = 0.0;
    double maxY_ = 0.0;
    std::string policy_;
    std::size_t maxFrames_;
    std::size_t maxFrameBytes_;
    std::size_t framesCaptured_ = 0;
    std::size_t framesDropped_ = 0;
    bool frameDropped_ = false;
};

} // namespace app
