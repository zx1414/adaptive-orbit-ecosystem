#include "World.h"
#include "ParamSchema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>

namespace {
constexpr double TWO_PI = 6.28318530717958647692;

void bounce(Vec2& pos, Vec2& vel, double w, double h, double restitution) {
    if (pos.x < 0.0) { pos.x = -pos.x; vel.x = -vel.x * restitution; }
    if (pos.x > w)   { pos.x = 2.0 * w - pos.x; vel.x = -vel.x * restitution; }
    if (pos.y < 0.0) { pos.y = -pos.y; vel.y = -vel.y * restitution; }
    if (pos.y > h)   { pos.y = 2.0 * h - pos.y; vel.y = -vel.y * restitution; }
    pos.x = std::clamp(pos.x, 0.0, w);
    pos.y = std::clamp(pos.y, 0.0, h);
}

double randRange(std::mt19937& rng, double lo, double hi) {
    std::uniform_real_distribution<double> d(lo, hi);
    return d(rng);
}

// WorldConfig（环境配置键） → CyanoParams（蓝细菌模块 POD）。
CyanoParams makeCyanoParams(const WorldConfig& c) {
    CyanoParams p;
    p.photosynthesisRate = c.cyanoPhotosynthesisRate;
    p.lightHalfSaturation = c.cyanoLightHalfSaturation;
    p.metabolismRate = c.cyanoMetabolismRate;
    p.linkDistance = c.cyanoLinkDistance;
    p.maxLinksPerNode = c.cyanoMaxLinks;
    p.linkCheckInterval = c.cyanoLinkCheckInterval;
    p.transferRate = c.cyanoTransferRate;
    p.transferDeadband = c.cyanoTransferDeadband;
    p.ballSpawnCost = c.cyanoBallSpawnCost;
    p.ballSpawnProbability = c.cyanoBallSpawnProbability;
    p.reproductionCost = c.cyanoReproductionCost;
    p.reproductionProbability = c.cyanoReproductionProbability;
    p.reproductionOffset = c.cyanoReproductionOffset;
    p.minNodeSeparation = c.cyanoMinNodeSeparation;
    return p;
}

// WorldConfig（环境配置键） → SunParams（阳光模型 POD）。
SunParams makeSunParams(const WorldConfig& c) {
    SunParams s;
    s.baseLevel = c.sunBaseLevel;
    s.dayAmplitude = c.sunDayAmplitude;
    s.dayPeriodFrames = c.sunDayPeriodFrames;
    s.verticalGradient = c.sunVerticalGradient;
    s.noiseAmplitude = c.sunNoiseAmplitude;
    s.noiseFrequency = c.sunNoiseFrequency;
    s.noiseSpeed = c.sunNoiseSpeed;
    return s;
}

// WorldConfig（环境配置键） → CyanoConversion（捕食转化率 POD）。
CyanoConversion makeCyanoConversion(const WorldConfig& c) {
    CyanoConversion v;
    v.mode = c.cyanoConversionMode;
    v.rate = c.cyanoConversionRate;
    v.exponent = c.cyanoConversionExponent;
    v.speedRef = c.cyanoConversionSpeedRef;
    v.accelRef = c.cyanoConversionAccelRef;
    v.wSpeed = c.cyanoConversionWSpeed;
    v.wAccel = c.cyanoConversionWAccel;
    v.minRate = c.cyanoConversionMinRate;
    v.maxRate = c.cyanoConversionMaxRate;
    return v;
}
}  // namespace

World::World(const WorldConfig& cfg)
    : config_(cfg),
      grid_(cfg.width, cfg.height, cfg.gridCellSize),
      rng_(cfg.seed),
      sun_(makeSunParams(cfg)),
      // 蓝细菌在构造期构造：initialCyanoNodes<=0 时 initialize() 不被调用，
      // 群落恒为空 → 后续每帧全链路零开销。
      cyano_(cfg.width, cfg.height, makeCyanoParams(cfg)),
      // 色相专用 RNG：与主 rng_ 完全隔离，色相随机绝不推进模拟随机流（验收 #13）。
      hueRng_(static_cast<uint32_t>(cfg.seed) * 2654435761u + 1013904223u) {
    // 未显式指定承载力时，按世界面积估算一个合理上限，防止核种群无限增长。
    if (config_.maxNuclei <= 0) {
        config_.maxNuclei = std::max(1, (int)(config_.width * config_.height / config_.territoryPerNucleus));
        config_.maxNuclei = std::max(config_.maxNuclei, config_.initialNuclei);
    }
    // 开启蓝细菌时立刻撒点；关闭时保持空群落（零开销短路）。
    if (config_.initialCyanoNodes > 0) {
        cyano_.initialize(rng_, config_.initialCyanoNodes, config_.cyanoColonies,
                          config_.cyanoInitialEnergy);
    }
}

double World::sunLevel() const {
    // 世界中心处的阳光值（昼夜 + 垂直梯度 + 局部扰动的完整解），供快照/渲染显示。
    return sunLevelAt(Vec2(config_.width * 0.5, config_.height * 0.5), frame_, sun_,
                      config_.width, config_.height);
}

void World::addFreeBall(const Vec2& pos, const Vec2& vel, BallType type) {
    // mod 产球的唯一入库通道：不归属任何核（ownerId=-1），
    // 并同步 prevOwner_ 并行数组（与 balls_ 等长，错位会导致归属判定越界）。
    Ball b;
    b.pos = pos;
    b.vel = vel;
    b.type = type;
    b.ownerId = -1;
    balls_.push_back(b);
    prevOwner_.push_back(-1);
}

void World::initialize() {
    balls_.clear();
    prevOwner_.clear();
    nuclei_.clear();

    // 初始小球：均匀分布、小速度、类型均匀随机。
    balls_.reserve((size_t)config_.initialBalls);
    std::uniform_int_distribution<int> typeDist(0, BALL_TYPE_COUNT - 1);
    for (int i = 0; i < config_.initialBalls; ++i) {
        Ball b;
        b.pos.x = randRange(rng_, 0.0, config_.width);
        b.pos.y = randRange(rng_, 0.0, config_.height);
        b.vel.x = randRange(rng_, -config_.ballVelRange, config_.ballVelRange);
        b.vel.y = randRange(rng_, -config_.ballVelRange, config_.ballVelRange);
        b.type = static_cast<BallType>(typeDist(rng_));
        b.ownerId = -1;
        balls_.push_back(b);
        prevOwner_.push_back(-1);
    }

    // 初始核：随机参数，能量设为繁殖阈值以下，需通过吸收小球积累能量。
    nuclei_.reserve((size_t)config_.initialNuclei);
    for (int i = 0; i < config_.initialNuclei; ++i) {
        Nucleus n;
        n.params.randomize(rng_, config_.ranges, config_.initialCyanoNodes > 0);
        n.pos.x = randRange(rng_, 0.0, config_.width);
        n.pos.y = randRange(rng_, 0.0, config_.height);
        n.vel = Vec2(0.0, 0.0);
        n.energy = n.params.energyThreshold * config_.nucleusInitEnergyRatio;
        n.alive = true;
        n.id = nextNucleusId_++;
        n.wanderAngle = randRange(rng_, 0.0, TWO_PI);
        // 色相只用 hueRng_：不推进主 rng_，保证开启/关闭可视化不改变模拟轨迹。
        {
            std::uniform_real_distribution<double> hueDist(0.0, 360.0);
            n.hue = hueDist(hueRng_);
        }
        nuclei_.push_back(n);
    }
    // 环境状态：开局存活峰值按实际核数记录（之后由 tickEnvironment 每帧刷新）。
    envState_.peakAlive = std::max(envState_.peakAlive, (int)nuclei_.size());
}

void World::addNucleus(const Vec2& pos, double energy, const NucleusParams& params) {
    Nucleus n(pos, energy, params);
    n.alive = true;
    n.id = nextNucleusId_++;
    n.wanderAngle = randRange(rng_, 0.0, TWO_PI);
    {
        std::uniform_real_distribution<double> hueDist(0.0, 360.0);
        n.hue = hueDist(hueRng_);
    }
    nuclei_.push_back(n);
}

