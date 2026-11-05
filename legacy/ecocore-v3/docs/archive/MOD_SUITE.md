# 小球生态多样化 Mod 套件

> 面向**玩家 / mod 使用者**。实现细节与调参推导见各 `mods/*.cpp` 文件头与 `docs/MOD_PLAN.md`。

---

## 1. 这套 mod 解决什么问题

基线（不启用任何 mod），`--balls 1000 --nuclei 20 --frames 5000`，8 个种子实测：

```
RESULT survivors_min=0 survivors_max=1 survivors_mean=0.13
STATS   coexistence=0/8  monoculture=1/8  extinction=7/8
```

**8 个种子里 7 个全灭。** 能量总账（3 种子）：

```
ABSORB        in   4725.03     ← 吸收小球赚到的
BALL_LOSS     out  9375.00     ← 失球被罚掉的
```

### 根因

`src/World.cpp:287-289`——每当一颗归属球被别的核抢走，就对**旧主**罚 `ballLossCost = 5.0`：

```cpp
int prev = prevOwner_[i];
if (prev >= 0 && prev != best && prev < (int)nuclei_.size() && nuclei_[(size_t)prev].alive) {
    energy_.apply(nuclei_[(size_t)prev], -config_.ballLossCost, EnergyReason::BALL_LOSS);
}
```

而吸收一颗球最多只赚 `baseBallEnergy(1.0) × absorbPreference(≤3.0) = 3.0`。

> **罚 5.0，最多赚 3.0 —— 净能量必为负。而且只罚旧主，新主不用赔，是单向销毁。**

核越密集 → 被抢越频繁 → 罚得越狠 → 死得越快。20 个核在 2000×2000 里互相抢球，**200~330 帧内就全灭**。

小球生态崩塌的真正原因不是小球不够，而是**小球被抢走的代价远高于吸收收益**。

---

## 2. 实测效果总表

同 8 个种子、5000 帧、**编译零新增警告、不启用 mod 时输出逐字节一致**：

| 配置 | 存活 min/max | 共存 | 独苗 | **全灭** | evenness | 零和校验 |
|---|---|---|---|---|---|---|
| **基线** | 0/1 | 0/8 | 1/8 | **7/8** | 0.000 | — |
| `ball_loss_relief` | 1/7 | 3/8 | 2/8 | **3/8** | 0.876 | — |
| **`relief`+`symbiosis`+`spill`** ⭐ | **1/7** | **3/8** | 2/8 | **3/8** | **0.941** | ✅ `SYMBIOSIS 5.9=5.9` |
| `relief`+`guild`+`symbiosis` | 0/1 | 0/8 | 1/8 | 7/8 | 0.000 | ✅ `GUILD 379.4=379.4` |
| 全部 7 个 | 0/0 | 0/8 | 0/8 | 8/8 | — | ✅ 全部 in==out |

⭐ **推荐配置**：`ball_loss_relief,symbiosis_bond,energy_spill`
**全灭率 7/8 → 3/8，存活上限 1 → 7，遗传多样性 evenness 0.000 → 0.941。**

### 零和校验（最重要的正确性信号）

严格零和 mod 的 `in` 与 `out` 必须**精确相等**，这比看存活数更能证明机制正确：

```
GUILD      257.2 / 257.2     ✅
SYMBIOSIS    5.9 /   5.9     ✅
SPILL        4.3 /   4.3     ✅
COMBAT      10.2 /  10.2     ✅
```

---

## 3. Mod 清单

| id | 名称 | 类型 | 作用 | 单独运行 |
|---|---|---|---|---|
| `ball_loss_relief` | 失球宽限 | 非零和 | **核心**：缓解 BALL_LOSS 单向销毁 | ✅ 有效 |
| `symbiosis_bond` | 核间共生 | 严格零和 | 富核向贫核零和输血 | ⚠️ 需 ≥2 核 |
| `energy_spill` | 能量溢出再分配 | 严格零和 | 富余核溢出给贫瘠核 | ⚠️ 需 ≥2 核 |
| `niche_guild` | 生态位行会 | 严格零和 | 按基因分派行会，同行会协作 | ✅ 有效 |
| `rare_ball_subsidy` | 稀有球型保育 | 非零和 | 偏好稀有小球的核获补贴 | ⚠️ 建议配合 relief |
| `season_cycle` | 季节轮回 | 非零和 | 四季轮换环境压力 | ✅ 有效 |
| `climate_event` | 气候突变 | 非零和 | 周期环境事件 | ⚠️ 效果温和 |

