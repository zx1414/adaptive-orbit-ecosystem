#pragma once
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <random>
#include "Ball.h"
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

    WorldConfig config_;
    std::vector<Ball> balls_;
    std::vector<int> prevOwner_;  // 每球上一帧归属（与 balls_ 并行，随增删同步）
    std::vector<Nucleus> nuclei_;
    SpatialGrid grid_;
    std::mt19937 rng_;
    EventBus events_;              // 事件总线（mod 订阅入口）
    EnergySystem energy_{events_}; // 能量唯一通道（依赖 events_，声明顺序不可换）
    int frame_ = 0;
    int nextNucleusId_ = 0;
    double frameAbsorbEnergy_ = 0.0;  // 本帧吸收总能量（BALL_ABSORBED 聚合事件）
    int frameAbsorbCount_ = 0;        // 本帧吸收小球数
    bool finished_ = false;
    const char* finishReason_ = "";
};