void World::updateCyanobacteria() {
    // 关闭蓝细菌（initialCyanoNodes<=0 → 群落恒空）：**零调用**，
    // 保住 Cyanobacteria::step() 的 rng 短路 → 关闭时与基线模拟完全一致（验收 #13）。
    if (config_.initialCyanoNodes <= 0) return;

    // 外部驱动点：mods/cyano_producer.cpp 订阅 CYANO_TICK 后在此接管。
    // 必须放在这里——与内部驱动的调用位置、frame_ 取值、以及"早于 buildGrid()"
    // 的时序完全一致，mod 驱动才会与内部驱动逐帧等价。
    // 无监听者时 emit 为空操作，不影响默认路径。
    if (cyanoExternalDriver_) {
        SimEvent e;
        e.type = EventType::CYANO_TICK;
        e.frame = frame_;
        events_.emit(e);
        return;
    }

    if (cyano_.empty()) return;

    static thread_local std::vector<CyanoBall> spawned;  // 复用缓冲，避免每帧分配
    spawned.clear();
    cyano_.step(rng_, frame_, sun_, spawned);
    // 产出的自由小球：不归属任何核，追加进 balls_（prevOwner_ 同步维持并行数组）。
    for (size_t i = 0; i < spawned.size(); ++i) {
        Ball b;
        b.pos = spawned[i].pos;
        b.vel = spawned[i].vel;
        b.type = spawned[i].type;
        b.ownerId = -1;
        balls_.push_back(b);
        prevOwner_.push_back(-1);
    }
}

void World::step() {
    // 年龄递增（用于新生保护期等）。
    for (Nucleus& n : nuclei_) {
        if (n.alive) ++n.age;
    }
    // 蓝细菌更新：插在 ++age 之后、buildGrid() 之前
    // （本帧新产出的球在 buildGrid 时入网格，下一帧即可被吸收）。
    updateCyanobacteria();
    buildGrid();
    updateBallDynamics();
    updateOwnership();
    absorbBalls();
    // 吸收聚合事件：本帧有吸收则发射一次（value=总能量，extra=球数）。
    if (frameAbsorbCount_ > 0) {
        SimEvent e;
        e.type = EventType::BALL_ABSORBED;
        e.frame = frame_ + 1;
        e.value = frameAbsorbEnergy_;
        e.extra = &frameAbsorbCount_;
        events_.emit(e);
    }
    nucleusCombat();
    nucleusMovement();
    reproduce();
    removeDeadNuclei();
    replenishBalls();
    ++frame_;
    // 环境状态推进：影响力恢复 + 事件冷却递减 + 存活峰值（**不消耗 RNG**，
    // 因此没有玩家命令时与 v2.1.0 的模拟轨迹逐字节一致）。
    tickEnvironment();
    checkFinish();
    // 事件时机（固定顺序）：++frame 后发 FRAME_START，step 末尾发 FRAME_END。
    {
        SimEvent e;
        e.type = EventType::FRAME_START;
        e.frame = frame_;
        events_.emit(e);
    }
    {
        SimEvent e;
        e.type = EventType::FRAME_END;
        e.frame = frame_;
        events_.emit(e);
    }
}

void World::buildGrid() {
    grid_.clear();
    for (size_t i = 0; i < balls_.size(); ++i) {
        grid_.addBall((int)i, balls_[i].pos);
    }
    for (size_t i = 0; i < nuclei_.size(); ++i) {
        if (nuclei_[i].alive) grid_.addNucleus((int)i, nuclei_[i].pos);
    }
}

double World::influenceRadius() const {
    double r = 0.0;
    for (const Nucleus& n : nuclei_) {
        if (!n.alive) continue;
        for (int t = 0; t < BALL_TYPE_COUNT; ++t) {
            r = std::max(r, n.params.orbitRadius[t]);
        }
    }
    return r + config_.influenceMargin;
}

void World::updateBallDynamics() {
    const double infR = influenceRadius();
    const double infR2 = infR * infR;
    const double ballBallQueryR = std::max(config_.ballBallRepelRadius, config_.ballSameOwnerAttractRadius);
    const double ballBallQueryR2 = ballBallQueryR * ballBallQueryR;
    for (size_t i = 0; i < balls_.size(); ++i) {
        Ball& b = balls_[i];
        Vec2 acc(0.0, 0.0);

        // 核施加的力场：轨道径向弹簧 + 切向环绕驱动 + 近距排斥。
        int cx0, cx1, cy0, cy1;
        grid_.cellRange(b.pos.x - infR, b.pos.y - infR,
                        b.pos.x + infR, b.pos.y + infR, cx0, cx1, cy0, cy1);
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                for (int ni : grid_.nucleiAt(cx, cy)) {
                    const Nucleus& n = nuclei_[(size_t)ni];
                    if (!n.alive) continue;
                    Vec2 d = b.pos - n.pos;
                    double distSq = d.lengthSq();
                    if (distSq > infR2) continue;  // 圆形范围裁剪，跳过方格内但过远的核
                    double dist = std::sqrt(distSq);
                    if (dist < 1e-9) dist = 1e-9;
                    Vec2 dir = d / dist;           // 径向朝外
                    Vec2 tangent(-dir.y, dir.x);   // 切向（逆时针）

                    int t = static_cast<int>(b.type);
                    double aff = n.params.affinity[(size_t)t];
                    double orbit = n.params.orbitRadius[(size_t)t];

                    // 径向弹簧：偏离期望半径时拉回。
                    acc += dir * (-aff * (dist - orbit) * config_.radialK);
                    // 切向驱动：形成环绕运动。
                    acc += tangent * (aff * config_.tangentialK);
                    // 近距排斥：维持分层，防止小球贴核。
                    if (dist < config_.nearRepelRadius) {
                        double strength = n.params.repelStrength[(size_t)t] *
                                          (1.0 - dist / config_.nearRepelRadius);
                        acc += dir * strength;
                    }
                }
            }
        }

        // 跟随耦合：归属核的加强弹簧，作用范围直至脱附半径（超出普通力场也有效），
        // 抵消核加速时的甩球效应，让快核甩不掉自己的球群。
        if (config_.followCoupling > 0.0 && b.ownerId >= 0 && b.ownerId < (int)nuclei_.size()) {
            const Nucleus& owner = nuclei_[(size_t)b.ownerId];
            if (owner.alive) {
                Vec2 od = b.pos - owner.pos;
                double odSq = od.lengthSq();
                double det2 = config_.detachRadius * config_.detachRadius;
                if (odSq < det2 && odSq > 1e-12) {
                    double odist = std::sqrt(odSq);
                    Vec2 odir = od / odist;
                    int t = static_cast<int>(b.type);
                    double aff = owner.params.affinity[(size_t)t];
                    double orbit = owner.params.orbitRadius[(size_t)t];
                    acc += odir * (-aff * (odist - orbit) *
                                  (config_.radialK * config_.followCoupling));
                }
            }
        }

        // 小球间相互作用：短程排斥 + 同核吸引。
        int bx0, bx1, by0, by1;
        grid_.cellRange(b.pos.x - ballBallQueryR, b.pos.y - ballBallQueryR,
                        b.pos.x + ballBallQueryR, b.pos.y + ballBallQueryR,
                        bx0, bx1, by0, by1);
        for (int cy = by0; cy <= by1; ++cy) {
            for (int cx = bx0; cx <= bx1; ++cx) {
                for (int j : grid_.ballsAt(cx, cy)) {
                    if ((size_t)j == i) continue;
                    const Ball& o = balls_[(size_t)j];
                    Vec2 d = b.pos - o.pos;
                    double distSq = d.lengthSq();
                    if (distSq >= ballBallQueryR2) continue;  // 过远，跳过 sqrt
                    double dist = std::sqrt(distSq);
                    if (dist < 1e-9) continue;
                    Vec2 dir = d / dist;
                    if (dist < config_.ballBallRepelRadius) {
                        double strength = config_.ballBallRepelK * (1.0 - dist / config_.ballBallRepelRadius);
                        acc += dir * strength;  // 推开
                    } else if (dist < config_.ballSameOwnerAttractRadius &&
                               b.ownerId >= 0 && b.ownerId == o.ownerId) {
                        double strength = config_.ballSameOwnerAttractK *
                                          (1.0 - dist / config_.ballSameOwnerAttractRadius);
                        acc -= dir * strength;  // 同核吸引（拉近）
                    }
                }
            }
        }

        // 积分 + 阻尼 + 限速。
        b.vel += acc;
        b.vel *= config_.ballDamping;
        double maxV = config_.ballMaxSpeedWorker;
        switch (b.type) {
            case BallType::Shield: maxV = config_.ballMaxSpeedShield; break;
            case BallType::Worker: maxV = config_.ballMaxSpeedWorker; break;
            case BallType::Scout:  maxV = config_.ballMaxSpeedScout; break;
        }
        // 跟随加速：归属球限速 = max(类型上限, 其核当前速度 × follow_boost)。
        if (config_.followBoost > 0.0 && b.ownerId >= 0 && b.ownerId < (int)nuclei_.size()) {
            const Nucleus& owner = nuclei_[(size_t)b.ownerId];
            if (owner.alive) {
                double followV = owner.vel.length() * config_.followBoost;
                if (followV > maxV) maxV = followV;
            }
        }
        double spSq = b.vel.lengthSq();
        if (spSq > maxV * maxV) {
            b.vel = b.vel * (maxV / std::sqrt(spSq));
        }
        b.pos += b.vel;
        bounce(b.pos, b.vel, config_.width, config_.height, config_.bounceRestitution);
    }
}

