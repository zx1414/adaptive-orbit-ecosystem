// agentsim - core/Types.h
// 全项目基础类型别名与基础枚举。
#pragma once
#include <cstdint>

namespace agentsim {

using EntityId = std::uint64_t;
using Tick = std::uint64_t;

// 注：FoodState{Alive,Consumed} 已于 2026-11-01 删除（B 路取消食物球后成为死代码，
// 全项目零引用）。食物实体的概念已由蓝细菌节点（CyanoNode.alive）承接。

} // namespace agentsim
