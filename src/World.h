#pragma once
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <random>
#include "Ball.h"
#include "Cyanobacteria.h"
#include "EnergySystem.h"
#include "EventBus.h"
#include "Nucleus.h"
#include "SpatialGrid.h"

// 世界/自然环境运行参数（可由环境配置文件与命令行覆盖）。
struct WorldConfig {
    // ---- 世界与运行 -------
    double width = 2000.0;
    double height = 2000.0;
    int seed = 42;
    int initialBalls = 1000;
    int initialNuclei = 20;
    int maxFrames = 100000;
    int renderInterval = 10;
    int sampleInterval = 0;
    int trendInterval = 100;
    double maxFps = 0.0;   // 每秒模拟帧数上限；0 = 不限速（跑满 CPU）
    bool visualization = true;  // 控制台可视化总开关（false 时即使 renderInterval>0 也不渲染）
    bool clearScreen = true;     // 每次渲染前是否清屏（false 则顺序滚动输出，兼容无法清屏的终端）
    int gridCols = 80;           // 渲染字符网格列数
    int gridRows = 40;           // 渲染字符网格行数
    bool lockConsole = true;     // 启动时锁定控制台窗口为合适大小
    bool pauseOnExit = false;    // 结束后等待按键（双击运行时防止窗口闪退）

    // ---- 环境与资源 -------
    int maxNuclei = 0;                    // 环境承载力；<=0 表示按世界面积自动计算
    double territoryPerNucleus = 4000.0;  // 承载力估算时每核占用的世界面积
    double absorbRadius = 30.0;           // 核吸收小球的半径
    double ballSpawnProbability = 0.01;   // 每帧随机补充自由小球的概率
    double baseBallEnergy = 1.0;          // 吸收单个小球的基础能量
    double gridCellSize = 60.0;           // 空间哈希网格单元大小

    // ---- 蓝细菌（环境与资源）----
    int    initialCyanoNodes = 0;                 // cyano_nodes          初始节点总数（0=关闭）
    int    cyanoColonies = 4;                     // cyano_colonies       初始聚落数
    double cyanoInitialEnergy = 10.0;             // cyano_initial_energy 节点初始能量
    double cyanoLinkDistance = 80.0;              // cyano_link_distance  连接距离
    int    cyanoMaxLinks = 6;                     // cyano_max_links      每节点连接上限
    int    cyanoLinkCheckInterval = 4;            // cyano_link_check_interval 连接维护间隔帧
    double cyanoPhotosynthesisRate = 0.02;        // cyano_photosynthesis_rate
    double cyanoLightHalfSaturation = 0.5;        // cyano_light_half_saturation
    double cyanoMetabolismRate = 0.005;           // cyano_metabolism_rate
    double cyanoTransferRate = 0.05;              // cyano_transfer_rate
    double cyanoTransferDeadband = 0.05;          // cyano_transfer_deadband  能量传输死区（差值不超过它就不传输，防高频微额往复）
    double cyanoBallSpawnCost = 8.0;              // cyano_ball_spawn_cost
    double cyanoBallSpawnProbability = 0.002;     // cyano_ball_spawn_probability
    double cyanoReproductionCost = 20.0;          // cyano_reproduction_cost
    double cyanoReproductionProbability = 0.001;  // cyano_reproduction_probability
    double cyanoReproductionOffset = 25.0;        // cyano_reproduction_offset
    double cyanoMinNodeSeparation = 15.0;         // cyano_min_node_separation
    double cyanoPredationRadius = 30.0;           // cyano_predation_radius  核吞节点的判定半径
    int    cyanoConversionMode = 1;               // cyano_conversion_mode   0=常数 1=速度×加速度反比 2=幂律 3=仅加速度
    double cyanoConversionRate = 1.0;             // cyano_conversion_rate
    double cyanoConversionExponent = 1.0;         // cyano_conversion_exponent
    double cyanoConversionSpeedRef = 60.0;        // cyano_conversion_speed_ref
    double cyanoConversionAccelRef = 3.0;         // cyano_conversion_accel_ref
    double cyanoConversionWSpeed = 1.0;           // cyano_conversion_w_speed
    double cyanoConversionWAccel = 1.0;           // cyano_conversion_w_accel
    double cyanoConversionMinRate = 0.001;        // cyano_conversion_min_rate
    double cyanoConversionMaxRate = 10.0;         // cyano_conversion_max_rate
    double cyanoHueJitter = 8.0;                  // cyano_hue_jitter        子核色相变异幅度（度）