void World::updateOwnership() {
    const double ownR = config_.detachRadius;
    for (size_t i = 0; i < balls_.size(); ++i) {
        Ball& b = balls_[i];
        int best = -1;
        double bestDistSq = ownR * ownR;
        int cx0, cx1, cy0, cy1;
        grid_.cellRange(b.pos.x - ownR, b.pos.y - ownR,
                        b.pos.x + ownR, b.pos.y + ownR, cx0, cx1, cy0, cy1);
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                for (int ni : grid_.nucleiAt(cx, cy)) {
                    const Nucleus& n = nuclei_[(size_t)ni];
                    if (!n.alive) continue;
                    Vec2 d = b.pos - n.pos;
                    double dsq = d.lengthSq();
                    if (dsq < bestDistSq) {
                        bestDistSq = dsq;
                        best = ni;
                    }
                }
            }
        }

        // 失球惩罚：上一帧有主、本帧换主或变自由，对失去方扣能量。
        // 被吃掉的球（吸收）不在此判定——它在 absorbBalls 里由同一主吸收，归属不变。
        int prev = prevOwner_[i];
        if (prev >= 0 && prev != best && prev < (int)nuclei_.size() && nuclei_[(size_t)prev].alive) {
            energy_.apply(nuclei_[(size_t)prev], -config_.ballLossCost, EnergyReason::BALL_LOSS);
        }

        b.ownerId = best;
        prevOwner_[i] = best;
    }
}

void World::absorbBalls() {
    const double r2 = config_.absorbRadius * config_.absorbRadius;
    frameAbsorbEnergy_ = 0.0;
    frameAbsorbCount_ = 0;
    frameCyanoCount_ = 0;

    // ---- 段 1：捕食蓝细菌节点（每核每帧最多吃 1 个）----
    // 独立于下方小球循环，语义清晰：判定半径用 cyanoPredationRadius，
    // 能量走 EnergyReason::CYANO_GAIN（与吃球的 ABSORB 分开统计）。
    if (config_.initialCyanoNodes > 0 && !cyano_.empty()) {
        const CyanoConversion conv = makeCyanoConversion(config_);
        const double predR = config_.cyanoPredationRadius;
        if (predR > 0.0) {
            // 无堆分配的固定缓冲（热路径：每个核每帧一次查询）。
            constexpr int kMaxNear = 8;
            int idxBuf[kMaxNear];
            double distBuf[kMaxNear];
            for (size_t i = 0; i < nuclei_.size(); ++i) {
                Nucleus& n = nuclei_[i];
                if (!n.alive) continue;
                // 偏好为最低值（0）→ 完全不捕食（验收 #9）。
                if (!(n.params.cyanoPreference > 0.0)) continue;
                const int found = cyano_.nodesNear(n.pos, predR, idxBuf, distBuf, kMaxNear);
                if (found <= 0) continue;

                // 取最近的一个节点吃掉。
                int best = 0;
                for (int k = 1; k < found; ++k) {
                    if (distBuf[k] < distBuf[best]) best = k;
                }
                const uint32_t nodeIdx = static_cast<uint32_t>(idxBuf[best]);
                const double rawEnergy = cyano_.consumeNode(nodeIdx);  // 只标死，同帧下标仍有效
                if (rawEnergy <= 0.0) continue;

                const double rate = cyanoConversionRate(
                    conv, n.params.maxSpeed, n.params.maxAccel, n.params.cyanoDigestion);
                // 转化率与"捕食意愿"共同决定实际获得的能量。
                const double gain = rawEnergy * rate * n.params.cyanoPreference;
                if (gain > 0.0) {
                    energy_.apply(n, gain, EnergyReason::CYANO_GAIN);
                    frameAbsorbEnergy_ += gain;
                    frameAbsorbCount_++;
                    frameCyanoCount_++;
                }
            }
        }
    }

    // ---- 段 2：原有小球吸收循环（一字未改）----
    for (size_t i = 0; i < balls_.size(); ) {
        Ball& b = balls_[i];
        bool absorbed = false;
        if (b.ownerId >= 0 && b.ownerId < (int)nuclei_.size()) {
            Nucleus& n = nuclei_[(size_t)b.ownerId];
            if (n.alive) {
                Vec2 d = b.pos - n.pos;
                if (d.lengthSq() < r2) {
                    double gain = config_.baseBallEnergy *
                                  n.params.absorbPreference[(size_t)b.type];
                    energy_.apply(n, gain, EnergyReason::ABSORB);
                    frameAbsorbEnergy_ += gain;
                    ++frameAbsorbCount_;
                    absorbed = true;
                }
            }
        }
        if (absorbed) {
            // swap-pop 移除，并继续处理换到当前位置的小球。
            balls_[i] = balls_.back();
            balls_.pop_back();
            prevOwner_[i] = prevOwner_.back();
            prevOwner_.pop_back();
        } else {
            ++i;
        }
    }
}

void World::nucleusCombat() {
    for (size_t i = 0; i < nuclei_.size(); ++i) {
        Nucleus& a = nuclei_[i];
        if (!a.alive) continue;
        double rangeA = a.params.attackRange;
        int cx0, cx1, cy0, cy1;
        grid_.cellRange(a.pos.x - rangeA, a.pos.y - rangeA,
                        a.pos.x + rangeA, a.pos.y + rangeA, cx0, cx1, cy0, cy1);
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                for (int nj : grid_.nucleiAt(cx, cy)) {
                    if ((size_t)nj <= i) continue;  // 每对只处理一次
                    Nucleus& b = nuclei_[(size_t)nj];
                    if (!b.alive) continue;
                    Vec2 d = a.pos - b.pos;
                    double dist = d.length();
                    if (dist < config_.nucleusMinSeparation) continue;  // 贴脸不攻击（斥力在 movement 处理）
                    if (a.age < config_.newbornGraceFrames || b.age < config_.newbornGraceFrames) continue;  // 新生保护期
                    double range = std::min(rangeA, b.params.attackRange);
                    if (dist > range) continue;

                    // 攻击强度高者吸取低者能量（死亡判定由 EnergySystem 统一处理）。
                    double delta = (a.params.attackStrength - b.params.attackStrength) * config_.combatRate;
                    energy_.apply(a, delta, EnergyReason::COMBAT);
                    energy_.apply(b, -delta, EnergyReason::COMBAT);
                }
            }
        }
    }
}

