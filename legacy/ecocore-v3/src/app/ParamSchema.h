// agentsim - app/ParamSchema.h
// R1-C：全部可调参数的单一事实来源（schema 元数据）。
//
// 设计边界（任务卡 R1-C）：
//   - 本轮 Config.h 不可改，ParamSchema **只做元数据**
//     （key/group/desc/type/min/max/default），不持有任何 Config 指针。
//     key -> SimConfig 字段的映射由主代理接线（见交付报告清单）。
//   - 键名沿用 v2.3.1 的 snake_case 风格；字段语义以 agentsim 为准：
//     Food 实体已整体移除（蓝细菌是唯一生产者），"food" 组描述的是
//     自由球（蓝细菌产球 + 启动库存）的能量经济学；控制台字符渲染
//     已被用户点名排除，旧版的 visualization/grid_* 等键不再收录。
//   - toJson() 供 GET /schema；validate() 供参数页/HTTP 细调做解析+范围校验。
#pragma once
#include <string>
#include <vector>

namespace agentsim::app {

struct ParamDef {
    enum class Type { Int, Double, Bool };
    std::string key;    // 配置键，如 "sun_base_level"
    std::string group;  // world / light / food / pressure / producer / cyano / genome / ball / nucleus
    std::string desc;   // 中文一句话说明
    Type type = Type::Double;
    double min = 0.0;   // 校验范围下限
    double max = 0.0;   // 校验范围上限
    double dflt = 0.0;  // 默认值（与 EnvControl 的"标准"档一致）
};

class ParamSchema {
public:
    static const std::vector<ParamDef>& all();
    static const ParamDef* find(const std::string& key);

    // GET /schema 用：按 group 分组的 JSON。
    // {"groups":[{"id":"light","name":"光照","params":[{"key":..,"desc":..,
    //   "type":"double","min":..,"max":..,"default":..},...]},...]}
    static std::string toJson();

    // 解析 + 范围校验：通过返回 true 且 err 为空；失败返回 false 并给出中文原因。
    // 注意：validate 是"校验"（越界即拒绝）；EnvControl 细调是"钳制"（越界收敛到
    // min/max 再写入）——语义刻意不同，分别服务参数页与实时滑杆。
    static bool validate(const std::string& key, const std::string& value, std::string& err);

    // 分组展示名（未登记的组原样返回 id）。
    static const char* groupName(const std::string& groupId);
};

} // namespace agentsim::app
