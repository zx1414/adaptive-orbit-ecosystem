#pragma once
// agentsim - app/SaveStore.h（R5-C 存档系统）
// 存档目录管理：saves/<名字>/{save.bin, meta.txt, timeline.bin}。
//   save.bin    = 版本头(magic ASAV + version=1) + cfg 关键字段 + policyName
//                 + Simulation::serializeState 的全量实体态（小端二进制）。
//   meta.txt    = name/frame/seed/aliveNuclei/savedAt 的 key=value 文本（人可读）。
//   timeline.bin= Timeline 的检查点序列（由 Timeline::exportTo/importFrom 读写）。
// 确定性契约：cfg 与实体态全覆盖（见 Simulation.cpp 布局注释），读档 =
// loadHeader 取 cfg -> 用它构造 Simulation -> load 恢复实体态，之后 stateHash()
// 与存档时逐位相同、续跑与"不存档直接跑"逐位一致（test_save.cpp 把守）。
#include <iosfwd>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agentsim {

class Simulation;
struct SimConfig;

namespace app {

class SaveStore {
public:
    struct Meta {
        std::string name;
        long long frame = 0;
        std::uint64_t seed = 0;
        std::size_t aliveNuclei = 0;
        std::string savedAt;      // "YYYY-MM-DD HH:MM:SS"
    };

    // 名字合法性：非空、<=64 字符、不含 / \\ : * ? " < > | 与 ".."、不含控制字符、
    // 不以空格开头结尾、不是 Windows 保留设备名（CON/PRN/AUX/NUL/COM1-9/LPT1-9）。
    static bool validName(const std::string& name, std::string& err);

    // 存档：saves/<name>/save.bin + meta.txt（目录不存在则创建；已存在则覆盖）。
    static bool save(const std::string& name, const Simulation& sim,
                     const SimConfig& cfg, const std::string& policyName,
                     std::string& err);

    // 读档头：只解析 cfg 关键字段与 policyName（不动任何 Simulation）。
    // 正确的完整读档流程：loadHeader -> 用返回 cfg 构造 Simulation -> load。
    static bool loadHeader(const std::string& name, SimConfig& cfg,
                           std::string& policyName, std::string& err);

    // 读档：恢复实体状态到 sim、配置到 cfg。sim 必须已用 loadHeader 的 cfg 构造
    //（terrain_/temp_ 是构造期实体，事后不可换）。版本不符/损坏/名字非法返回 false。
    static bool load(const std::string& name, Simulation& sim,
                     SimConfig& cfg, std::string& policyName, std::string& err);

    // 全部存档列表，按 savedAt 倒序（同刻按名字升序，保证确定性）。
    static std::vector<Meta> list();

    static bool remove(const std::string& name, std::string& err);
    static bool rename(const std::string& oldName, const std::string& newName,
                       std::string& err);
};

    // ---- 内部共享通道（SaveStore 与 Timeline 共用的 cfg 块读写；非公共 API）----
namespace savedetail {
// cfg 关键字段 + policyName 的小端块读写（save.bin 与 timeline.bin 的同一节）。
bool writeCfgBlock(std::ostream& os, const SimConfig& cfg,
                   const std::string& policyName, std::string& err);
bool readCfgBlock(std::istream& is, SimConfig& cfg,
                  std::string& policyName, std::string& err);
} // namespace savedetail

} // namespace app
} // namespace agentsim
