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

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

const std::vector<ModInfo>& allMods() {
    static const std::vector<ModInfo> mods = {
        {"periodic_drain", "周期性瘟疫",
         "每 100 帧对所有存活核扣 10 能量（示例 mod）",
         "PLAGUE", "", registerPeriodicDrain},
        {"plague_strike", "瘟疫打击",
         "每 50 帧对能量超过 100 的核扣 8 能量（示例 mod）",
         "PLAGUE", "", registerPlagueStrike},

        // ---- 小球生态多样化 mod 套件 ----
        // 顺序 = 推荐优先级（mods.list 可覆盖）。touches 供启动器做冲突提示。
        {"ball_loss_relief", "失球宽限",
         "被抢球不再直接罚没：按换主阶梯（前 N 次全额、其后按比例）返还部分罚金，"
         "并对新生核提供幼体保护，消除「罚 5.0 最多赚 3.0」的单向销毁。"
         "【实测】单独启用 extinction 2/3→0/3、survivors_max 1→7（8 种子）", 
         "TERRITORY", "", registerBallLossRelief},
        {"rare_ball_subsidy", "稀有球型保育",
         "核吸收自己高偏好（absorbPreference>1.2）的稀有小球时获得保育补贴，"
         "稀缺度越高补贴越多，让核主动偏向不同球型以换取能量，推动球型生态位分化。"
         "【注意】单独启用实测更差，定位为配合 ball_loss_relief 使用的组件",
         "SUBSIDY", "", registerRareBallSubsidy},
        {"niche_guild", "生态位行会",
         "按归一化遗传参数（护盾/资源/侦察取向）组建行会，同行会相邻核获得零和协作加成，"
         "打破 22 参数自由漂变导致的同质化。严格零和，in==out",
         "GUILD", "", registerNicheGuild},
        {"symbiosis_bond", "核间共生",
         "距离适中的富余核（能量>阈值）向贫瘠核（能量<0.9×阈值）零和输血，建立互惠关系，"
         "补上基因库中缺失的合作型生态位。"
         "【注意】配对型：需同时存在富核与贫核，单独启用不触发（基线仅 0~1 核）；"
         "须与 ball_loss_relief 组合使用。严格零和，in==out",
         "SYMBIOSIS", "", registerSymbiosisBond},
        {"season_cycle", "季节轮回",
         "按周期切换春/夏/秋/冬环境压力（代谢缓解/繁殖纾困/丰收储备/代谢加压），"
         "奖励轮换的策略，打破稳态单一最优解。四季净收益对称，非单向补贴",
         "SEASON", "", registerSeasonCycle},
        {"energy_spill", "能量溢出再分配",
         "能量超阈值 1.05 倍的核将溢出部分零和转移给贫瘠核（<0.95×阈值），"
         "减少富集核的浪费、托举濒危核。"
         "【注意】配对型：需同时存在富核与贫核，单独启用不触发；"
         "须与 ball_loss_relief 组合使用。严格零和，in==out",
         "SPILL", "", registerEnergySpill},
        {"climate_event", "气候突变",
         "每 800 帧用确定性随机种子（api.rng）抽取丰收/寒冬/瘟疫三选一的全局事件，"
         "为恒定环境加入不可预测的扰动；瘟疫只打击能量>100 的富核以防垄断",
         "BOOM", "", registerClimateEvent},

        // ---- 生产者（蓝细菌）----
        // 需配合 env_config 的 cyano_nodes > 0：本 mod 只接管每帧驱动，
        // 群落初始化与配置解析仍由 World 负责。未启用时行为与无蓝细菌版本完全一致。
        {"cyanobacteria", "蓝细菌生产者",
         "自养生产者群落：光合产能、代谢消耗、连接网络内能量由高向低流动，"
         "概率产球与繁殖形成聚落扩张；核捕食节点获得 节点能量×转化率，"
         "转化率随最大速度/加速度下降（机动性与消化效率的取舍）。",
         "CYANO_GAIN", "", registerCyanobacteria},
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
