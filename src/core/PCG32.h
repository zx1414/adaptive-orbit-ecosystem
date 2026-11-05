// agentsim - core/PCG32.h
// 自传 PCG32 伪随机数生成器（基于 O'Neill 公开算法的经典实现）。
// 红线：不使用 <random> 与 rand()；固定种子逐字节可复现（S2）。
// state()/setState() 支持快照/恢复，供确定性双跑与调试。
#pragma once
#include <cstdint>

namespace agentsim {

class PCG32 {
public:
    // 默认种子非零，避免全零状态卡死。
    PCG32(std::uint64_t seed = 0x853c49e6748fea9bULL,
          std::uint64_t seq = 0xda3e39cb94b95bdbULL);

    // 前进一步 LCG，输出 XSL-RR 64->32 位。
    std::uint32_t nextUint32();

    // [0,1) 双精度：53 位有效位，分子分母各取高 27/26 位。
    double nextDouble();

    void setState(std::uint64_t state, std::uint64_t inc);
    void getState(std::uint64_t& state, std::uint64_t& inc) const;

private:
    std::uint64_t state_;
    std::uint64_t inc_;
};

} // namespace agentsim