**零和 vs 非零和**：
- **严格零和**（`symbiosis_bond` / `energy_spill` / `niche_guild`）：能量只在核间搬运，系统总量不变。`in` 与 `out` **必须相等**。
- **非零和**（`ball_loss_relief` / `rare_ball_subsidy` / `season_cycle` / `climate_event`）：从虚空注入或抽走能量。`in` 与 `out` **故意不等**——这是设计意图。

### ⚠️ 配对型 mod 需要 ≥2 个核

`symbiosis_bond` 和 `energy_spill` 都要求**同时存在"富核"和"贫核"**。基线下多数种子全灭或只剩 1 个核，而**1 个核在数学上不可能既是富核又是贫核**——配对数恒为 0。

**所以它们单独运行时账目为空是预期行为，不是 bug。必须与 `ball_loss_relief` 组合使用。**

---

## 4. 核心 mod 详解：`ball_loss_relief`

### 机制

按稳定 `Nucleus::id` 逐帧追踪每个核的归属球数，与上帧快照差分得到掉球数，按阶梯返还部分罚金：

| 参数 | 值 | 说明 |
|---|---|---|
| `kInfantProtectionFrames` | 200 | **幼体保护**：首次出现后 200 帧，返还 0.9× |
| `kFreeSwitches` | 2 | 每 1200 帧窗口内前 2 次掉球**全额**返还 |
| `kReliefRatio` | 0.5 | 超出后返还 50% |
| `kMaxRefundBalls` | 6 | 单核单帧返还上限 |

### 关键设计一：幼体保护（与「免费次数」正交）

实测**纯失球核死亡速度约 2.5 球/帧**（181 帧被罚约 627 球）。
如果只有"每窗口前 N 次免费"这种**计数型**宽限，新核在 240 帧窗口内被罚 600 球却只能拿回 2 次——**且它在攒满额度前就已经死了**。

所以必须有一条**不依赖累计、不依赖存活时长**的通道：按 `firstSeenFrame` 连续生效的幼体保护。实测这一条把返还覆盖率从 12% 提到 37%。

### 关键设计二：返还不超过核心实收罚金（防爆炸）

早期版本按"存活核数 × 单核上限"给预算，结果 `relief+guild` 组合**种群爆炸到 maxNuclei=1000**。

逐项拆账后找到根因——**吸收有界、失球无界**：

| | relief 单独 | 爆炸时 | 是否有界 |
|---|---|---|---|
| `ABSORB in` | 3,662 | 283,626 (77×) | ✅ 球总数被 `initialBalls=1000` 锁死 |
| `BALL_LOSS out` | 6,200 | 220,635 (36×) | ❌ 随核数线性增长 |

核越多 → 归属 churn 越多 → 罚金越多 → 返还越多 → 核越有精力繁殖 → 核更多。**闭环正反馈。**

**最终修法**（与核数无关，从根上切断反馈）：
```cpp
budget = min( sqrt(alive) × kGlobalBallsPerSqrt × ballLossCost,
              ledgerDelta  × kRefundCapOfLedger /*0.6*/ )
```
`ledgerDelta` 是核心本帧**实收罚金**（`totalOut(BALL_LOSS)` 差分）。
**不变量：返还不超过核心实际收走的罚金 ⇒ relief 数学上不可能成为净能量源。**

### 关键设计三：吸收假阳性过滤

归属球减少有两个原因：**被抢走**（真失球）和**被吸收**（不是失球，核心不收费）。`BALL_ABSORBED` 事件只给全局个数，所以用全局预算：
```
可返还名额 = 本帧总掉球数 - 本帧总吸收数  （下限 0）
```
按各核掉球比例分配，**保证返还总量恒不超过真实失球量**——宁可少还，绝不超发。

---

## 5. 如何启用

### 方式一：启动器 Mod 页（推荐）
双击 `simulator.exe`（或 `启动器.bat`）→ **Mod 页** → 勾选并保存。

### 方式二：直接写 `mods.list`
在 exe 同目录创建 `mods.list`，每行一个 mod id，**顺序 = 优先级 = 事件派发顺序**：

```
ball_loss_relief
symbiosis_bond
energy_spill
```

`#` 开头是注释，空行忽略。

### 推荐配置

| 场景 | `mods.list` |
|---|---|
| **首选** | `ball_loss_relief` + `symbiosis_bond` + `energy_spill` |
| 保守 | 只 `ball_loss_relief` |
| 不想全开 | **不要同时开 `niche_guild` + `symbiosis_bond`**（收益互相抵消，见实测表） |
| ❌ 不建议 | 全部 7 个同开（实测 8/8 全灭，叠加饱和） |

