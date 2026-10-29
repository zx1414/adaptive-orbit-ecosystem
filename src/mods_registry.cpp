#include "mods_registry.h"
#include <fstream>
#include <iostream>

// mod 注册函数声明（实现见 mods/ 目录）。
void registerPeriodicDrain(ModAPI&);
void registerPlagueStrike(ModAPI&);
void registerBallLossRelief(ModAPI&);
void registerRareBallSubsidy(ModAPI&);
void registerNicheGuild(ModAPI&);
void registerSymbiosisBond(ModAPI&);
void registerSeasonCycle(ModAPI&);
void registerEnergySpill(ModAPI&);
void registerClimateEvent(ModAPI&);
void registerCyanobacteria(ModAPI&);
void registerBallRoles(ModAPI&);
void registerAging(ModAPI&);
void registerLuckyLottery(ModAPI&);

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

const std::vector<ModInfo>& allMods() {
    // 字段顺序：{id, name, desc, touches, incompatible, requires, reg, ...}
    // requires = 启动器徽章文案（纯展示，不参与注册/校验逻辑）：
    //   ""                       无前置条件
    //   "alive>=2"               需要至少 2 个存活核才能观察到配对/共生效果
    //   "combo:<mod>"            建议与指定 mod 组合使用
    //   "conflict:relief"        与失球宽限同一机制域（冲突提示）
    //   "event-adapter"          事件适配器：与玩家事件卡共用 World::applyEnvEvent
    //   "ambient"                环境氛围型：只改每帧压力/能量，无外部依赖
    //   "config:cyano_nodes>0"   需要环境配置开启蓝细菌（cyano_nodes > 0）
    static const std::vector<ModInfo> mods = {
        {"periodic_drain", "周期性瘟疫",
         "每 100 帧对所有存活核扣 10 能量（示例 mod）",
         "PLAGUE", "", "", registerPeriodicDrain},
        {"plague_strike", "瘟疫打击",
         "每 50 帧对能量超过 100 的核扣 8 能量（示例 mod）",
         "PLAGUE", "", "", registerPlagueStrike},

        // ---- 小球生态多样化 mod 套件 ----
        // 顺序 = 推荐优先级（mods.list 可覆盖）。touches 供启动器做冲突提示。
        {"ball_loss_relief", "失球宽限",
         "被抢球不再直接罚没：按换主阶梯（前 N 次全额、其后按比例）返还部分罚金，"
         "并对新生核提供幼体保护，消除「罚 5.0 最多赚 3.0」的单向销毁。"
         "【实测】单独启用 extinction 2/3→0/3、survivors_max 1→7（8 种子）", 
         "TERRITORY", "", "", registerBallLossRelief},
        {"rare_ball_subsidy", "稀有球型保育",
         "核吸收自己高偏好（absorbPreference>1.2）的稀有小球时获得保育补贴，"
         "稀缺度越高补贴越多，让核主动偏向不同球型以换取能量，推动球型生态位分化。"
         "【注意】单独启用实测更差，定位为配合 ball_loss_relief 使用的组件",
         "SUBSIDY", "", "combo:ball_loss_relief", registerRareBallSubsidy},
        {"niche_guild", "生态位行会",
         "按归一化遗传参数（护盾/资源/侦察取向）组建行会，同行会相邻核获得零和协作加成，"
         "打破 22 参数自由漂变导致的同质化。严格零和，in==out",
         "GUILD", "", "conflict:relief", registerNicheGuild},
        {"symbiosis_bond", "核间共生",
         "距离适中的富余核（能量>阈值）向贫瘠核（能量<0.9×阈值）零和输血，建立互惠关系，"
         "补上基因库中缺失的合作型生态位。"
         "【注意】配对型：需同时存在富核与贫核，单独启用不触发（基线仅 0~1 核）；"
         "须与 ball_loss_relief 组合使用。严格零和，in==out",
         "SYMBIOSIS", "", "alive>=2", registerSymbiosisBond},
        {"season_cycle", "季节轮回",
         "按周期切换春/夏/秋/冬环境压力（代谢缓解/繁殖纾困/丰收储备/代谢加压），"
         "奖励轮换的策略，打破稳态单一最优解。四季净收益对称，非单向补贴",
         "SEASON", "", "ambient", registerSeasonCycle},
        {"energy_spill", "能量溢出再分配",
         "能量超阈值 1.05 倍的核将溢出部分零和转移给贫瘠核（<0.95×阈值），"
         "减少富集核的浪费、托举濒危核。"
         "【注意】配对型：需同时存在富核与贫核，单独启用不触发；"
         "须与 ball_loss_relief 组合使用。严格零和，in==out",
         "SPILL", "", "alive>=2", registerEnergySpill},
        {"climate_event", "气候突变",
         "每 800 帧用确定性随机种子（api.rng）抽取丰收/寒冬/瘟疫三选一的全局事件，"
         "为恒定环境加入不可预测的扰动；事件效果复用 World::applyEnvEvent，"
         "与玩家事件卡共用实现、不重复结算",
         "BOOM", "", "event-adapter", registerClimateEvent},

        // ---- 生产者（蓝细菌）----
        // 需配合 env_config 的 cyano_nodes > 0：本 mod 只接管每帧驱动，
        // 群落初始化与配置解析仍由 World 负责。未启用时行为与无蓝细菌版本完全一致。
        {"cyanobacteria", "蓝细菌生产者",
         "自养生产者群落：光合产能、代谢消耗、连接网络内能量由高向低流动，"
         "概率产球与繁殖形成聚落扩张；核捕食节点获得 节点能量×转化率，"
         "转化率随最大速度/加速度下降（机动性与消化效率的取舍）。",
         "CYANO_GAIN", "", "config:cyano_nodes>0", registerCyanobacteria},

        // ---- 小球角色分工（计划1 经济层）----
        {"ball_roles", "小球角色分工",
         "资源球为主人持续产出能量（半饱和曲线：收益递减、单核上界 0.04/帧），"
         "护盾球按持有量抵挡本帧失球罚金的 60%（台账法精确校准，杜绝吸收假阳性）——"
         "让「养球」与「吃球」成为两种真实策略。"
         "【注意】与失球宽限都返还 BALL_LOSS 罚金，叠加可突破净销毁上限，勿同时启用",
         "BALL_ROLES_WORKER,BALL_ROLES_SHIELD", "ball_loss_relief", "conflict:relief",
         registerBallRoles},

        // ---- 趣味 mod ----
        {"aging", "衰老",
         "每个存活核每帧流失 kAgingBase(0.00001)×min(核龄, kAgingCapAge(3000)) 能量："
         "线性衰老、3000 帧封顶（避免老核被无限盘剥），核龄直接读 Nucleus::age，"
         "无随机、零跨帧状态。把「寿命」变成被选择的性状，加速高龄核淘汰与策略轮换。"
         "【注意】单向能量汇（AGING 只有 out），非零和注入；"
         "【验收观测点】--energy-summary 中 AGING out>0 且随帧数增长，存活核数低于基线属预期",
         "AGING", "", "", registerAging},
        {"lucky_lottery", "幸运抽奖",
         "每 kLotteryPeriod(300) 帧开一期彩票：所有存活核各买 kTicket(2.0) 注金，"
         "全部注金一次性发给 api.rng 均匀抽中的一个幸运核——严格零和，"
         "奖金 = Σ注金 = 存活核数×2.0，一分不留；存活核 <2 时本期跳过。"
         "为生态注入期望为零、方差巨大的随机漂变，打破富者恒富。"
         "【验收观测点】--energy-summary 中 LUCKY_LOTTERY 的 in 与 out 相等（|in-out|<0.01）且均>0；"
         "5000 帧共开奖 16 期",
         "LUCKY_LOTTERY", "", "alive>=2", registerLuckyLottery},
    };
    return mods;
}

std::vector<std::string> readModsList(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    if (!f.is_open()) return out;
    std::string line;
    while (std::getline(f, line)) {
        std::string id = trim(line);
        if (id.empty() || id[0] == '#') continue;
        bool known = false;
        for (const ModInfo& m : allMods()) {
            if (m.id == id) {
                known = true;
                break;
            }
        }
        if (known) {
            out.push_back(id);
        } else {
            std::cerr << "[mods] 未知 mod，已忽略: " << id << "\n";
        }
    }
    return out;
}

void registerAllMods(ModAPI& api, const std::vector<std::string>& enabledOrdered) {
    for (const std::string& id : enabledOrdered) {
        for (const ModInfo& m : allMods()) {
            if (id == m.id) {
                std::cout << "[mods] 注册 " << m.name << "\n";
                m.reg(api);
                break;
            }
        }
    }
}
