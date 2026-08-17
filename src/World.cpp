#include "World.h"
#include "ParamSchema.h"
#include <algorithm>
#include <cmath>
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
}  // namespace

World::World(const WorldConfig& cfg)
    : config_(cfg), grid_(cfg.width, cfg.height, cfg.gridCellSize), rng_(cfg.seed) {
    // 未显式指定承载力时，按世界面积估算一个合理上限，防止核种群无限增长。
    if (config_.maxNuclei <= 0) {
        config_.maxNuclei = std::max(1, (int)(config_.width * config_.height / config_.territoryPerNucleus));
        config_.maxNuclei = std::max(config_.maxNuclei, config_.initialNuclei);
    }
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
        n.params.randomize(rng_, config_.ranges);
        n.pos.x = randRange(rng_, 0.0, config_.width);
        n.pos.y = randRange(rng_, 0.0, config_.height);
        n.vel = Vec2(0.0, 0.0);
        n.energy = n.params.energyThreshold * config_.nucleusInitEnergyRatio;
        n.alive = true;
        n.id = nextNucleusId_++;
        n.wanderAngle = randRange(rng_, 0.0, TWO_PI);
        nuclei_.push_back(n);
    }
}

void World::addNucleus(const Vec2& pos, double energy, const NucleusParams& params) {
    Nucleus n(pos, energy, params);
    n.alive = true;
    n.id = nextNucleusId_++;
    n.wanderAngle = randRange(rng_, 0.0, TWO_PI);
    nuclei_.push_back(n);
}

void World::step() {
    // 年龄递增（用于新生保护期等）。
    for (Nucleus& n : nuclei_) {
        if (n.alive) ++n.age;
    }
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
        child.params.mutate(rng_, n.params.mutationRate, config_.ranges);
        child.pos.x += randRange(rng_, -config_.reproductionOffset, config_.reproductionOffset);
        child.pos.y += randRange(rng_, -config_.reproductionOffset, config_.reproductionOffset);
        child.pos.x = std::clamp(child.pos.x, 0.0, config_.width);
        child.pos.y = std::clamp(child.pos.y, 0.0, config_.height);
        child.vel = Vec2(0.0, 0.0);
        child.energy = threshold * config_.childEnergyRatio;  // 初始能量直接赋值（不触发死亡判定）
        child.alive = true;
        child.id = nextNucleusId_++;
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

// ==================== 存档序列化（Phase D） ====================
namespace {
void wU8(std::ostream& os, uint8_t v) { os.write((const char*)&v, 1); }
void wU32(std::ostream& os, uint32_t v) { os.write((const char*)&v, 4); }
void wU64(std::ostream& os, uint64_t v) { os.write((const char*)&v, 8); }
void wF64(std::ostream& os, double v) { os.write((const char*)&v, 8); }
void wI32(std::ostream& os, int32_t v) { os.write((const char*)&v, 4); }
void wStr(std::ostream& os, const std::string& s) {
    wU32(os, (uint32_t)s.size());
    os.write(s.data(), (std::streamsize)s.size());
}
bool rU8(std::istream& is, uint8_t& v) { return is.read((char*)&v, 1).good(); }
bool rU32(std::istream& is, uint32_t& v) { return is.read((char*)&v, 4).good(); }
bool rU64(std::istream& is, uint64_t& v) { return is.read((char*)&v, 8).good(); }
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
    wU32(os, 1);  // 存档格式版本
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
    }
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
    if (version != 1) return fail("存档版本不支持（当前仅支持 v1）");
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
    }
    frame_ = (int)frame64;
    nextNucleusId_ = (int)next64;
    finished_ = false;
    finishReason_ = "";
    buildGrid();
    return true;
}
