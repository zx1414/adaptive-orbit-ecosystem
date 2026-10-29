# EcoCore v3

从零重建的 C++17 生态模拟核心。旧版 v2.3.1 已冻结在 Git 标签 legacy-v2.3.1，不再参与构建。

## 设计核心

- **环境零产能**：Environment 只有边界和光照场，没有能量池、不会随机生成食物。
- **生产者是唯一能量来源**：Producer 抽象基类，当前实现 PhotoProducer，把光照转成生物量。
- **能量账本**：所有能量变化经过 EnergyLedger；转移零和、耗散记账、死亡结算，每个 tick 校验
  injected - dissipated == 全部存活实体能量之和。
- **决策层可插拔**：Observation(28 维) + Action + IPolicy；规则策略与固定策略共用接口，后续可直接加神经网络。
- **依赖方向**：core ← decision；core ← world；World 是 M1 组合根，持有 producers/agents；app 负责装配与驱动。决策层不反向依赖 world。

## 构建

需要 Visual Studio 2022 的 C++ 桌面开发组件。

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
~~~

生成 build/simulator.exe 和 build/unit_tests.exe。

也可以用 CMake 3.16+（若已安装）：

~~~powershell
cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
~~~

## 运行

~~~powershell
# 无头跑 5000 tick，检查能量审计
build\simulator.exe --no-render --ticks 5000 --seed 42 --audit

# 使用第二种策略
build\simulator.exe --no-render --ticks 1000 --policy stub --audit

# 控制台画面（每 500 tick 绘制一次）
build\simulator.exe --ticks 5000 --policy rule --render-interval 500

# 能量收支与状态哈希
build\simulator.exe --no-render --ticks 5000 --energy-summary --state-hash build/hash.txt

# 种群/能量/基因趋势 CSV（每 100 tick 一行）
build\simulator.exe --no-render --ticks 20000 --stats build/stats.csv --stats-interval 100

# 实验性平衡参数：局部遮荫 + 收获率 + 猎物保留量 + 寿命
build\simulator.exe --no-render --ticks 20000 --producer-crowding 0.5 --producer-radius 120 --agent-harvest 0.1 --agent-reserve 5 --agent-lifespan 2000 --audit
~~~

参数：--width --height --seed --ticks --producers --agents --policy --render/--no-render
--render-interval --energy-summary --state-hash --stats --stats-interval --bench --audit --help --version。

平衡参数：--photo --producer-maint --producer-repro --producer-max --producer-crowding
--producer-radius --agent-metab --agent-harvest --agent-reserve --agent-repro --agent-max
--agent-lifespan --sense-radius --mutation。

统计 CSV 列：tick、producers、agents、总能量、注入、耗散、转移、生产者/Agent 平均能量、
生产者/Agent 出生与死亡、各基因均值、audit_ok。默认参数与平衡实验结论见 [docs/BALANCE.md](docs/BALANCE.md)。

## 验证

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1
~~~

包含：MSVC /W4 /WX 构建、决策层依赖守卫、单元测试、双跑确定性、零生产者灭绝、stub 策略冒烟。

当前 14 个单元测试覆盖能量守恒/截断/死亡、初始化封存、亚 epsilon 耗散记账、环境光照有限性、
生产者光合是唯一初级注入、策略确定性、观测版本校验、观测有限性、种群/基因统计、寿命死亡、
世界双跑哈希一致、零生产者最终灭绝。集成冒烟额外覆盖统计 CSV、状态哈希和退出码契约。

## 目录

- src/core：类型、数学、PCG32、时间、配置、事件
- src/decision：Observation/Action/IPolicy/Registry/RulePolicy/StubPolicy
- src/world：EnergyLedger、Environment、SpatialIndex、World、WorldView
- src/producers：Producer 基类、PhotoProducer、ProducerSystem
- src/agents：Agent、Sensors、Behavior、AgentSystem
- src/app：CLI、控制台渲染、CSV 统计
- tests/unit：零第三方断言框架与测试
- tools：构建与总检查脚本
- docs/ARCH.md、docs/ROADMAP.md、docs/BALANCE.md：架构、里程碑与平衡实验

## 后续

M2 的统计导出与参数化已完成；平衡实验表明默认基线仍是唯一稳健配置，低于上限的稳定共存需要 M2.5 的资源竞争模型。
M3 存档与快照、M4 新可视化、M5 NNetPolicy、M6 新扩展接口。详见 docs/ROADMAP.md。