    // ---- 阳光（昼夜 + 垂直梯度 + 局部扰动）----
    double sunBaseLevel = 1.0;                    // sun_base_level
    double sunDayAmplitude = 0.7;                 // sun_day_amplitude
    double sunDayPeriodFrames = 1200.0;           // sun_day_period_frames
    double sunVerticalGradient = 0.3;             // sun_vertical_gradient
    double sunNoiseAmplitude = 0.1;               // sun_noise_amplitude
    double sunNoiseFrequency = 0.01;              // sun_noise_frequency
    double sunNoiseSpeed = 0.002;                 // sun_noise_speed

    // ---- 蓝细菌可视化 ----
    bool   cyanoRenderLinks = true;               // cyano_render_links  控制台是否画节点连线
    int    cyanoRenderChar = 0;                   // cyano_render_char   0='*' 1='o' 2='+'（避免与 n/N/s/w/c 冲突）

    // ---- 小球动力学 -------
    double ballDamping = 0.90;                 // 小球速度阻尼
    double radialK = 0.03;                     // 轨道径向弹簧系数
    double tangentialK = 2.0;                  // 切向（环绕）驱动系数
    double nearRepelRadius = 25.0;             // 小球近核排斥触发半径
    double ballBallRepelRadius = 10.0;         // 小球间短程排斥半径
    double ballBallRepelK = 1.2;
    double ballSameOwnerAttractRadius = 40.0;  // 同核小球吸引半径
    double ballSameOwnerAttractK = 0.15;
    double ballMaxSpeedShield = 25.0;          // 护盾球速度上限
    double ballMaxSpeedWorker = 50.0;          // 资源球速度上限
    double ballMaxSpeedScout = 80.0;           // 侦察球速度上限
    double ballVelRange = 2.0;                 // 初始/补充小球速度随机范围（±该值）
    double bounceRestitution = 0.8;            // 边界反弹恢复系数
    double followBoost = 1.5;                  // 跟随加速：归属球限速 = max(类型上限, 核速×该值)；0 关闭
    double followCoupling = 15.0;               // 跟随耦合：归属核轨道弹簧加强倍数（越大越甩不掉球，0 关闭）
    double detachRadius = 400.0;               // 归属球距核超过该距离即脱附
    double ballLossCost = 5.0;                 // 核失去一个归属球（脱附/被抢）扣除的能量

    // ---- 核动力学 -------
    double nucleusDamping = 0.85;   // 核速度阻尼
    double attackK = 0.5;           // 攻击追逐加速度系数
    double avoidK = 0.6;            // 避让加速度系数
    double combatRate = 0.05;       // 争斗能量转移速率
    double influenceMargin = 20.0;  // 核力场查询半径的额外余量
    double forageK = 1.0;           // 核朝小球觅食的加速度系数
    double forageRadius = 300.0;    // 核觅食的小球查询半径
    double wanderK = 2.0;           // 核随机游走（探索）加速度系数
    double wanderTurnRate = 0.1;     // 随机游走方向每帧最大转向角（弧度），小=路径直，大=原地抖
    double basalCost = 0.01;       // 基础代谢：每帧固定能量流逝（v=0 也不为 0）
    double speedCostK = 0.0001;     // 速度代价系数
    double speedCostExponent = 2.0; // 速度代价指数（>1 为超线性增长）
    double reproductionCostRatio = 0.8; // 繁殖代价：父核消耗 = ratio * energyThreshold
    double childEnergyRatio = 0.5;  // 子核初始能量 = ratio * energyThreshold
    double nucleusInitEnergyRatio = 0.7; // 初始随机核能量 = ratio * energyThreshold
    double reproductionOffset = 20.0;    // 繁殖时子核位置随机偏移（±该值）
    double nucleusMinSeparation = 40.0;  // 核最小间距：小于该距离施强斥力且跳过攻击
    double nucleusRepelK = 4.0;          // 核贴脸斥力刚度（线性弹簧：F = k × 重叠量）
    int newbornGraceFrames = 30;         // 新核保护期帧数（期内不攻击、不被攻击）

    // ---- 遗传参数范围（随机初始化与变异裁剪）----
    ParamRanges ranges;
};

// 世界：持有小球与核，并实现每帧的模拟规则。
class World {
public:
    explicit World(const WorldConfig& cfg);

    // 随机初始化小球与核。
    void initialize();

    // 手动注入一个核（在随机初始化之后加入）。
    void addNucleus(const Vec2& pos, double energy, const NucleusParams& params);

    // 推进一帧。
    void step();

    const WorldConfig& config() const { return config_; }
    int frame() const { return frame_; }
    const std::vector<Ball>& balls() const { return balls_; }
    const std::vector<Nucleus>& nuclei() const { return nuclei_; }

    // 非 const 版本仅供 ModAPI 使用：mod 约定只通过 EnergySystem::apply 改能量，
    // 不得直接修改其它字段（位置/速度/参数等）。
    std::vector<Nucleus>& nuclei() { return nuclei_; }

    int aliveNucleusCount() const;
    double averageNucleusEnergy() const;