void World::nucleusMovement() {
    // 两阶段更新：先用帧首位置统一计算所有核的加速度，再统一积分。
    // 避免顺序更新导致后处理的核看到已移动的邻居位置（对称性/稳定性修复）。
    std::vector<Vec2> accs(nuclei_.size(), Vec2(0.0, 0.0));
    for (size_t i = 0; i < nuclei_.size(); ++i) {
        Nucleus& n = nuclei_[i];
        if (!n.alive) continue;
        Vec2& acc = accs[i];
        double queryR = std::max(n.params.attackRange, n.params.avoidRange);
        int cx0, cx1, cy0, cy1;
        grid_.cellRange(n.pos.x - queryR, n.pos.y - queryR,
                        n.pos.x + queryR, n.pos.y + queryR, cx0, cx1, cy0, cy1);
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                for (int nj : grid_.nucleiAt(cx, cy)) {
                    if ((size_t)nj == i) continue;
                    const Nucleus& o = nuclei_[(size_t)nj];
                    if (!o.alive) continue;
                    Vec2 d = o.pos - n.pos;
                    double dist = d.length();
                    if (dist < 1e-9) continue;
                    Vec2 dir = d / dist;  // 指向对方
                    if (dist < config_.nucleusMinSeparation) {
                        // 贴脸强斥力（线性弹簧，随重叠量增大），并跳过攻击/避让。
                        double overlap = config_.nucleusMinSeparation - dist;
                        double strength = config_.nucleusRepelK * overlap;
                        acc -= dir * strength;
                    } else if (dist < n.params.attackRange &&
                               n.age >= config_.newbornGraceFrames &&
                               o.age >= config_.newbornGraceFrames &&
                               n.params.attackStrength > o.params.attackStrength) {
                        // 趋利：只追击比自己弱的对手（威胁评估）。
                        double strength = n.params.attackStrength * config_.attackK *
                                          (1.0 - dist / n.params.attackRange);
                        acc += dir * strength;
                    } else if (dist < n.params.avoidRange &&
                               n.params.attackStrength < o.params.attackStrength) {
                        // 避害：躲避比自己强的对手（新生保护期内也可逃跑）。
                        double strength = n.params.avoidStrength * config_.avoidK *
                                          (1.0 - dist / n.params.avoidRange);
                        acc -= dir * strength;
                    }
                }
            }
        }

        // 觅食：核主动朝附近小球移动（按吸收偏好加权），没有邻居也会去追食物。
        if (config_.forageK > 0.0) {
            const double fr2 = config_.forageRadius * config_.forageRadius;
            int fx0, fx1, fy0, fy1;
            grid_.cellRange(n.pos.x - config_.forageRadius, n.pos.y - config_.forageRadius,
                            n.pos.x + config_.forageRadius, n.pos.y + config_.forageRadius,
                            fx0, fx1, fy0, fy1);
            for (int cy = fy0; cy <= fy1; ++cy) {
                for (int cx = fx0; cx <= fx1; ++cx) {
                    for (int j : grid_.ballsAt(cx, cy)) {
                        const Ball& b = balls_[(size_t)j];
                        Vec2 d = b.pos - n.pos;
                        double distSq = d.lengthSq();
                        if (distSq > fr2 || distSq < 1e-9) continue;
                        double dist = std::sqrt(distSq);
                        Vec2 dir = d / dist;
                        double w = n.params.absorbPreference[(size_t)b.type];
                        double strength = config_.forageK * w * (1.0 - dist / config_.forageRadius);
                        acc += dir * strength;
                    }
                }
            }
        }

        // 随机游走（探索）：方向持久、缓慢转向，形成连贯探索路径而非原地震颤。
        if (config_.wanderK > 0.0) {
            n.wanderAngle += randRange(rng_, -config_.wanderTurnRate, config_.wanderTurnRate);
            acc += Vec2(std::cos(n.wanderAngle), std::sin(n.wanderAngle)) * config_.wanderK;
        }

    }

    // Pass 2：统一积分（速度/位置/代谢）。
    for (size_t i = 0; i < nuclei_.size(); ++i) {
        Nucleus& n = nuclei_[i];
        if (!n.alive) continue;
        Vec2& acc = accs[i];

        n.vel += acc;
        n.vel *= config_.nucleusDamping;
        double maxV = n.params.maxSpeed;
        double spSq = n.vel.lengthSq();
        if (spSq > maxV * maxV) {
            n.vel = n.vel * (maxV / std::sqrt(spSq));
        }

        // 代谢消耗：基础项（v=0 也流逝）+ 速度非线性项（超线性增长）。
        {
            double v = n.vel.length();
            double loss = config_.basalCost +
                          config_.speedCostK * std::pow(v, config_.speedCostExponent);
            energy_.apply(n, -loss, EnergyReason::METABOLISM);
        }

        n.pos += n.vel;
        bounce(n.pos, n.vel, config_.width, config_.height, config_.bounceRestitution);
    }

    // Pass 3：硬分离，保证核间距 >= nucleus_min_separation（消除重叠与抖动）。
    if (config_.nucleusMinSeparation > 0.0) {
        const double sep = config_.nucleusMinSeparation;
        const double sep2 = sep * sep;
        for (size_t i = 0; i < nuclei_.size(); ++i) {
            Nucleus& a = nuclei_[i];
            if (!a.alive) continue;
            for (size_t j = i + 1; j < nuclei_.size(); ++j) {
                Nucleus& b = nuclei_[j];
                if (!b.alive) continue;
                Vec2 d = b.pos - a.pos;
                double distSq = d.lengthSq();
                if (distSq >= sep2) continue;
                double dist = std::sqrt(distSq);
                Vec2 dir;
                if (dist < 1e-9) {
                    double ang = randRange(rng_, 0.0, TWO_PI);
                    dir = Vec2(std::cos(ang), std::sin(ang));
                } else {
                    dir = d / dist;
                }
                double overlap = sep - dist;
                a.pos -= dir * (overlap * 0.5);
                b.pos += dir * (overlap * 0.5);
                // 沿法向衰减接近速度，防止继续对冲。
                double relv = (b.vel - a.vel).dot(dir);
                if (relv < 0.0) {
                    a.vel += dir * (relv * 0.5);
                    b.vel -= dir * (relv * 0.5);
                }
            }
        }
    }
}

void World::reproduce() {
    size_t count = nuclei_.size();  // 仅处理繁殖前已存在的核
    for (size_t i = 0; i < count; ++i) {
        Nucleus& n = nuclei_[i];
        if (!n.alive) continue;
        if (n.energy < n.params.energyThreshold) continue;
        if (nuclei_.size() >= (size_t)config_.maxNuclei) break;  // 达到环境承载力

        double threshold = n.params.energyThreshold;
        energy_.apply(n, -threshold * config_.reproductionCostRatio, EnergyReason::REPRODUCTION);

        Nucleus child = n;
        child.age = 0;  // 新生核从 0 岁开始（保护期）
        child.params.mutate(rng_, n.params.mutationRate, config_.ranges,
                            config_.initialCyanoNodes > 0);
        child.pos.x += randRange(rng_, -config_.reproductionOffset, config_.reproductionOffset);
        child.pos.y += randRange(rng_, -config_.reproductionOffset, config_.reproductionOffset);
        child.pos.x = std::clamp(child.pos.x, 0.0, config_.width);
        child.pos.y = std::clamp(child.pos.y, 0.0, config_.height);
        child.vel = Vec2(0.0, 0.0);
        child.energy = threshold * config_.childEnergyRatio;  // 初始能量直接赋值（不触发死亡判定）
        child.alive = true;
        child.id = nextNucleusId_++;
        // 色相遗传：父 + 小幅高斯扰动，环绕到 [0,360)。只用 hueRng_，不动主 rng_。
        {
            std::normal_distribution<double> jitter(0.0, config_.cyanoHueJitter);
            double h = n.hue + jitter(hueRng_);
            h = std::fmod(h, 360.0);
            if (h < 0.0) h += 360.0;
            child.hue = h;
        }
        nuclei_.push_back(child);
        {
            SimEvent e;
            e.type = EventType::NUCLEUS_BORN;
            e.frame = frame_ + 1;
            e.nucleus = &nuclei_.back();
            events_.emit(e);
        }
    }
}

void World::removeDeadNuclei() {
    for (size_t i = 0; i < nuclei_.size(); ) {
        if (nuclei_[i].alive) {
            ++i;
        } else {
            nuclei_[i] = nuclei_.back();
            nuclei_.pop_back();
        }
    }
}

void World::replenishBalls() {
    std::bernoulli_distribution spawn(config_.ballSpawnProbability);
    if (spawn(rng_)) {
        spawnFreeBall();
    }
    // 维持小球总数不低于初始值。
    int deficit = config_.initialBalls - (int)balls_.size();
    for (int k = 0; k < deficit; ++k) {
        spawnFreeBall();
    }
}

void World::spawnFreeBall() {
    Ball b;
    b.pos.x = randRange(rng_, 0.0, config_.width);
    b.pos.y = randRange(rng_, 0.0, config_.height);
    b.vel.x = randRange(rng_, -config_.ballVelRange, config_.ballVelRange);
    b.vel.y = randRange(rng_, -config_.ballVelRange, config_.ballVelRange);
    std::uniform_int_distribution<int> typeDist(0, BALL_TYPE_COUNT - 1);
    b.type = static_cast<BallType>(typeDist(rng_));
    b.ownerId = -1;
    balls_.push_back(b);
    prevOwner_.push_back(-1);
}

void World::checkFinish() {
    if (aliveNucleusCount() == 0) {
        finished_ = true;
        finishReason_ = "所有核已死亡";
    } else if (frame_ >= config_.maxFrames) {
        finished_ = true;
        finishReason_ = "达到最大帧数";
    }
}