---

## 6. 验证是否生效

```bash
simulator.exe --seed 42 --balls 1000 --nuclei 20 --frames 10000 --no-render --energy-summary
```

检查输出末尾能量收支摘要：

```
  TERRITORY: in 5961.00, out 0.00      ← ball_loss_relief 生效
  SYMBIOSIS: in 5.88, out 5.88         ← 零和对称 = 实现正确
  SPILL:     in 4.25, out 4.25         ← 零和对称 = 实现正确
```

**三个判定标准：**
1. 启用的 mod 对应 `EnergyReason` 出现在账目里且非零
2. 零和 mod 的 `in == out`（精确对称）
3. 存活核数相对基线有提升

> ⚠️ 账目非零**不等于**有效。`season_cycle` 曾经出现 `SEASON in=1342` 但机制实际过弱——比不触发更隐蔽。

---

## 7. 确定性保证

- 所有随机**只走 `api.rng`**（与核心同种子），禁 `rand()` / 时间种子
- **不启用任何 mod 时，输出与基线逐字节一致**（已验证：10000 帧 seed 42 → `ABSORB 3238.81 / METABOLISM 197.36 / REPRODUCTION 3260.06 / BALL_LOSS 3820.00`，与改动前完全相同）
- 同一 `mods.list` + 同一种子 → 输出可复现
- mod 执行顺序 = `mods.list` 顺序

---

## 8. 平衡性回归测试

```powershell
& .\build\build.ps1 -OutName simulator          # 构建
& .\build\run_matrix.ps1 -Exe simulator -Seeds '1,2,3,5,7,13,42,99' -Frames 5000
```

输出汇总表 + `$env:TEMP\sim_build\mod_matrix.csv`。关键指标：

| 指标 | 含义 |
|---|---|
| `Surv` | min/max 存活核数 |
| `Coex` | 有 ≥2 核共存的种子数（**越高越多样**） |
| `Mono` | 只剩 1 核（单一独苗） |
| `Ext` | 全灭（**应尽量低**） |
| `Even` | 遗传参数聚类均匀度，1.0 = 完全多样，0 = 单一克隆 |
| `Ledger` | 各 `EnergyReason` 的 in/out（**零和 mod 应相等**） |

---

## 9. 开发者：新增 mod

见 `docs/MOD_DEV_GUIDE.md`（接口 + 登记）和 `src/MOD_SPEC.md`（红线 + 验收）。

登记 4 处：`mods/<id>.cpp`、`src/mods_registry.cpp`（声明 + 数组）、`CMakeLists.txt`、`build/build.ps1` 的 `$ModSources`。

### ⚠️ 编译时两个不能省的开关

```powershell
& .\build\build.ps1 -OutName simulator     # 构建
powershell -File build\deploy.ps1         # 部署到项目根的 simulator.exe
```

| 开关 | 不加会怎样 |
|---|---|
| **`/utf-8`** | 源码是 UTF-8 无 BOM，MSVC 会按系统代码页（GBK）解码，**报上千个语法错误** |
| **`/MT`** | 动态链接会依赖运行时 DLL。旧的 MinGW 构建依赖 `libstdc++-6.dll` / `libwinpthread-1.dll` / `libgcc_s_seh-1.dll`——**CLion 被卸载后这些 DLL 消失，`simulator.exe` 直接打不开**（`0xC0000135 STATUS_DLL_NOT_FOUND`） |

`/MT` 静态链接后 exe **零外部依赖**，只导入 `kernel32.dll` / `shell32.dll` / `ws2_32.dll` 三个系统 DLL。
`build.ps1` 每次构建后自动扫描导入表并打印 `deps: self-contained`，**防止将来不小心退回动态链接**；`deploy.ps1` 部署时也会再查一次。

### 三条最容易踩的红线

1. **跨帧状态必须 `static`**：`subscribe` 注册的 `std::function` 比 `World` 活得更久（见 `mod_api.h:21-22`）。用 `[&]` 捕获 `registerXxx` 内的局部变量 = **必然 `0xC0000005` 崩溃**。`/Zs` 语法检查**抓不到**，必须实跑。
2. **命名空间隔离**：不得定义与核心同名的类型（`SpatialGrid` 撞名 → 7 个编译错误）。自定义类型加 `Mod` 前缀。
3. **算术论证不能替代实跑**：本轮曾出现"算术推导正确但一跑就退化"的参数。