    // ---- 蓝细菌（WS4 接线）----
    // 生产者群落只读访问（WS6a 快照取数 / Sampler / 渲染使用）。
    const Cyanobacteria& cyano() const { return cyano_; }
    // 当前世界中心处的阳光值（>= 0），供快照头与渲染显示昼夜与光照强度。
    double sunLevel() const;

    // ---- 蓝细菌 mod 化接缝（供 mods/cyanobacteria.cpp 驱动）----
    // 只暴露"驱动群落"所必需的最小能力，World 仍是状态与不变量的唯一持有者。
    // 群落可写访问：mod 每帧驱动 step 用；关闭（initialCyanoNodes<=0）时群落恒空。
    Cyanobacteria& cyanoMutable() { return cyano_; }
    // 阳光参数：mod 调 Cyanobacteria::step() 时需要（构造期由 WorldConfig 派生、此后只读）。
    const SunParams& sunParams() const { return sun_; }
    // 添加自由小球：mod 唯一合法的加球通道。
    // 必须经由 World 执行——prevOwner_ 是与 balls_ 并行的私有数组，
    // mod 直接 push_back 会导致两数组错位。
    void addFreeBall(const Vec2& pos, const Vec2& vel, BallType type);
    // 蓝细菌是否启用：等价于配置 initialCyanoNodes > 0（构造期已知，不依赖运行时状态，
    // 以保证"关闭时与基线逐字节一致"的确定性红线不被破坏）。
    bool cyanoEnabled() const { return config_.initialCyanoNodes > 0; }
    // 外部驱动开关：mods/cyanobacteria.cpp 注册时置 true，此后由 mod 在 FRAME_START
    // 驱动群落，World::updateCyanobacteria() 主动让位，**避免双重驱动**。
    // 默认 false = World 内部驱动（与 mod 化之前的行为完全一致）。
    void setCyanoExternalDriver(bool v) { cyanoExternalDriver_ = v; }
    bool cyanoExternalDriver() const { return cyanoExternalDriver_; }

    bool finished() const { return finished_; }
    const char* finishReason() const { return finishReason_; }

    // 提前终止（启动器"停止"按钮）：标记结束，原因可自定义。
    void stopNow(const char* reason) {
        if (!finished_) {
            finished_ = true;
            finishReason_ = reason;
        }
    }

    // ---- 存档（Phase D）----
    // 把完整世界状态（配置 + rng + 帧号 + 球 + 核）写入流；失败返回 false。
    bool writeState(std::ostream& os) const;
    // 从流恢复状态（配置键值按 ParamSchema 解析，未知键忽略以保持向前兼容）。
    // 恢复后重建空间网格、finished 复位。失败时 err 给出原因。
    bool readState(std::istream& is, std::string& err);

    std::mt19937& rng() { return rng_; }
    EnergySystem& energy() { return energy_; }
    EventBus& events() { return events_; }

private:
    void buildGrid();
    void updateBallDynamics();
    void updateOwnership();
    void absorbBalls();
    void nucleusCombat();
    void nucleusMovement();
    void reproduce();
    void removeDeadNuclei();
    void replenishBalls();
    void checkFinish();
    void spawnFreeBall();
    double influenceRadius() const;
    void updateCyanobacteria();   // 蓝细菌每帧更新 + 产出自由小球入库

    WorldConfig config_;
    std::vector<Ball> balls_;
    std::vector<int> prevOwner_;  // 每球上一帧归属（与 balls_ 并行，随增删同步）
    std::vector<Nucleus> nuclei_;
    SpatialGrid grid_;
    std::mt19937 rng_;
    SunParams sun_;        // 阳光模型参数（由 WorldConfig 派生，构造期固定）
    Cyanobacteria cyano_;  // 生产者群落（值成员；initialCyanoNodes<=0 时恒为空 → 全链路零开销）
    // 核色相专用 RNG：**绝不消耗主 rng_**，保证开启可视化不影响模拟可复现性。
    std::mt19937 hueRng_;
    EventBus events_;              // 事件总线（mod 订阅入口）
    EnergySystem energy_{events_}; // 能量唯一通道（依赖 events_，声明顺序不可换）
    int frame_ = 0;
    int nextNucleusId_ = 0;
    double frameAbsorbEnergy_ = 0.0;  // 本帧吸收总能量（BALL_ABSORBED 聚合事件）
    int frameAbsorbCount_ = 0;        // 本帧吸收小球数
    int frameCyanoCount_ = 0;         // 本帧捕食蓝细菌节点数
    // mods/cyanobacteria.cpp 接管驱动时置 true（见 setCyanoExternalDriver）。
    bool cyanoExternalDriver_ = false;
    bool finished_ = false;
    const char* finishReason_ = "";
};