int World::aliveNucleusCount() const {
    int c = 0;
    for (const Nucleus& n : nuclei_) {
        if (n.alive) ++c;
    }
    return c;
}

double World::averageNucleusEnergy() const {
    int c = 0;
    double sum = 0.0;
    for (const Nucleus& n : nuclei_) {
        if (n.alive) {
            ++c;
            sum += n.energy;
        }
    }
    return c == 0 ? 0.0 : sum / c;
}

// ==================== 玩家实时环境调控（v2.2.0） ====================
namespace {
constexpr int kCyanoDefaultNodes = 150;       // cyano=on 激活节点数（契约默认目标）
constexpr int kBloomExtraNodes = 120;         // 已开启时 bloom 追加节点数
constexpr double kHarvestDelta = 8.0;         // 丰收：每核 +8
constexpr double kHarvestInjectionCap = 2000.0;  // 丰收：单次总注入封顶
constexpr double kWinterDelta = -5.0;         // 寒冬：每核 −5
constexpr double kWinterSkipBelow = 5.0;      // 寒冬：能量 <5 跳过
constexpr double kPlagueDelta = -12.0;        // 瘟疫：富核 −12
constexpr double kPlagueMinEnergy = 100.0;    // 瘟疫：只打能量 >100

std::string trimEnv(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lowerEnv(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// 严格整数解析（"3" 通过，"3.0"/"3abc"/"" 拒绝）。
bool parseStrictIntEnv(const std::string& text, int& out) {
    const std::string t = trimEnv(text);
    if (t.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (end == nullptr || static_cast<size_t>(end - t.c_str()) != t.size()) return false;
    out = (int)v;
    return true;
}

// 命令开关值：1/on/true/yes 视为“执行”。
bool parseTruthyEnv(const std::string& text) {
    const std::string v = lowerEnv(trimEnv(text));
    return v == "1" || v == "on" || v == "true" || v == "yes";
}

// cyano=on|off：仅接受明确的开关值（含 1/0 别名）。
bool parseSwitchEnv(const std::string& text, bool& on) {
    const std::string v = lowerEnv(trimEnv(text));
    if (v == "on" || v == "1" || v == "true" || v == "yes") { on = true; return true; }
    if (v == "off" || v == "0" || v == "false" || v == "no") { on = false; return true; }
    return false;
}

const EnvEventDef* findEventDef(EnvEventKind kind) {
    for (const EnvEventDef& d : EnvControl::events()) {
        if (d.kind == kind) return &d;
    }
    return nullptr;
}

// 精确读取 schema 标量字段（按 ParamDef 偏移直接 memcpy）。
// 用途：axis 同档 no-op 的“改动前后值全等”判定——不能用 ParamSchema::valueString 的
// %g（6 位有效数字）比较，否则 1.0000000001 与 1.0 会被误判成没有变化。
double readParamExact(const WorldConfig& cfg, const ParamDef& d) {
    const char* p = reinterpret_cast<const char*>(&cfg) + d.offA;
    switch (d.type) {
        case ParamDef::Type::Int: {
            int v = 0;
            std::memcpy(&v, p, sizeof(v));
            return (double)v;
        }
        case ParamDef::Type::Bool: {
            bool v = false;
            std::memcpy(&v, p, sizeof(v));
            return v ? 1.0 : 0.0;
        }
        case ParamDef::Type::Double:
        case ParamDef::Type::Pair: {
            double v = 0.0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
    }
    return 0.0;
}

// 两个配置在某个 schema 键上是否“逐值全等”（精确比较，不做容差）。
bool paramSameExact(const WorldConfig& a, const WorldConfig& b, const ParamDef& d) {
    if (d.type == ParamDef::Type::Int || d.type == ParamDef::Type::Bool) {
        return readParamExact(a, d) == readParamExact(b, d);
    }
    const double x = readParamExact(a, d);
    const double y = readParamExact(b, d);
    return x == y;  // NaN 时返回 false（视为有变化，宁可扣费）
}
}  // namespace

EnvApplyResult World::applyEnvCommand(const std::map<std::string, std::string>& cmds) {
    EnvApplyResult res;
    // ---- fine 批量扣费语义（v2.2.0 task-4）----
    // 前端按轴合并提交，一次 POST /env 里可能有同一轴/多条 `fine.*`；
    // 一次细调应用只扣一次 kFineCost（不是每键 3）。
    // 预扫描：本批含 fine 命令但影响力不足一次细调 → 整批 fine 全部拒绝。
    bool hasFineCmd = false;
    for (const auto& kv : cmds) {
        if (kv.first.compare(0, 5, "fine.") == 0) {
            hasFineCmd = true;
            break;
        }
    }
    const bool fineBatchUnaffordable =
        hasFineCmd && envState_.influence + 1e-9 < EnvControl::kFineCost;
    bool fineCostPaid = false;  // 本批 fine 费用是否已扣（最多一次）
    for (const auto& kv : cmds) {
        const std::string& key = kv.first;
        const std::string value = trimEnv(kv.second);
        std::string err;

        // ---- axis.<light|food|pressure|producer>=<0..4>：快调一档，费用 1 ----
        if (key.compare(0, 5, "axis.") == 0) {
            const std::string id = key.substr(5);
            const EnvAxisDef* ax = EnvControl::axis(id);
            if (!ax) {
                res.rejected.emplace_back(key, "未知环境轴: " + id);
                continue;
            }
            int level = 0;
            if (!parseStrictIntEnv(value, level)) {
                res.rejected.emplace_back(key, "档位必须是整数（0..4）");
                continue;
            }
            if (level < 0 || level >= 5) {
                res.rejected.emplace_back(key, "档位越界（0..4）");
                continue;
            }
            // 同档 true no-op：先把本档写入临时配置，再逐键比对“改动前后值是否全等”。
            // 为什么不用 axisLevelOf()：它带 1e-12 相对容差，会把“自定义但极接近某档”
            // 误判成同档；这里用精确值比对，只有真正没有变化才免单（幂等成功）。
            WorldConfig trial = config_;
            if (!EnvControl::applyAxisLevel(trial, ax->axis, level, err)) {
                res.rejected.emplace_back(key, err);
                continue;
            }
            bool changed = false;
            for (const auto& lv : ax->levels[(size_t)level].values) {
                const ParamDef* d = ParamSchema::find(lv.first);
                if (!d || !paramSameExact(config_, trial, *d)) {
                    changed = true;
                    break;
                }
            }
            if (!changed) {
                // 无变化：接受命令（applied=1），但不扣影响力、不增加干预计数、不重建派生对象。
                ++res.applied;
                continue;
            }
            if (envState_.influence + 1e-9 < EnvControl::kQuickCost) {
                res.rejected.emplace_back(key, "影响力不足（需要 1）");
                continue;
            }
            config_ = trial;
            envState_.influence = std::max(0.0, envState_.influence - EnvControl::kQuickCost);
            ++envState_.interventions;
            syncDerivedFromConfig();
            ++res.applied;
            continue;
        }

        // ---- fine.<白名单键>=<数值>：细调；本批只扣一次 kFineCost（按 schema min/max 钳制）----
        if (key.compare(0, 5, "fine.") == 0) {
            const std::string pk = key.substr(5);
            if (fineBatchUnaffordable) {
                res.rejected.emplace_back(key, "影响力不足（需要 3）");
                continue;
            }
            // 先写入临时配置，用于判断“是否真的改变”（无变化不扣费）。
            WorldConfig trial = config_;
            if (!EnvControl::applyFine(trial, pk, value, err)) {
                res.rejected.emplace_back(key, err);
                continue;
            }
            const ParamDef* d = ParamSchema::find(pk);
            if (d && paramSameExact(config_, trial, *d)) {
                // 无变化（已是目标值）：幂等成功，不扣费、不计干预。
                ++res.applied;
                continue;
            }
            config_ = trial;
            if (!fineCostPaid) {  // 整批 fine 只扣一次
                envState_.influence =
                    std::max(0.0, envState_.influence - EnvControl::kFineCost);
                fineCostPaid = true;
            }
            ++envState_.interventions;
            syncDerivedFromConfig();
            ++res.applied;
            continue;
        }

        // ---- event.<harvest|winter|plague|bloom>=1：事件（费用/冷却见 EnvControl）----
        if (key.compare(0, 6, "event.") == 0) {
            const std::string id = key.substr(6);
            const EnvEventDef* ev = EnvControl::event(id);
            if (!ev) {
                res.rejected.emplace_back(key, "未知事件: " + id);
                continue;
            }
            if (!parseTruthyEnv(value)) {
                res.rejected.emplace_back(key, "事件值必须为 1");
                continue;
            }
            if (!applyEnvEvent(ev->kind, true, err)) {
                res.rejected.emplace_back(key, err);
                continue;
            }
            ++res.applied;
            continue;
        }

        // ---- preset.<balance|bounty|harsh|bloom>=1：四轴档位 + 蓝细菌目标，费用 5 ----
        if (key.compare(0, 7, "preset.") == 0) {
            const std::string id = key.substr(7);
            const EnvPresetDef* p = EnvControl::preset(id);
            if (!p) {
                res.rejected.emplace_back(key, "未知预设: " + id);
                continue;
            }
            if (!parseTruthyEnv(value)) {
                res.rejected.emplace_back(key, "预设值必须为 1");
                continue;
            }
            if (envState_.influence + 1e-9 < EnvControl::kPresetCost) {
                res.rejected.emplace_back(key, "影响力不足（需要 5）");
                continue;
            }
            if (!EnvControl::applyPresetConfig(config_, *p, err)) {
                res.rejected.emplace_back(key, err);
                continue;
            }
            // 蓝细菌目标：只增不减（契约：预设目标小于现有数量时不强删）。
            if (p->cyanoOn) {
                if (config_.initialCyanoNodes <= 0) {
                    activateCyano(p->cyanoTarget);
                } else {
                    const int have = cyano_.aliveNodeCount();
                    if (have < p->cyanoTarget) addCyanoNodes(p->cyanoTarget - have);
                }
            } else if (config_.initialCyanoNodes > 0) {
                deactivateCyano();
            }
            envState_.influence = std::max(0.0, envState_.influence - EnvControl::kPresetCost);
            ++envState_.interventions;
            syncDerivedFromConfig();
            ++res.applied;
            continue;
        }

        // ---- cyano=on|off：在线开关，费用 10，无冷却 ----
        if (key == "cyano") {
            bool on = false;
            if (!parseSwitchEnv(value, on)) {
                res.rejected.emplace_back(key, "蓝细菌开关值必须为 on 或 off");
                continue;
            }
            const bool currently = config_.initialCyanoNodes > 0;
            if (on == currently) {
                res.rejected.emplace_back(key, on ? "蓝细菌已开启" : "蓝细菌已关闭");
                continue;
            }
            if (envState_.influence + 1e-9 < EnvControl::kCyanoToggleCost) {
                res.rejected.emplace_back(key, "影响力不足（需要 10）");
                continue;
            }
            if (on) {
                activateCyano(kCyanoDefaultNodes);
            } else {
                deactivateCyano();
            }
            envState_.influence = std::max(0.0, envState_.influence - EnvControl::kCyanoToggleCost);
            ++envState_.interventions;
            ++res.applied;
            continue;
        }

        res.rejected.emplace_back(key, "未知命令键");
    }
    return res;
}

bool World::applyEnvEvent(EnvEventKind kind, bool playerInitiated, std::string& err) {
    err.clear();
    const EnvEventDef* def = findEventDef(kind);
    if (!def) {
        err = "未知事件";
        return false;
    }
    if (aliveNucleusCount() == 0) {
        err = "无存活核，事件无效";
        return false;
    }
    if (playerInitiated) {
        const int cd = envState_.cooldown[(size_t)kind];
        if (cd > 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "事件冷却中（剩余 %d 帧）", cd);
            err = buf;
            return false;
        }
        if (envState_.influence + 1e-9 < def->cost) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "影响力不足（需要 %.0f，当前 %.1f）", def->cost,
                          envState_.influence);
            err = buf;
            return false;
        }
    }
    applyEnvEventEffect(kind);
    if (playerInitiated) {
        envState_.influence = std::max(0.0, envState_.influence - def->cost);
        envState_.cooldown[(size_t)kind] = def->cooldownFrames;
        ++envState_.interventions;
    }
    return true;
}

void World::applyEnvEventEffect(EnvEventKind kind) {
    switch (kind) {
        case EnvEventKind::Harvest: {
            // 全体存活核 +8；单次总注入 ≤2000（按实际注入量累计）。
            double injected = 0.0;
            for (Nucleus& n : nuclei_) {
                if (!n.alive) continue;
                if (injected + kHarvestDelta > kHarvestInjectionCap) break;
                energy_.apply(n, kHarvestDelta, EnergyReason::BOOM);
                injected += kHarvestDelta;
            }
            break;
        }
        case EnvEventKind::Winter: {
            for (Nucleus& n : nuclei_) {
                if (!n.alive) continue;
                if (n.energy < kWinterSkipBelow) continue;  // 能量 <5 跳过（防误杀）
                energy_.apply(n, kWinterDelta, EnergyReason::BOOM);
            }
            break;
        }
        case EnvEventKind::Plague: {
            for (Nucleus& n : nuclei_) {
                if (!n.alive) continue;
                if (!(n.energy > kPlagueMinEnergy)) continue;  // 只打富核
                energy_.apply(n, kPlagueDelta, EnergyReason::BOOM);
            }
            break;
        }
        case EnvEventKind::Bloom:
        default: {
            if (config_.initialCyanoNodes <= 0 || cyano_.empty()) {
                activateCyano(kCyanoDefaultNodes);
            } else {
                addCyanoNodes(kBloomExtraNodes);
            }
            break;
        }
    }
}

void World::tickEnvironment() {
    // 影响力恢复（暂停时不推进：tickEnvironment 只在 step() 内调用）。
    envState_.influence = std::min(EnvControl::kInfluenceMax,
                                   envState_.influence + EnvControl::kInfluenceRegenPerFrame);
    // 事件冷却递减。
    for (int& cd : envState_.cooldown) {
        if (cd > 0) --cd;
    }
    // 存活峰值（结算展示）。
    const int alive = aliveNucleusCount();
    if (alive > envState_.peakAlive) envState_.peakAlive = alive;
}

void World::activateCyano(int nodes) {
    const int n = (nodes > 0) ? nodes : kCyanoDefaultNodes;
    syncCyanoParamsFromConfig();
    if (cyano_.worldWidth() != config_.width || cyano_.worldHeight() != config_.height) {
        cyano_.setWorldSize(config_.width, config_.height);
    }
    // 玩家行为：允许消耗主 rng 撒点（无玩家命令的基线路径不会走到这里）。
    cyano_.initialize(rng_, n, config_.cyanoColonies, config_.cyanoInitialEnergy);
    config_.initialCyanoNodes = n;  // 开关标记 = 节点数（>0 即启用）
}

void World::deactivateCyano() {
    // 关闭：清空群落并复位开关；**零 RNG**（不推进主随机流）。
    cyano_.clear();
    config_.initialCyanoNodes = 0;
}

void World::addCyanoNodes(int nodes) {
    if (nodes <= 0) return;
    if (config_.initialCyanoNodes <= 0) {
        activateCyano(nodes);  // 关闭态下“追加”等价于直接激活
        return;
    }
    syncCyanoParamsFromConfig();
    if (cyano_.worldWidth() != config_.width || cyano_.worldHeight() != config_.height) {
        cyano_.setWorldSize(config_.width, config_.height);
    }
    cyano_.addNodes(rng_, nodes, config_.cyanoColonies, config_.cyanoInitialEnergy);
}

void World::injectBalls(int n) {
    for (int i = 0; i < n; ++i) spawnFreeBall();
}

void World::syncSunFromConfig() { sun_ = makeSunParams(config_); }

void World::syncCyanoParamsFromConfig() {
    cyano_.setParams(makeCyanoParams(config_));
}

void World::syncDerivedFromConfig() {
    // 配置（WorldConfig）是唯一事实来源：派生 POD / 群落参数 / 空间网格尺寸都按它重建。
    syncSunFromConfig();
    syncCyanoParamsFromConfig();
    // 世界尺寸可能因细调/读档变化：群落内部网格与节点位置需同步（零 RNG）。
    if (cyano_.worldWidth() != config_.width || cyano_.worldHeight() != config_.height) {
        cyano_.setWorldSize(config_.width, config_.height);
    }
    // SpatialGrid 没有 reset：按新尺寸整对象重建（隐式拷贝赋值，无 RNG/无副作用）。
    grid_ = SpatialGrid(config_.width, config_.height, config_.gridCellSize);
}

// ==================== 存档序列化（Phase D / v2.2.0） ====================
namespace {
void wU8(std::ostream& os, uint8_t v) { os.write((const char*)&v, 1); }
void wU16(std::ostream& os, uint16_t v) { os.write((const char*)&v, 2); }
void wU32(std::ostream& os, uint32_t v) { os.write((const char*)&v, 4); }
void wU64(std::ostream& os, uint64_t v) { os.write((const char*)&v, 8); }
void wF32(std::ostream& os, float v) { os.write((const char*)&v, 4); }
void wF64(std::ostream& os, double v) { os.write((const char*)&v, 8); }
void wI32(std::ostream& os, int32_t v) { os.write((const char*)&v, 4); }
void wStr(std::ostream& os, const std::string& s) {
    wU32(os, (uint32_t)s.size());
    os.write(s.data(), (std::streamsize)s.size());
}
bool rU8(std::istream& is, uint8_t& v) { return is.read((char*)&v, 1).good(); }
bool rU16(std::istream& is, uint16_t& v) { return is.read((char*)&v, 2).good(); }
bool rU32(std::istream& is, uint32_t& v) { return is.read((char*)&v, 4).good(); }
bool rU64(std::istream& is, uint64_t& v) { return is.read((char*)&v, 8).good(); }
bool rF32(std::istream& is, float& v) { return is.read((char*)&v, 4).good(); }
bool rF64(std::istream& is, double& v) { return is.read((char*)&v, 8).good(); }
bool rI32(std::istream& is, int32_t& v) { return is.read((char*)&v, 4).good(); }
bool rStr(std::istream& is, std::string& s) {
    uint32_t n = 0;
    if (!rU32(is, n)) return false;
    if (n > (1u << 20)) return false;
    s.resize(n);
    return is.read(&s[0], (std::streamsize)n).good();
}
std::string trimLocal(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

bool World::writeState(std::ostream& os) const {
    if (!os.good()) return false;
    os.write("NSMS", 4);
    wU32(os, 2);  // 存档格式版本：v2 = v1 + 色相 RNG + 蓝细菌 + EnvState（mod 状态段由启动器追加）
    // 配置段：schema 键值行（版本演进安全：读取端忽略未知键）。
    std::ostringstream cfgLines;
    for (const ParamDef& d : ParamSchema::all()) {
        cfgLines << d.key << " = " << ParamSchema::valueString(config_, d) << "\n";
    }
    std::string cfgStr = cfgLines.str();
    wU32(os, (uint32_t)ParamSchema::all().size());
    wStr(os, cfgStr);
    // 运行态计数
    wU64(os, (uint64_t)frame_);
    wU64(os, (uint64_t)nextNucleusId_);
    // rng 状态（std::mt19937 标准序列化，往返后继续生成完全一致）。
    std::ostringstream rngStream;
    rngStream << rng_;
    std::string rngStr = rngStream.str();
    wU32(os, (uint32_t)rngStr.size());
    os.write(rngStr.data(), (std::streamsize)rngStr.size());
    // 球 + 上一帧归属
    wU64(os, (uint64_t)balls_.size());
    for (const Ball& b : balls_) {
        wF64(os, b.pos.x);
        wF64(os, b.pos.y);
        wF64(os, b.vel.x);
        wF64(os, b.vel.y);
        wU8(os, (uint8_t)b.type);
        wI32(os, (int32_t)b.ownerId);
    }
    for (int p : prevOwner_) wI32(os, (int32_t)p);
    // 核（含 22 遗传参数）
    wU64(os, (uint64_t)nuclei_.size());
    for (const Nucleus& n : nuclei_) {
        wF64(os, n.pos.x);
        wF64(os, n.pos.y);
        wF64(os, n.vel.x);
        wF64(os, n.vel.y);
        wF64(os, n.energy);
        wU8(os, n.alive ? 1 : 0);
        wI32(os, (int32_t)n.id);
        wI32(os, (int32_t)n.age);
        wF64(os, n.wanderAngle);
        for (int t = 0; t < BALL_TYPE_COUNT; ++t) {
            wF64(os, n.params.affinity[(size_t)t]);
            wF64(os, n.params.orbitRadius[(size_t)t]);
            wF64(os, n.params.absorbPreference[(size_t)t]);
            wF64(os, n.params.repelStrength[(size_t)t]);
        }
        wF64(os, n.params.attackRange);
        wF64(os, n.params.attackStrength);
        wF64(os, n.params.avoidRange);
        wF64(os, n.params.avoidStrength);
        wF64(os, n.params.maxSpeed);
        wF64(os, n.params.energyThreshold);
        wF64(os, n.params.mutationRate);
        // ---- 蓝细菌相关遗传字段（存档版本 v1 内兼容追加）----
        wF64(os, n.params.cyanoPreference);
        wF64(os, n.params.cyanoDigestion);
        wF64(os, n.params.maxAccel);
    }

    // ==================== v2 追加段（必须在原有 mod 状态段之前） ====================
    // 1) 色相 RNG（与主 rng 独立，但也要保存，读档后色相序列才能续上）。
    std::ostringstream hueStream;
    hueStream << hueRng_;
    wStr(os, hueStream.str());

    // 2) 蓝细菌群落：节点 → 连接 → nextNodeId → enabled。
    const std::vector<CyanoNode>& cn = cyano_.nodes();
    const std::vector<CyanoLink>& cl = cyano_.links();
    wU32(os, (uint32_t)cn.size());
    for (const CyanoNode& n : cn) {
        wF32(os, (float)n.pos.x);
        wF32(os, (float)n.pos.y);
        wF32(os, (float)n.energy);
        wU32(os, n.id);
        wU16(os, n.colonyId);
        wU16(os, (uint16_t)std::clamp(n.age, 0, 65535));
        // flags：bit0 = 存活，bit1..2 = 类型（与 WebRenderer 快照 flags 语义一致）。
        const uint16_t flags =
            (uint16_t)((n.alive ? 1u : 0u) | ((uint32_t)(n.type & 0x3u) << 1));
        wU16(os, flags);
        wU16(os, n.linkCount);
        wU8(os, n.type);
    }
    wU32(os, (uint32_t)cl.size());
    for (const CyanoLink& l : cl) {
        wU32(os, l.a);  // nodes_ 数组下标（a < b）
        wU32(os, l.b);
        wF64(os, l.flow);
    }
    wU32(os, cyano_.nextNodeId());
    wU8(os, config_.initialCyanoNodes > 0 ? 1 : 0);

    // 3) EnvState：影响力 / 事件冷却[4] / 干预计数 / 存活峰值。
    wF64(os, envState_.influence);
    for (int i = 0; i < kEnvEventCount; ++i) wI32(os, (int32_t)envState_.cooldown[(size_t)i]);
    wI32(os, (int32_t)envState_.interventions);
    wI32(os, (int32_t)envState_.peakAlive);
    return os.good();
}

bool World::readState(std::istream& is, std::string& err) {
    auto fail = [&](const char* m) {
        err = m;
        return false;
    };
    char magic[4];
    if (!is.read(magic, 4)) return fail("存档文件损坏（读取失败）");
    if (std::memcmp(magic, "NSMS", 4) != 0) return fail("不是有效存档（magic 不匹配）");
    uint32_t version = 0;
    if (!rU32(is, version)) return fail("存档版本字段缺失");
    // v1 = 蓝细菌节点段之前（无 hueRng / 无群落 / 无 EnvState）；v2 = 当前格式。
    if (version != 1 && version != 2) return fail("存档版本不支持（当前支持 v1/v2）");
    uint32_t cfgCount = 0;
    if (!rU32(is, cfgCount)) return fail("配置段缺失");
    std::string cfgStr;
    if (!rStr(is, cfgStr)) return fail("配置段读取失败");
    std::istringstream ls(cfgStr);
    std::string line;
    while (std::getline(ls, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        ParamSchema::applyLine(config_, trimLocal(line.substr(0, eq)),
                               trimLocal(line.substr(eq + 1)));
    }
    uint64_t frame64 = 0, next64 = 0;
    if (!rU64(is, frame64) || !rU64(is, next64)) return fail("运行态计数缺失");
    uint32_t rngLen = 0;
    if (!rU32(is, rngLen)) return fail("rng 段缺失");
    if (rngLen > (1u << 20)) return fail("rng 段长度异常");
    std::string rngStr;
    rngStr.resize(rngLen);
    if (!is.read(&rngStr[0], (std::streamsize)rngLen)) return fail("rng 段读取失败");
    std::istringstream rs(rngStr);
    rs >> rng_;
    if (rs.fail()) return fail("rng 状态恢复失败");
    uint64_t ballCount = 0;
    if (!rU64(is, ballCount)) return fail("球数缺失");
    if (ballCount > 10000000) return fail("球数异常（存档损坏）");
    balls_.resize((size_t)ballCount);
    for (Ball& b : balls_) {
        if (!rF64(is, b.pos.x) || !rF64(is, b.pos.y) ||
            !rF64(is, b.vel.x) || !rF64(is, b.vel.y)) return fail("球数据不完整");
        uint8_t t = 0;
        if (!rU8(is, t)) return fail("球类型缺失");
        b.type = static_cast<BallType>(std::min<int>(t, BALL_TYPE_COUNT - 1));
        if (!rI32(is, b.ownerId)) return fail("球归属缺失");
    }
    prevOwner_.resize((size_t)ballCount);
    for (int& p : prevOwner_) {
        if (!rI32(is, p)) return fail("球归属历史缺失");
    }
    uint64_t nCount = 0;
    if (!rU64(is, nCount)) return fail("核数缺失");
    if (nCount > 1000000) return fail("核数异常（存档损坏）");
    nuclei_.resize((size_t)nCount);
    for (Nucleus& n : nuclei_) {
        if (!rF64(is, n.pos.x) || !rF64(is, n.pos.y) ||
            !rF64(is, n.vel.x) || !rF64(is, n.vel.y) ||
            !rF64(is, n.energy)) return fail("核数据不完整");
        uint8_t alive8 = 0;
        if (!rU8(is, alive8)) return fail("核存活标志缺失");
        n.alive = alive8 != 0;
        if (!rI32(is, n.id) || !rI32(is, n.age) || !rF64(is, n.wanderAngle)) {
            return fail("核状态缺失");
        }
        for (int t = 0; t < BALL_TYPE_COUNT; ++t) {
            if (!rF64(is, n.params.affinity[(size_t)t]) ||
                !rF64(is, n.params.orbitRadius[(size_t)t]) ||
                !rF64(is, n.params.absorbPreference[(size_t)t]) ||
                !rF64(is, n.params.repelStrength[(size_t)t])) return fail("核遗传参数不完整");
        }
        if (!rF64(is, n.params.attackRange) || !rF64(is, n.params.attackStrength) ||
            !rF64(is, n.params.avoidRange) || !rF64(is, n.params.avoidStrength) ||
            !rF64(is, n.params.maxSpeed) || !rF64(is, n.params.energyThreshold) ||
            !rF64(is, n.params.mutationRate)) return fail("核遗传参数不完整");
        // ---- 蓝细菌相关遗传字段（存档版本 v1 内兼容追加）----
        if (!rF64(is, n.params.cyanoPreference) || !rF64(is, n.params.cyanoDigestion) ||
            !rF64(is, n.params.maxAccel)) return fail("核蓝细菌字段缺失");
    }
    // ==================== v2 追加段 ====================
    std::vector<CyanoNode> cyanoNodes;
    std::vector<CyanoLink> cyanoLinks;
    uint32_t cyanoNextId = 1;
    bool cyanoEnabledSaved = false;
    bool hasEnvState = false;
    EnvState loadedEnv;
    if (version >= 2) {
        // 1) hueRng 文本流
        std::string hueStr;
        if (!rStr(is, hueStr)) return fail("色相 RNG 段缺失");
        {
            std::istringstream hs(hueStr);
            hs >> hueRng_;
            if (hs.fail()) return fail("色相 RNG 状态恢复失败");
        }
        // 2) 蓝细菌节点（f32 x,y,energy；u32 id；u16 colony,age,flags,linkCount；u8 type）
        uint32_t nodeCount = 0;
        if (!rU32(is, nodeCount)) return fail("蓝细菌节点数缺失");
        if (nodeCount > 1000000) return fail("蓝细菌节点数异常（存档损坏）");
        cyanoNodes.resize((size_t)nodeCount);
        for (CyanoNode& nd : cyanoNodes) {
            float fx = 0.0f, fy = 0.0f, fe = 0.0f;
            if (!rF32(is, fx) || !rF32(is, fy) || !rF32(is, fe)) {
                return fail("蓝细菌节点数据不完整");
            }
            nd.pos.x = fx;
            nd.pos.y = fy;
            nd.energy = fe;
            if (!rU32(is, nd.id)) return fail("蓝细菌节点 ID 缺失");
            if (!rU16(is, nd.colonyId)) return fail("蓝细菌节点聚落缺失");
            uint16_t age16 = 0, flags = 0;
            if (!rU16(is, age16) || !rU16(is, flags)) return fail("蓝细菌节点状态缺失");
            nd.age = (int)age16;
            if (!rU16(is, nd.linkCount)) return fail("蓝细菌节点连接数缺失");
            uint8_t type8 = 0;
            if (!rU8(is, type8)) return fail("蓝细菌节点类型缺失");
            nd.type = type8;
            nd.alive = (flags & 0x1u) ? 1 : 0;  // 与写端 flags 语义一致
        }
        // 连接（u32 a,b；f64 flow）
        uint32_t linkCount = 0;
        if (!rU32(is, linkCount)) return fail("蓝细菌连接数缺失");
        if (linkCount > 10000000) return fail("蓝细菌连接数异常（存档损坏）");
        cyanoLinks.resize((size_t)linkCount);
        for (CyanoLink& l : cyanoLinks) {
            if (!rU32(is, l.a) || !rU32(is, l.b) || !rF64(is, l.flow)) {
                return fail("蓝细菌连接数据不完整");
            }
        }
        if (!rU32(is, cyanoNextId)) return fail("蓝细菌 nextNodeId 缺失");
        uint8_t enabled8 = 0;
        if (!rU8(is, enabled8)) return fail("蓝细菌开关标志缺失");
        cyanoEnabledSaved = (enabled8 != 0);
        // 3) EnvState
        if (!rF64(is, loadedEnv.influence)) return fail("影响力段缺失");
        for (int i = 0; i < kEnvEventCount; ++i) {
            int32_t cd = 0;
            if (!rI32(is, cd)) return fail("事件冷却段缺失");
            loadedEnv.cooldown[(size_t)i] = (int)cd;
        }
        int32_t iv = 0, pk = 0;
        if (!rI32(is, iv) || !rI32(is, pk)) return fail("环境干预计数段缺失");
        loadedEnv.interventions = (int)iv;
        loadedEnv.peakAlive = (int)pk;
        hasEnvState = true;
    }

    frame_ = (int)frame64;
    nextNucleusId_ = (int)next64;
    finished_ = false;
    finishReason_ = "";
    // 配置（含世界尺寸/阳光/蓝细菌参数）已应用 → 派生对象必须按新配置重建。
    syncDerivedFromConfig();
    if (version >= 2) {
        // 蓝细菌开关标记以存档为准（配置段里的 cyano_nodes 可能被后续在线操作改过）。
        if (!cyanoEnabledSaved) {
            config_.initialCyanoNodes = 0;
            cyano_.clear();
        } else {
            if (config_.initialCyanoNodes <= 0) {
                config_.initialCyanoNodes =
                    (int)std::max<size_t>(1, cyanoNodes.size());  // 防御：保持“启用”语义
            }
            cyano_.load(cyanoNodes, cyanoLinks, cyanoNextId);
        }
        if (hasEnvState) {
            envState_ = loadedEnv;
            envState_.influence =
                std::clamp(envState_.influence, 0.0, EnvControl::kInfluenceMax);
            for (int& cd : envState_.cooldown) {
                if (cd < 0) cd = 0;
            }
            if (envState_.interventions < 0) envState_.interventions = 0;
            if (envState_.peakAlive < 0) envState_.peakAlive = 0;
        }
    } else {
        // v1 旧档：没有蓝细菌节点段 → 群落只能是空的（不消耗 RNG，也不伪造节点）。
        // 因此把开关标记归零：GET /env 显示“已关闭”，玩家可显式 cyano=on 重新激活
        //（激活会消耗主 rng 撒点，属于玩家行为，不会污染“无玩家命令”的确定性红线）。
        // 环境状态从默认值开始（旧档没有影响力概念）。
        envState_ = EnvState{};
        if (config_.initialCyanoNodes > 0) {
            config_.initialCyanoNodes = 0;
            cyano_.clear();
        }
    }
    buildGrid();
    return true;
}
