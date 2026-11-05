// agentsim - app/ParamSchema.cpp
// R1-C：参数元数据表 + 校验 + GET /schema 的 JSON。
//
// 键名风格沿用 v2.3.1（snake_case），但字段语义以 agentsim 为准：
//   - Food 实体已整体移除（V2-SPEC §E）：蓝细菌是唯一生产者，"food" 组描述的是
//     自由球（蓝细菌产球 + 启动库存）的能量经济学，不再是"人工撒食"。
//   - 控制台字符渲染被用户点名排除（V2-SPEC §0），旧版的 visualization/grid_*
//     等渲染键不再收录；render_interval 保留（浏览器画面快照仍需要）。
//   - V2-SPEC-AMEND-1：战斗取消 → 无 attack_*/avoid_* 键；新增 4 个核社会参数
//     与 2 个施力参数，共 24 个 genome 键（3 类型数组键拆成逐类型标量键）。
//
// 本表**只做元数据**：不持有 Config 指针，不做 key→字段映射。
// 主代理接线时负责把 key 写到 SimConfig 对应字段（见交付报告映射清单）。
//
// 数值来源约定：
//   - agentsim 已有字段的默认值照抄 Config.h / CyanoParams（真实值）。
//   - agentsim 尚无字段的键（阳光模型、球动力学等）给出与 256x256 小世界
//     匹配的提案值（v2.3.1 是 2000x2000，距离类参数按面积比 ~1/8 缩放），
//     全部在交付报告"映射清单"里逐条列出，由主代理在接线轮定夺。
#include "ParamSchema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace agentsim::app {
namespace {

// 表的行式中间结构：先写平表，构建时转成 ParamDef（key/group/desc 转 std::string）。
struct Row {
    const char* key;
    const char* group;
    const char* desc;
    ParamDef::Type type;
    double mn;
    double mx;
    double dflt;
};

// ---- 分组 id（顺序即 toJson 输出顺序）----
const char* G_WORLD = "world";        // 世界与运行
const char* G_LIGHT = "light";        // 光照
const char* G_FOOD = "food";          // 食物（自由球）
const char* G_PRESSURE = "pressure";  // 生存压力
const char* G_PRODUCER = "producer";  // 生产者（光合）
const char* G_CYANO = "cyano";        // 蓝细菌网络
const char* G_GENOME = "genome";      // 基因组（遗传）
const char* G_BALL = "ball";          // 球动力学
const char* G_NUCLEUS = "nucleus";    // 核动力学

// 完整参数表。约定：min/max 是参数页的"硬范围"（校验拒绝越界），
// dflt 是参数页"恢复默认"的目标值，与 EnvControl 各轴"标准"档一致。
const Row kRows[] = {
    // ===== world：世界与运行 =====
    {"width", G_WORLD, "世界宽度（环面横向尺寸）", ParamDef::Type::Double, 100, 10000, 1024.0},
    {"height", G_WORLD, "世界高度（环面纵向尺寸）", ParamDef::Type::Double, 100, 10000, 1024.0},
    {"seed", G_WORLD, "随机种子；同种子+同参数可完全复现", ParamDef::Type::Int, 0, 4294967295.0, 42.0},
    {"ticks", G_WORLD, "总模拟帧数；跑满后模拟结束", ParamDef::Type::Int, 1, 1000000000.0, 1000.0},
    {"nuclei", G_WORLD, "初始核数量；越多竞争越激烈", ParamDef::Type::Int, 0, 100000, 12.0},
    {"balls", G_WORLD, "初始自由球数量（启动库存，吃完即止）", ParamDef::Type::Int, 0, 100000, 64.0},
    {"cyano_nodes", G_WORLD, "初始蓝细菌节点数（第一营养级规模）", ParamDef::Type::Int, 0, 100000, 512.0},
    {"sense_radius", G_WORLD, "感知层探测半径；越大核看得越远（感知负担也越重）", ParamDef::Type::Double, 1, 1000, 24.0},
    {"max_fps", G_WORLD, "实时模式帧率上限；0 = 不限速跑满", ParamDef::Type::Double, 0, 1000, 0.0},
    {"render_interval", G_WORLD, "浏览器画面快照间隔帧数；越小越流畅（前端负载越高）", ParamDef::Type::Int, 0, 10000, 1.0},
    {"autosave_interval", G_WORLD, "时间轴检查点间隔帧数；0 关闭", ParamDef::Type::Int, 0, 1000000, 200.0},

    // ===== light：光照 =====
    {"sun_base_level", G_LIGHT, "阳光基准强度（1=标准）；调大生产者更繁盛", ParamDef::Type::Double, 0, 5, 1.0},
    {"sun_day_amplitude", G_LIGHT, "昼夜波动幅度（0=无昼夜）；调大昼夜差异更剧烈", ParamDef::Type::Double, 0, 5, 0.5},
    {"sun_day_period_frames", G_LIGHT, "昼夜周期帧数；调小昼夜交替更快", ParamDef::Type::Double, 0, 1000000, 1000.0},
    {"sun_vertical_gradient", G_LIGHT, "垂直光照梯度（顶部更亮）；0 = 无梯度", ParamDef::Type::Double, 0, 1, 0.2},
    {"sun_noise_amplitude", G_LIGHT, "局部阳光扰动幅度；调大光斑更明显", ParamDef::Type::Double, 0, 5, 0.2},
    {"sun_noise_frequency", G_LIGHT, "局部扰动空间频率；调大光斑更细碎", ParamDef::Type::Double, 0, 10, 1.0},
    {"sun_noise_speed", G_LIGHT, "局部扰动时间漂移速度；调大光斑移动更快", ParamDef::Type::Double, 0, 10, 0.5},
    {"cyano_light_half_saturation", G_LIGHT, "光合半饱和光强；调大需更强光才接近满速", ParamDef::Type::Double, 0, 10, 0.5},
    {"light_warmth", G_LIGHT, "光色温（0 冷蓝 1 暖红），仅影响渲染氛围", ParamDef::Type::Double, 0, 1, 0.5},
    {"light_sample_grid", G_LIGHT, "阳光空间采样网格密度（每边格数）", ParamDef::Type::Int, 4, 64, 8.0},

    // ===== food：食物（自由球能量经济学）=====
    {"ball_energy", G_FOOD, "单个自由球含能量；调大捕食收益更高", ParamDef::Type::Double, 0.01, 1000, 10.0},
    {"ball_radius", G_FOOD, "自由球半径（渲染与碰撞）", ParamDef::Type::Double, 0.1, 100, 1.0},
    {"ball_vel_range", G_FOOD, "新生球初速度随机范围（±）", ParamDef::Type::Double, 0, 50, 5.0},
    {"ball_initial_energy", G_FOOD, "新生自由球初始能量", ParamDef::Type::Double, 0, 1000, 10.0},
    {"ball_energy_cap", G_FOOD, "单球储能上限", ParamDef::Type::Double, 0, 10000, 500.0},
    {"ball_spawn_probability", G_FOOD, "每帧随机补充自由球概率（0=只靠蓝细菌产球）", ParamDef::Type::Double, 0, 1, 0.0},
    {"ball_spawn_cost", G_FOOD, "蓝细菌产出一个自由球的能量成本", ParamDef::Type::Double, 0, 1000, 30.0},
    {"ball_spawn_min_dist", G_FOOD, "产球出生点距产者的最小环面距离", ParamDef::Type::Double, 0, 1000, 10.0},
    {"ball_spawn_max_dist", G_FOOD, "产球出生点距产者的最大环面距离", ParamDef::Type::Double, 1, 2000, 30.0},
    {"ball_loss_cost", G_FOOD, "球脱附/被抢时对原主的扣能；调大丢球代价更高", ParamDef::Type::Double, 0, 1000, 5.0},
    {"detach_radius", G_FOOD, "球距核超过该距离脱附变自由", ParamDef::Type::Double, 10, 2000, 60.0},
    {"regrow_food", G_FOOD, "人工补食开关（蓝细菌之外的第二能量源，默认关）", ParamDef::Type::Bool, 0, 1, 0.0},

    // ===== pressure：生存压力 =====
    {"basal_cost", G_PRESSURE, "基础代谢：核每帧固定能量流逝", ParamDef::Type::Double, 0, 10, 0.01},
    {"respiration_base", G_PRESSURE, "核呼吸损耗基数（运动的固定生理成本）", ParamDef::Type::Double, 0, 10, 0.008},
    {"speed_cost_k", G_PRESSURE, "速度代价系数；调大跑得快更耗能", ParamDef::Type::Double, 0, 1, 0.005},
    {"speed_cost_exponent", G_PRESSURE, "速度代价指数（>1 超线性）；越大高速惩罚越狠", ParamDef::Type::Double, 1, 4, 1.5},
    {"reproduction_cost_ratio", G_PRESSURE, "繁殖代价：父核消耗 = 比值 × 繁殖阈值", ParamDef::Type::Double, 0, 2, 0.4},
    {"child_energy_ratio", G_PRESSURE, "子核初始能量 = 比值 × 繁殖阈值", ParamDef::Type::Double, 0, 1, 0.5},
    {"reproduction_offset", G_PRESSURE, "子核出生位置相对父核的随机偏移（±）", ParamDef::Type::Double, 0, 1000, 25.0},
    {"mutation_coefficient", G_PRESSURE, "全局变异系数：子代扰动 = 个体变异率 × 该系数", ParamDef::Type::Double, 0, 10, 1.0},
    {"water_cost_multiplier", G_PRESSURE, "水域移动耗能倍率", ParamDef::Type::Double, 1, 10, 1.8},

    // ===== producer：生产者（光合速率族）=====
    {"cyano_photosynthesis_rate", G_PRODUCER, "最大光合速率；调大生产者能量积累更快", ParamDef::Type::Double, 0, 10, 1.0},
    {"cyano_metabolism_rate", G_PRODUCER, "蓝细菌节点每帧代谢消耗", ParamDef::Type::Double, 0, 10, 0.02},
    {"cyano_reproduction_probability", G_PRODUCER, "节点分裂概率；调大扩张更快", ParamDef::Type::Double, 0, 1, 0.01},
    {"cyano_reproduction_offset", G_PRODUCER, "子节点相对亲代的最大散布距离", ParamDef::Type::Double, 0, 1000, 125.0},

    // ===== cyano：蓝细菌网络 =====
    {"cyano_colonies", G_CYANO, "初始聚落（菌落）数量；调大节点更分散", ParamDef::Type::Int, 0, 1000, 4.0},
    {"cyano_initial_energy", G_CYANO, "节点初始能量", ParamDef::Type::Double, 0, 1000, 40.0},
    {"cyano_energy_cap", G_CYANO, "单节点储能上限", ParamDef::Type::Double, 0, 10000, 100.0},
    {"cyano_link_distance", G_CYANO, "节点自动连接距离；调大网络更密", ParamDef::Type::Double, 1, 2000, 60.0},
    {"cyano_max_links", G_CYANO, "每节点连接数上限", ParamDef::Type::Int, 0, 64, 3.0},
    {"cyano_link_check_interval", G_CYANO, "连线维护间隔帧数；调大省 CPU 但响应慢", ParamDef::Type::Int, 1, 1000, 1.0},
    {"cyano_transfer_rate", G_CYANO, "连线能量传输比例（富→穷）", ParamDef::Type::Double, 0, 1, 0.1},
    {"cyano_transfer_deadband", G_CYANO, "能量传输死区（差值不超过它不传输）", ParamDef::Type::Double, 0, 100, 1.0},
    {"cyano_min_node_separation", G_CYANO, "节点最小间距；过近则取消分裂", ParamDef::Type::Double, 0, 1000, 10.0},
    {"cyano_predation_radius", G_CYANO, "核捕食节点判定半径（0=禁捕食）", ParamDef::Type::Double, 0, 1000, 12.0},
    {"cyano_conversion_mode", G_CYANO, "转化率公式形态：0=常数 1=速度反比 2=幂律 3=仅加速度", ParamDef::Type::Int, 0, 3, 1.0},
    {"cyano_conversion_rate", G_CYANO, "转化率基准系数；调大捕食收益更高", ParamDef::Type::Double, 0, 100, 1.0},
    {"cyano_conversion_exponent", G_CYANO, "模式 2 的幂指数；调大机动性收益差距更悬殊", ParamDef::Type::Double, 0, 5, 1.0},
    {"cyano_conversion_speed_ref", G_CYANO, "速度归一化参考值", ParamDef::Type::Double, 1, 10000, 20.0},
    {"cyano_conversion_accel_ref", G_CYANO, "加速度归一化参考值", ParamDef::Type::Double, 0.01, 10000, 6.0},
    {"cyano_conversion_w_speed", G_CYANO, "转化率中速度项权重", ParamDef::Type::Double, 0, 10, 1.0},
    {"cyano_conversion_w_accel", G_CYANO, "转化率中加速度项权重", ParamDef::Type::Double, 0, 10, 1.0},
    {"cyano_conversion_min_rate", G_CYANO, "转化率下限（防止收益被压到 0）", ParamDef::Type::Double, 0, 100, 0.1},
    {"cyano_conversion_max_rate", G_CYANO, "转化率上限（防止收益爆炸）", ParamDef::Type::Double, 0, 1000, 1.0},
    {"cyano_hue_jitter", G_CYANO, "子节点色相抖动（度）；调大后代颜色差异更明显", ParamDef::Type::Double, 0, 180, 8.0},

    // ===== genome：基因组（min/max = 变异 Clamp 范围；default = 初始化 Init 范围中值）=====
    // 来源：V2-SPEC-AMEND-1 §二.1（nucleus*/force*）+ v2.3.1 NucleusParams.h（其余）。
    // 亲和力三键是归一化权重分量（子代归一化到和 1），这里的 min 是变异裁剪下限。
    {"affinity_resource", G_GENOME, "对 Resource 球的亲和力权重分量（归一化和为 1）", ParamDef::Type::Double, 0.0001, 10, 0.55},
    {"affinity_scout", G_GENOME, "对 Scout 球的亲和力权重分量（归一化和为 1）", ParamDef::Type::Double, 0.0001, 10, 0.55},
    {"affinity_shield", G_GENOME, "对 Shield 球的亲和力权重分量（归一化和为 1）", ParamDef::Type::Double, 0.0001, 10, 0.55},
    {"orbit_radius_resource", G_GENOME, "Resource 球期望环绕半径", ParamDef::Type::Double, 10, 300, 115.0},
    {"orbit_radius_scout", G_GENOME, "Scout 球期望环绕半径", ParamDef::Type::Double, 10, 300, 115.0},
    {"orbit_radius_shield", G_GENOME, "Shield 球期望环绕半径", ParamDef::Type::Double, 10, 300, 115.0},
    {"absorb_preference_resource", G_GENOME, "吸收 Resource 球的能量倍率", ParamDef::Type::Double, 0.1, 3, 1.0},
    {"absorb_preference_scout", G_GENOME, "吸收 Scout 球的能量倍率", ParamDef::Type::Double, 0.1, 3, 1.0},
    {"absorb_preference_shield", G_GENOME, "吸收 Shield 球的能量倍率", ParamDef::Type::Double, 0.1, 3, 1.0},
    {"repel_strength_resource", G_GENOME, "Resource 球过近时的排斥强度", ParamDef::Type::Double, 0.1, 6, 1.75},
    {"repel_strength_scout", G_GENOME, "Scout 球过近时的排斥强度", ParamDef::Type::Double, 0.1, 6, 1.75},
    {"repel_strength_shield", G_GENOME, "Shield 球过近时的排斥强度", ParamDef::Type::Double, 0.1, 6, 1.75},
    {"nucleus_sight_radius", G_GENOME, "察觉其他核的距离（社会感知半径）", ParamDef::Type::Double, 10, 600, 170.0},
    {"nucleus_personal_space", G_GENOME, "个人空间半径（小于它对邻核施斥力）", ParamDef::Type::Double, 5, 200, 47.5},
    {"nucleus_repel_strength", G_GENOME, "对邻核的斥力强度", ParamDef::Type::Double, 0.1, 20, 3.25},
    {"nucleus_cohesion", G_GENOME, "向核群聚拢倾向（0 = 独行）", ParamDef::Type::Double, 0, 10, 1.5},
    {"force_range", G_GENOME, "施力控制范围（可给范围内的球点名加速）", ParamDef::Type::Double, 10, 400, 90.0},
    {"force_cost_k", G_GENOME, "施力代价系数（cost = k × |a|²，走账本耗散）", ParamDef::Type::Double, 0.0001, 1, 0.0275},
    {"max_speed", G_GENOME, "核最大速度", ParamDef::Type::Double, 5, 300, 65.0},
    {"max_accel", G_GENOME, "核最大加速度（绝对量）", ParamDef::Type::Double, 0.5, 50, 10.5},
    {"energy_threshold", G_GENOME, "繁殖阈值（攒够才分家）", ParamDef::Type::Double, 20, 1000, 140.0},
    {"mutation_rate", G_GENOME, "个体变异率（自身也遗传）", ParamDef::Type::Double, 0.001, 0.5, 0.105},
    {"cyano_preference", G_GENOME, "捕食蓝细菌倾向（0 = 完全不吃）", ParamDef::Type::Double, 0, 1, 0.5},
    {"cyano_digestion", G_GENOME, "消化效率（捕食转化与消化冷却的基础）", ParamDef::Type::Double, 0.05, 1, 0.55},

    // ===== ball：球动力学 =====
    {"ball_damping", G_BALL, "球速度阻尼（每帧保留比例）", ParamDef::Type::Double, 0.5, 1, 0.92},
    {"radial_k", G_BALL, "轨道径向弹簧系数；调大球群轨道更紧", ParamDef::Type::Double, 0, 1, 0.05},
    {"tangential_k", G_BALL, "切向环绕驱动系数；调大公转更快", ParamDef::Type::Double, 0, 10, 1.0},
    {"near_repel_radius", G_BALL, "球近核排斥触发半径", ParamDef::Type::Double, 1, 200, 8.0},
    {"ball_ball_repel_radius", G_BALL, "球间短程排斥半径", ParamDef::Type::Double, 1, 100, 6.0},
    {"ball_ball_repel_k", G_BALL, "球间短程排斥强度", ParamDef::Type::Double, 0, 10, 0.5},
    {"same_owner_attract_radius", G_BALL, "同核球相互吸引半径（0 = 关闭）", ParamDef::Type::Double, 0, 200, 0.0},
    {"same_owner_attract_k", G_BALL, "同核球相互吸引强度", ParamDef::Type::Double, 0, 1, 0.05},
    {"ball_max_speed_shield", G_BALL, "护盾球速度上限", ParamDef::Type::Double, 1, 500, 15.0},
    {"ball_max_speed_resource", G_BALL, "资源球速度上限", ParamDef::Type::Double, 1, 500, 20.0},
    {"ball_max_speed_scout", G_BALL, "侦察球速度上限（三型中最快）", ParamDef::Type::Double, 1, 500, 30.0},
    {"follow_coupling", G_BALL, "归属核轨道弹簧加强倍数（快核甩不掉球）", ParamDef::Type::Double, 0, 100, 4.0},
    {"follow_boost", G_BALL, "跟随加速倍率（球限速 = max(类型上限, 核速×该值)）", ParamDef::Type::Double, 0, 10, 1.5},
    {"bounce_restitution", G_BALL, "边界反弹恢复系数（wrap_boundary=1 时忽略）", ParamDef::Type::Double, 0, 1, 1.0},
    {"wrap_boundary", G_BALL, "true=环面穿越边界；false=边界反弹", ParamDef::Type::Bool, 0, 1, 1.0},

    // ===== nucleus：核动力学 =====
    {"nucleus_damping", G_NUCLEUS, "核速度阻尼（每帧保留比例）", ParamDef::Type::Double, 0.5, 1, 0.9},
    {"nucleus_min_separation", G_NUCLEUS, "核最小间距（硬分离 + 贴脸跳过互作）", ParamDef::Type::Double, 0, 200, 12.0},
    {"nucleus_repel_k", G_NUCLEUS, "贴脸斥力刚度", ParamDef::Type::Double, 0, 100, 2.0},
    {"forage_k", G_NUCLEUS, "核朝蓝细菌节点觅食的加速度系数", ParamDef::Type::Double, 0, 10, 1.0},
    {"forage_radius", G_NUCLEUS, "核觅食查询半径", ParamDef::Type::Double, 10, 2000, 100.0},
    {"wander_k", G_NUCLEUS, "随机游走加速度系数", ParamDef::Type::Double, 0, 10, 0.5},
    {"wander_turn_rate", G_NUCLEUS, "游走方向每帧最大转向角（弧度）", ParamDef::Type::Double, 0, 3, 0.5},
    {"nucleus_init_energy_ratio", G_NUCLEUS, "初始核能量 = 比值 × 繁殖阈值", ParamDef::Type::Double, 0, 2, 1.0},
};

static_assert(sizeof(kRows) / sizeof(kRows[0]) >= 110,
              "ParamSchema 表必须覆盖至少 110 个键（V2-SPEC-AMEND-1 后的验收线）");

// ---- JSON 小工具 ----

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// 数字输出：%.10g 足够本表全部字面量无损往返；整数值补 ".0" 保持 double 语义。
std::string jsonNum(double v) {
    if (!std::isfinite(v)) return "0.0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    std::string s(buf);
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

const char* typeName(ParamDef::Type t) {
    switch (t) {
        case ParamDef::Type::Int: return "int";
        case ParamDef::Type::Double: return "double";
        case ParamDef::Type::Bool: return "bool";
    }
    return "double";
}

// ---- 严格解析（validate 用）----

std::string trimStr(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 整个字符串必须是一个整数（可选正负号 + 纯数字），防 "3abc" / 溢出。
bool parseIntStrict(const std::string& text, long long& out) {
    const std::string t = trimStr(text);
    if (t.empty() || t.size() > 19) return false;
    size_t i = 0;
    bool neg = false;
    if (t[0] == '+' || t[0] == '-') {
        neg = t[0] == '-';
        i = 1;
        if (t.size() == 1) return false;
    }
    long long v = 0;
    for (; i < t.size(); ++i) {
        if (t[i] < '0' || t[i] > '9') return false;
        v = v * 10 + (t[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

// 整个字符串必须是一个有限十进制数。
bool parseDoubleStrict(const std::string& text, double& out) {
    const std::string t = trimStr(text);
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == nullptr) return false;
    if (static_cast<size_t>(end - t.c_str()) != t.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

const std::vector<ParamDef>& buildTable() {
    static const std::vector<ParamDef> table = [] {
        std::vector<ParamDef> v;
        v.reserve(sizeof(kRows) / sizeof(kRows[0]));
        for (const Row& r : kRows) {
            ParamDef d;
            d.key = r.key;
            d.group = r.group;
            d.desc = r.desc;
            d.type = r.type;
            d.min = r.mn;
            d.max = r.mx;
            d.dflt = r.dflt;
            v.push_back(std::move(d));
        }
        return v;
    }();
    return table;
}

} // namespace

const std::vector<ParamDef>& ParamSchema::all() { return buildTable(); }

const ParamDef* ParamSchema::find(const std::string& key) {
    for (const ParamDef& d : all()) {
        if (d.key == key) return &d;
    }
    return nullptr;
}

const char* ParamSchema::groupName(const std::string& groupId) {
    if (groupId == G_WORLD) return "世界与运行";
    if (groupId == G_LIGHT) return "光照";
    if (groupId == G_FOOD) return "食物（自由球）";
    if (groupId == G_PRESSURE) return "生存压力";
    if (groupId == G_PRODUCER) return "生产者（光合）";
    if (groupId == G_CYANO) return "蓝细菌网络";
    if (groupId == G_GENOME) return "基因组（遗传）";
    if (groupId == G_BALL) return "球动力学";
    if (groupId == G_NUCLEUS) return "核动力学";
    return nullptr;
}

bool ParamSchema::validate(const std::string& key, const std::string& value, std::string& err) {
    const ParamDef* d = find(key);
    if (d == nullptr) {
        err = "未知参数键: " + key;
        return false;
    }
    const std::string v = trimStr(value);
    if (v.empty()) {
        err = "值为空: " + key;
        return false;
    }
    switch (d->type) {
        case ParamDef::Type::Int: {
            long long iv = 0;
            if (!parseIntStrict(v, iv)) {
                err = "不是合法整数: " + key + " = " + value;
                return false;
            }
            if (static_cast<double>(iv) < d->min || static_cast<double>(iv) > d->max) {
                err = "超出范围 [" + std::to_string(static_cast<long long>(d->min)) + ", " +
                      std::to_string(static_cast<long long>(d->max)) + "]: " + key;
                return false;
            }
            return true;
        }
        case ParamDef::Type::Double: {
            double dv = 0.0;
            if (!parseDoubleStrict(v, dv)) {
                err = "不是合法数值: " + key + " = " + value;
                return false;
            }
            if (dv < d->min || dv > d->max) {
                err = "超出范围 [" + jsonNum(d->min) + ", " + jsonNum(d->max) + "]: " + key;
                return false;
            }
            return true;
        }
        case ParamDef::Type::Bool: {
            const std::string lv = lowerCopy(v);
            if (lv == "on" || lv == "true" || lv == "yes" || lv == "1" || lv == "off" ||
                lv == "false" || lv == "no" || lv == "0") {
                return true;
            }
            err = "布尔值只能是 on/off/true/false/1/0/yes/no: " + key;
            return false;
        }
    }
    err = "未知参数类型: " + key;
    return false;
}

std::string ParamSchema::toJson() {
    // 按 kRows 的分组出现顺序输出；每组一个 {"id","name","params":[...]}。
    std::string out;
    out.reserve(16 * 1024);
    out += "{\"count\":";
    out += std::to_string(all().size());
    out += ",\"groups\":[";
    bool firstGroup = true;
    for (const ParamDef& d : all()) {
        // 该组是否已经输出过（表本身按组排序，组头只在首成员处输出）。
        bool isNew = true;
        for (const ParamDef& prev : all()) {
            if (&prev == &d) break;
            if (prev.group == d.group) {
                isNew = false;
                break;
            }
        }
        if (!isNew) continue;
        if (!firstGroup) out += ',';
        firstGroup = false;
        out += "{\"id\":\"";
        out += jsonEscape(d.group);
        out += "\",\"name\":\"";
        out += jsonEscape(groupName(d.group) == nullptr ? d.group : groupName(d.group));
        out += "\",\"params\":[";
        bool firstParam = true;
        for (const ParamDef& p : all()) {
            if (p.group != d.group) continue;
            if (!firstParam) out += ',';
            firstParam = false;
            out += "{\"key\":\"";
            out += jsonEscape(p.key);
            out += "\",\"desc\":\"";
            out += jsonEscape(p.desc);
            out += "\",\"type\":\"";
            out += typeName(p.type);
            out += "\",\"min\":";
            out += jsonNum(p.min);
            out += ",\"max\":";
            out += jsonNum(p.max);
            out += ",\"default\":";
            out += jsonNum(p.dflt);
            out += '}';
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

} // namespace agentsim::app
