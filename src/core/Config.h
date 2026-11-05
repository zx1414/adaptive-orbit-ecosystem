// agentsim - core/Config.h
// 仿真配置聚合（POD 风格）。数值均为初版占位默认值，可由 CLI 覆盖。
//
// 【R1-B 2026-11-01：蓝细菌取代食物】FoodConfig / SimConfig::food 已整体删除：
// 蓝细菌是唯一食物来源（用户拍板），初始库存改用 CyanoParams 节点数
// （--cyano-nodes / --cyano-max-nodes），核只捕食蓝细菌节点。
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "Genome.h"

namespace agentsim {

struct WorldConfig {
    double width = 1024.0;  // 环面宽（R10：512→1024，用户验收"世界太小"；面积 16× 原版）
    double height = 1024.0; // 环面高（R10）
    std::uint64_t maxTicks = 1000;
    std::uint64_t seed = 42;
};

// ---- 阳光模型（R1-B：参数化昼夜周期，替代原硬编码 0.5 + 0.5*sin(tick*0.01)）----
// 计算式（R1-B 契约，仿真方与前端共同遵守）：
//   base = sun.baseLevel - sun.verticalGradient * (y / worldHeight)   ∈ [0,1]（截断后）
//   day  = sun.dayAmplitude * sin(2*pi * tick / sun.dayPeriodFrames)
//   noise= sun.noiseAmplitude * hash01(tick, floor(y / cellH), tick/sun.noiseSpeed)
//          （确定性整数哈希：仅依赖入参零依赖 RNG，hash01 ∈ [0,1)）
//   sunLevel(tick, y) = clamp01(base + day + noise)
// 参数全为默认占位：base=0.5、amp=0.5、period=628 帧、noise 关闭（0 幅度）
// 时逐位退化回旧硬编码式 0.5 + 0.5*sin(0.01*tick)（0.01 = 2*pi/628.3185… 的
// 同一近似周期；R1-B 取 dayPeriodFrames=628，sun(0)=0.5、峰值 1.0，与旧曲线重合）。
struct SunConfig {
    double baseLevel = 0.5;          // 基础光强（垂直梯度取 0 时的全天均值）
    double dayAmplitude = 0.5;       // 昼夜正弦幅度
    double dayPeriodFrames = 628.0;  // 昼夜周期（帧）；<=0 时昼夜项恒 0
    double verticalGradient = 0.0;   // 垂直梯度：y=0 最亮，y=H 最暗（系数 × y/H）
    double noiseAmplitude = 0.0;     // 阳光噪声幅度（默认 0 = 关闭，回归安全）
    double noiseFrequency = 0.125;   // 噪声网格频率（格宽 = 1/freq 世界单位）
    double noiseSpeed = 1.0;         // 噪声时间步进（每 speed 帧换一格；<=0 视作静止）
};

struct PolicyConfig {
    double senseRadius = 24.0; // 探测半径
    double damping = 0.9;      // 速度阻尼
    double mutation = 0.05;    // 初版占位：规则扰动幅度
    // ---- 追加字段（F2）：策略名，组合根按此装配 RulePolicy/StubPolicy ----
    std::string name = "rule";
    // ---- 追加字段（感知机策略）：字段仅声明，接线由下一轮完成（不改 Cli/Simulation）。----
    bool usePerceptron = false;            // 组合根按此装配 PerceptronPolicy（当前未接线）
    std::uint64_t perceptronSeed = 12345;  // 构造期 PCG32 初值种子
    double perceptronScale = 0.1;          // 初值幅度：w/b 在 [-scale, scale] 均匀取值
    // ---- 追加字段（趋温策略）：字段仅声明，接线由下一轮完成（不改 Cli/Simulation）。----
    bool useThermal = false;               // 组合根按此装配 ThermalPolicy（当前未接线）
    double thermalPreferredTemp = 0.5;     // 最适温度（物种偏好，对应归一化采样 s[17]）
    // 打分权重（--thermal-w-thermal / --thermal-w-caution 的真实落点）。
    double thermalWThermal = 1.2;          // 温度适宜度权重
    double thermalWCaution = 0.8;          // 失真规避权重
};

// ---- 追加结构（F2）：agent-mode 契约。仅 1（自治体范式）已实现；0（轨道范式）解析期拒绝。----
struct AgentConfig {
    int agentMode = 1;
};

// ---- 追加结构（F2）：群体规模。R1-B：food 字段随 Food 实体一起删除，
// 初始第一营养级库存 = CyanoParams.nodeSeedCount（--cyano-nodes）。----
struct ColonyConfig {
    std::size_t nuclei = 12;   // 初始核数量
    // ---- R2-A（基因组接线）追加：种群性能护栏与遗传全局参数 ----
    // maxNuclei：核数上限。**不是**生态承载力——承载力由能量收支自然决定
    //（出生耗能 + 捕食供给 + 呼吸/移动耗散的负反馈）。300 只是 O(n²) 对核物理
    // 的性能护栏，正常参数下永远撞不到（GOAL-ECOLOGY：kReproCap=24 硬顶已删除）。
    std::size_t maxNuclei = 1200; // R10：512→1024 再 2 倍折中（600→1200；O(n²) 下 1200 核 ≈ 4× 单帧成本，实测再评）
    // mutationCoefficient：genomeMutate 的全局变异系数（乘在每基因的
    // mutationRate*(clampMax-clampMin) 步长上）。0.25 = 温和搜索（子代理 A 建议，
    // 1.0 会一步跨过半个参数域，选择压被噪声淹没）。
    double mutationCoefficient = 0.25;
    // genomeRanges：24 参数的 Init/Clamp 区间（genomeRandomize/genomeMutate 消费）。
    // 默认值 = Genome.h 标定区间；组合根出生引导与繁殖变异统一从这里取。
    GenomeRanges genomeRanges{};
};

// ---- 追加结构（温度场）：环境层温度场配置（温度偏好失真机制的地基）----
struct TemperatureSource {
    double x = 0.0, y = 0.0; // 源中心（世界坐标）
    double amplitude = 0.0;  // 贡献幅度：正为热源，负为冷源
    double radius = 200.0;   // 高斯半径
};

struct TemperatureConfig {
    std::size_t sourceCount = 3;
    double baseline = 0.5;    // 基线温度
    double preferred = 0.5;   // 最适温度（物种偏好，初版全局常量）
    double distortionK = 2.0; // 失真放大系数
    std::vector<TemperatureSource> sources; // 非空时 TemperatureField::generate 直接采用
};

// ---- 追加结构（地形）：环境层地形配置（障碍 / 水域 / 温度区）。----
// 各 count 默认 0：默认不生成任何地貌，不改变现有生态行为（回归安全）。
struct TerrainBlob {
    double x = 0.0, y = 0.0;
    double radius = 100.0;   // <=0 时 Terrain 回退到组默认半径
    double strength = 1.0;   // 温度区用：中心温度偏移量
};

struct TerrainConfig {
    std::size_t obstacleCount = 0;       // 默认 0（回归安全）
    std::size_t waterCount = 0;
    std::size_t thermalZoneCount = 0;
    // 半径按「世界面积占比」标定，而不是拍脑袋给绝对值：
    // 默认世界 256x256 = 65536 面积。障碍 80 半径 => 单块 π*80² ≈ 20106（占 31%），
    // 5 块就超过 150% 面积，会把世界堵死（实测：nuclei 13->4，食物总量爆涨）。
    // 因此障碍取 24（单块占 2.8%，5 块约 14%，留出通路）。
    double obstacleRadius = 24.0;
    // 水域不阻挡、只提高耗能，可以大一些；但 120 仍偏大（3 块占 62%），取 60。
    double waterRadius = 60.0;
    double waterCostMultiplier = 1.8;    // 水域移动耗能倍率
    // 温度区只施加偏移、不阻挡也不耗能，取 100（2 块占 48%，可接受）。
    double thermalRadius = 100.0;
    double thermalStrength = 0.15;       // 散点热区的中心偏移
    std::vector<TerrainBlob> obstacles;     // 非空的组直接采用（不消耗 rng）
    std::vector<TerrainBlob> waters;
    std::vector<TerrainBlob> thermalZones;
};

// ---- R3-B（实时调控接线）新增结构：运行时键值的落点 ----
// 设计约束（任务卡 R3-B）：
//   - behavior/AgentBehavior.h 本轮不可改，BehaviorParams 无法加字段——
//     nucleus./pressure. 组里"行为层已有同义字段"的键（basal_cost / speed_cost_k /
//     cyano_predation_radius）经 RuntimeTuning 的 NucleusTuning 镜像落 Config，
//     由 Simulation::applyConfig 拷贝进 bp_ 的**既有**字段（立即生效）；
//     行为层尚无落点的键先存于此，待 R3-A/R4 消费（字段注释逐条声明消费者）。
//   - CyanoParams 在 environment/ 层，core/Config.h 不得反向 include（分层红线）——
//     用 CyanoTuning 白盒镜像，Simulation::applyConfig 逐字段同步进 CyanoField。
//   - 全部默认值 = 现行真实值（不是 schema 提案值），保证"接线前逐位等价"。
//     注意 ParamSchema 的 dflt 是参数页"恢复默认"的目标值，与这里的历史实际值
//     允许不同（如 conversion_rate schema 默认 1.0、实际 0.6——接线不改历史行为）。

// 运行面参数（serve 循环 / 前端 / 初始库存语义；非世界物理）。
struct RuntimeTuning {
    double maxFps = 0.0;              // max_fps：实时模式帧率上限（0 = 不限速）。serve 循环消费。
    int renderInterval = 1;           // render_interval：前端快照发布间隔帧。serve 循环消费。
    int autosaveInterval = 200;       // autosave_interval：检查点间隔（R4 存档消费；现仅存储）。
    int initialFreeBalls = 0;         // balls：启动自由球库存（0 = 现行行为：无启动库存，R4 消费）。
    double lightWarmth = 0.5;         // light_warmth：光色温（前端渲染消费；现仅存储）。
    int lightSampleGrid = 8;          // light_sample_grid：前端采样网格密度（现仅存储）。
    double cyanoInitialEnergy = 40.0; // cyano_initial_energy：节点播种能量。CyanoField::seed
                                      // 现固定 0（模块头文件本轮不可改），R4 播种能量化时消费。
    int cyanoColonies = 64;           // cyano_colonies：初始聚落数（R8-T53：世界 512 后 4→16，簇覆盖比与 256 世界等价）。
    bool cyanoEnabled = true;         // cyano_enabled：蓝细菌网络总开关（EnvControl cyano=on/off）。
                                      // R3-A 在 step() 内消费（关闭 = 光合短路）；本轮仅存储。
};

// 压力/核生理参数（pressure 组 + nucleus 组）。respirationBase / locomotionCost /
// cyanoPredationRadius 经 applyConfig 同步进 BehaviorParams 既有字段（立即生效）；
// 其余为 R3-A（核物理）与 R4 的消费者预留。
struct NucleusTuning {
    // 构造期经 Simulation 构造函数镜像进 bp_（此前只在 applyConfig 生效，CLI 运行
    // 从未消费过本字段——构造期镜像补上后 CLI 默认运行才真正吃到这里的值）。
    // R3-A 回滚说明：0.012 标定与 0.35 光合同试过（扫描显示创始瓶颈期死亡更多），
    // 回到 0.008；"活着有真实成本"由GOAL §三.4 的基因驱动捕食承担。
    // R3-A 标定：0.008 -> 0.03（sweep 实测：提高基础燃烧把 N* = 供给/燃烧压进
    // 护栏下方，是消费者平衡种群最有效的单旋钮）。三种子验收数据（final42/7/123）
    // 即本值；0.02 变体实测 seed123 多样性塌到 14/24（0.03 为 24/24）、crit1 均值
    // 0.83 vs 0.655，弃用。注意：0.03 下 check.ps1 的 200-tick live-param
    // --thermal-w-caution 探针窗口内两策略轨迹巧合收敛（1000 tick 实测分叉：
    // 核 21 vs 105，参数非死），探针窗口建议拉长——tools/check.ps1 不在本卡 paths。
    double respirationBase = 0.03;         // basal_cost / respiration_base（二键同字段，去重映射）
    double locomotionCost = 0.0002;        // speed_cost_k（= BehaviorParams::locomotionCost 同值）
    double speedCostExponent = 2.0;        // speed_cost_exponent（R11-P1-3 接线：代谢 ∝ speed^β；2.0 = 旧 s*s 逐位恒等）
    double reproductionCostRatio = 1.0;    // reproduction_cost_ratio（R11-P1-3 接线：繁殖成本 = 门槛×ratio；1.0 = 旧扣满门槛逐位恒等）
    double childEnergyRatio = 0.5;         // child_energy_ratio（R3-A；现子代分成是行为层常量）
    double reproductionOffset = 25.0;      // reproduction_offset（R3-A；现出生偏移是行为层常量）
    double nucleusInitEnergyRatio = 1.0;   // nucleus_init_energy_ratio（R4；现出生能量 60 硬编码）
    double damping = 1.0;                  // nucleus_damping（R11-P1-3 接线：速度阻尼 ∈[0,1]，1.0 = 无阻尼逐位恒等）
    double minSeparation = 12.0;           // nucleus_min_separation（R3-A 硬分离消费）
    double repelK = 2.0;                   // nucleus_repel_k（R3-A 贴脸斥力消费）
    double forageK = 1.0;                  // forage_k（R3-A 觅食引力消费）
    double forageRadius = 100.0;           // forage_radius（R3-A 消费）
    double wanderK = 0.5;                  // wander_k（R3-A 随机游走消费）
    double wanderTurnRate = 0.5;           // wander_turn_rate（R3-A 消费）
    double cyanoPredationRadius = 14.0;    // cyano_predation_radius（= BehaviorParams 同名字段同值）
    // R3-A：捕食口粮系数（每口原始激励 = cyanoPreference * grazeBiteFraction *
    // 猎物能量）。原为 Simulation.cpp 匿名 namespace 的 constexpr kGrazeBiteFraction=0.05
    //（R2-A 标定注释保留在 Simulation.cpp 使用点）；R3-A 提升为配置参数——
    // 配合捕食避难所（CyanoTuning.grazeRefugeEnergy）标定消费者承载力：
    // 平衡核数 N* ≈ 光合供给 / (单核消费速率) ∝ 1/grazeBiteFraction，
    // 提高系数把 N* 从护栏(300)上方拉回护栏下方，消费端由食物稀缺自然调节。
    // R3-A 标定：0.05 -> 0.15（sweep 实测：单口效率提高三倍使核在食物稀缺时
    // 攒能量更快，N* 进一步下降；配合避难所不再触发"吃绝"）。
    double grazeBiteFraction = 0.15;       // （R2-A 原值 0.05；R3-A 标定见使用点注释）
    // R9-A（主代理代笔，无 spawn 会话；偏离已记 LOG）：迁移回报。
    // 机制：捕食目标若距核**上一次进食点** > migrationRadius（环面距离），转化率
    // 补偿 (1 + migrationBonus)（钳到 ≤1.0）——补偿的是转化率公式对快核的折扣，
    // 不新增能量注入（transfer 仍零和：节点 -> 核）。慢核坐吃转化率不变。
    // 零值 = 关闭，全程逐位回归（对拍门禁保证）。
    double migrationBonus = 0.0;           // migration_bonus（R9-A；0=默认关闭。实验 0.6/r120：6000t 撞 600 护栏、hue 5.4° 更差——奖励太强反成"快=多吃"的新单调压，见 LOG）
    double migrationRadius = 120.0;        // migration_radius（R9-A；距离阈值，环面单位）
};

// 球能量经济学 + 球力场参数（food 组的 ball_* 7 键 + ball 组 15 键）。
// R4 球力场实现消费；ballEnergy / ballInitialEnergy 同时是事件丰收"每球能量"口径
// 的参考（事件能量直接走 ledger，不产实体，见 main.cpp 事件执行器）。
struct BallTuning {
    double energy = 10.0;                  // ball_energy
    double radius = 1.0;                   // ball_radius
    double velRange = 5.0;                 // ball_vel_range（新生球初速随机范围 ±）
    double initialEnergy = 10.0;           // ball_initial_energy
    double energyCap = 500.0;              // ball_energy_cap
    // ball_loss_cost（R7-B 消费：换主对原主的扣款）。R7-B 标定 5.0 -> 0.2：
    // 5.0 是 v2.3.1 代码默认值，其 env_config_balanced.txt 实测记档
    // "ball_loss_cost=5.0 -> 核在 ~300-1000 帧内崩塌到 1 个"（R7-B 500-tick
    // probe 复现：12 核 -> 2 核，ball_loss=745 ≈ 创始种群全部能量）；v2.3.1
    // 实际发行 env_config.txt 用 1.5 且靠 absorbBalls 吃球回收对冲（本项目
    // 球是仓库不回收），故取其 12000 帧稳定档 0.2（同源标定注释）。
    double lossCost = 0.2;
    double detachRadius = 60.0;            // detach_radius（R4 脱附半径）
    double damping = 0.92;                 // ball_damping（R4）
    double radialK = 0.05;                 // radial_k（R4 轨道径向弹簧）
    double tangentialK = 1.0;              // tangential_k（R4 切向环绕驱动）
    double nearRepelRadius = 8.0;          // near_repel_radius（R4）
    double ballRepelRadius = 6.0;          // ball_ball_repel_radius（R4）
    double ballRepelK = 0.5;               // ball_ball_repel_k（R4）
    double sameOwnerAttractRadius = 0.0;   // same_owner_attract_radius（0 = 关闭。R11-P1-4 曾试恢复原版 40：256 世界机制测试 reproduction_grows_population 被破坏——球群聚拢改变创始瓶颈期能量动力学；1024 世界稳态重标定时再开。RuntimeEnv same_owner_attract_radius 可运行时启用）
    double sameOwnerAttractK = 0.05;       // same_owner_attract_k（R4 标定；与 radius 同批恢复原版值时同步调）
    double maxSpeedShield = 15.0;          // ball_max_speed_shield（R4）
    double maxSpeedResource = 20.0;        // ball_max_speed_resource（R4）
    double maxSpeedScout = 30.0;           // ball_max_speed_scout（R4）
    double followCoupling = 4.0;           // follow_coupling（R4）
    double followBoost = 1.5;              // follow_boost（R4）
    double bounceRestitution = 1.0;        // bounce_restitution（R4；wrap 优先时忽略）
    bool wrapBoundary = true;              // wrap_boundary（R4；当前实现恒 wrap）
};

// 蓝细菌参数白盒镜像（environment/CyanoParams 的 core 侧拷贝；逐字段同名同默认值）。
// 分层约束：core/ 不得 include environment/，而 SimConfig 需要 cyano.* 键的落点——
// Simulation::applyConfig 把本结构逐字段拷进 CyanoField 的 params_（白盒契约：
// 两边字段必须同名同义，CyanoNode.h 头部注释为准）。新增 CyanoParams 字段时
// 两边同步加，漏一侧会被 applyConfig 的编译期逐字段赋值当场暴露。
struct CyanoTuning {
    // R3-A 标定回滚说明：曾按 docs/ECOLOGY-DESIGN.md 常数表试过 0.35/0.05
    //（参数扫描实测：光合注入不足 -> 12 核创始种群饿到只剩 3 -> 消费端起不来，
    // 组合根生态健全性测试 reproduction_grows_population 失败）。该表按旧的
    // 捕食口粮系数标定，与现行 kGrazeBiteFraction=0.5 基因驱动口径不匹配。
    // 维持模块默认 1.0/0.02，承载力负反馈交给自遮光 + 捕食避难所。
    // R3-A 标定尝试记录：0.35（创始种群饿死）、0.5（种群压崩 26 核、CV 0.77、
    // 多样性塌）均失败；1.0 是当前消费端结构下的可行值，能量斜率由遮蔽 k=0.20
    // 的负反馈承担（节点数自我收缩）。
    double photosynthesisRate = 1.0;       // cyano_photosynthesis_rate
    double lightHalfSaturation = 0.5;      // cyano_light_half_saturation（light 组）
    double metabolismRate = 0.02;          // cyano_metabolism_rate
    double energyCap = 100.0;              // cyano_energy_cap
    double reproductionCost = 40.0;        // （无直接键；与 CyanoParams 默认同步）
    double reproductionProbability = 0.01; // cyano_reproduction_probability
    // R3-A 标定尝试记录：300（主代理建议的空间异质性方向）实测 seed42
    // 多样性 15/24、hue 5.6°，比 125 更差（群落过度破碎反而加速局域灭绝/漂变）
    // ——回滚 125，机制层结论（需要空间异质性但偏移量不是正确旋钮）写进报告。
    double reproductionOffset = 125.0;     // cyano_reproduction_offset  // R10 实验 (c)：分裂散布 125→260（菌落前沿扩张加速，让"迁移"有市场）
    double minSeparation = 10.0;           // cyano_min_node_separation
    double ballSpawnCost = 30.0;           // ball_spawn_cost（food 组）
    double ballSpawnProbability = 0.02;    // cyano_ball_spawn_probability（EnvControl food 轴用键）
    double ballRadius = 1.0;               // （产球能量倍数；与 BallTuning::radius 无关）
    double ballSpawnMinDist = 10.0;        // ball_spawn_min_dist
    double ballSpawnMaxDist = 30.0;        // ball_spawn_max_dist
    double linkRadius = 60.0;              // cyano_link_distance
    int linkMaxNeighbors = 3;              // cyano_max_links
    double linkTransferRate = 0.1;         // cyano_transfer_rate
    double linkDeadband = 1.0;             // cyano_transfer_deadband
    int linkCheckInterval = 1;             // cyano_link_check_interval
    std::size_t maxNodes = 4096;        // cyanoMaxNodes 的镜像（R10：1024→4096 随世界再 4 倍）
    std::size_t nodeSeedCount = 128;       // cyanoNodeCount 的镜像（R8-T53 同步 128）
    int conversionMode = 1;                // cyano_conversion_mode
    double conversionRate = 0.6;           // cyano_conversion_rate（实际值 0.6，非 schema 提案 1.0）
    double conversionExponent = 1.0;       // cyano_conversion_exponent
    double conversionSpeedRef = 40.0;      // cyano_conversion_speed_ref（实际 40，非提案 20）
    double conversionAccelRef = 60.0;      // cyano_conversion_accel_ref（实际 60，非提案 6）
    double conversionWSpeed = 1.0;         // cyano_conversion_w_speed
    double conversionWAccel = 1.0;         // cyano_conversion_w_accel
    double conversionMinRate = 0.05;       // cyano_conversion_min_rate（实际 0.05，非提案 0.1）
    double conversionMaxRate = 1.0;        // cyano_conversion_max_rate
    // R3-A 标定：8 -> 20（founder 瓶颈后 hue 圆标准差塌到 5.5°~16°；加大每次
    // 分裂的色相抖动加速谱系再发散。判据 4 阈值 30°）。
    double hueJitterSigma = 20.0;          // cyano_hue_jitter（原 8.0）
    int cyanoColonies = 64;  // cyano_colonies 初始聚落数（R10：16→64 随世界再 4 倍）
    double clusterSpread = 45.0;           // cyano_cluster_spread 聚落撒点半径（R7-C 消费）
    // R3-A 自遮光：局部光竞争（节点越密 -> 单位产量越低）。与 CyanoParams 的
    // shade 三字段同名同义（白盒镜像契约），applyConfig 逐字段直通 CyanoField；
    // 构造期由 Simulation 构造函数同样镜像（否则构造期配置被静默忽略）。
    // R3-A 标定（sweep 实测 seed42/4000-5000tick）：k=0.05 时遮蔽不足以把
    // 生产者注入压到消费端耗散之下（N* > 300 护栏 -> 撞顶锁死）；k=0.20 把
    // 稳态节点数压到 ~44，消费者 N* 落回 ~100（< 护栏），种群出现健康波动
    //（tailCV 0.138）。注意 CyanoParams.shadeK 模块默认仍是 0.05（独立语义，
    // 单测钉死不受影响）；本字段经构造期/applyConfig 镜像覆盖。
    double shadeK = 0.20;                  // 遮蔽强度
    double shadeRadius = 60.0;             // 遮蔽半径
    double shadeSigma = 30.0;              // 高斯衰减宽度
    // R3-A 捕食避难所标定：节点保留 15（=15% cap）不可捕食储备，生产者
    // 不会被吃绝（捕食者-猎物稳定共存条件；CyanoParams 模块默认 0 逐位
    // 保守，标定值经构造期/applyConfig 镜像生效）。
    double grazeRefugeEnergy = 15.0;
};

struct SimConfig {
    WorldConfig world;
    SunConfig sun;             // R1-B：可配置阳光（替代硬编码昼夜正弦）
    // ---- 蓝细菌库存（R1-B）：初始节点数与硬上限。字段直接放 SimConfig
    //（CLI 直写、组合根构造期一次拷贝进 CyanoParams）。----
    std::size_t cyanoNodeCount = 512;  // 初始蓝细菌节点数（R10：128→512 随世界再 4 倍）
    // R3-A：性能护栏从 64 提到 256——64 曾同时是生态天花板（分裂触顶后光合
    // 注入与密度无关，能量持续膨胀）。自遮光（CyanoParams shade 三参数）接管
    // 承载力后，护栏只防 O(n²) 的 linkTransfer 失控，生态平衡应停在护栏之下。
    std::size_t cyanoMaxNodes = 4096;  // 节点数硬上限（R10：1024→4096 随世界再 4 倍；蓝细菌是轻量节点）
    PolicyConfig policy;
    // ---- 追加成员（F2）----
    AgentConfig agent;
    ColonyConfig colony;
    // ---- 追加成员（温度场）----
    TemperatureConfig temperature{};
    // ---- 追加成员（地形）：默认全 0，不生成任何地貌 ----
    TerrainConfig terrain{};
    // ---- R3-B（实时调控接线）新增：运行时键值落点（默认值 = 现行真实值，回归逐位安全）----
    RuntimeTuning runtime{};   // max_fps / render_interval / light_warmth / cyano_enabled 等
    NucleusTuning nucleus{};   // pressure + nucleus 组（basal_cost 等经 applyConfig 进 bp_）
    BallTuning ball{};         // food 组球能量键 + ball 组力场键（R4 消费）
    CyanoTuning cyanoTuning{}; // producer + cyano 组 + light 组半饱和（applyConfig 直通 CyanoField）
};

} // namespace agentsim
