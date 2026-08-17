# 核-小球生态演化模拟器

多智能体生态演化模拟器：小球（Ball）是环境中的基础资源与物理素材；核（Nucleus）是决策与进化单元，携带 22 个可遗传的连续参数，通过**吸引-排列-吸收**小球获取能量，与其他核**竞争/合作**，能量充足时**繁殖**，子代参数**变异**，经自然选择涌现多样化的生存策略。

纯 C++17 + STL 实现，无任何第三方依赖，控制台字符渲染，附带批量运行与聚类分析脚本（仅依赖 numpy）。

---

## 目录

1. [构建与运行](#构建与运行)
2. [命令行参数](#命令行参数)
3. [配置文件 env_config.txt](#配置文件-env_configtxt)
4. [数学模型](#数学模型)
5. [遗传参数与进化](#遗传参数与进化)
6. [采样与数据分析](#采样与数据分析)
7. [性能与确定性](#性能与确定性)
8. [文件结构](#文件结构)

---

## 构建与运行

### 依赖

- 编译器：g++ / MSVC / clang++，需支持 C++17
- 构建：CMake ≥ 3.14（或用 g++ 直接编译）
- 仅标准库，零第三方依赖（Windows 下 HTTP 服务器仅用系统自带 WinSock）

### 编译

```bash
# 方式一：g++ 直接编译（Windows 需链接系统网络库）
g++ -std=c++17 -O2 -Isrc -o simulator src/*.cpp mods/*.cpp -lws2_32 -lshell32

# 方式二：CMake（Windows 推荐 Ninja 生成器，见下方说明）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> 注意：项目路径含中文时 MinGW Makefiles 生成器可能报路径错误，改用 Ninja
> 或 MSVC 即可（CLion 自带 ninja）；纯命令行 g++ 直接编译不受影响。

### 运行

**双击 `simulator.exe` 直接进入启动器**（浏览器界面）：快速设置 + 启动模拟、逐球实时可视化、参数调节、mod 管理。

```bash
# 启动器模式（默认，无参数即进入；也可显式指定）
./simulator [--serve] [--port 8765] [--no-browser]

# 控制台模式（原行为：字符渲染 + 批处理）
./simulator --console --env env_config.txt --seed 42 --balls 1000 --nuclei 20
```

控制台画面：`n`/`N` = 核（`N` 表示能量已够繁殖），`s`/`w`/`c` = 护盾球/资源球/侦察球，底部为统计行。

### 启动器（浏览器界面）

双击 exe 自动打开浏览器（`http://127.0.0.1:<端口>/`，端口占用自动 +1），包含四部分：

- **运行页**：快速设置（seed/balls/nuclei/frames）→ 启动模拟；逐球实时渲染（轨迹拖尾、缩放平移、暂停/单步/停止/速度、悬停查看核参数）；运行中可点「保存」把当前世界存为存档；结束后可返回菜单或退出。
- **参数页**：约 80 个参数按 5 类分组编辑（含范围校验与恢复默认）；「保存为默认」写入 `env_config.txt`，命令行模式同样生效。
- **Mod 页**：两个标签——「玩法包」：勾选启用、↑↓ 调整优先级（列表中越靠前越先执行）、自动冲突提示（影响相同机制的 mod 给出黄条警告）；「材质包」：勾选启用、↑↓ 调整堆叠顺序（后者覆盖前者），调色板即时生效。配置分别写入 `mods.list` / `packs.list`，下次启动模拟生效。
- **存档页**：存档列表（缩略图/时间/帧号/种子/存活核），载入/重命名/删除；载入后停在暂停态，继续运行与不中断直跑**逐帧一致**（rng 状态完整往返）。存档文件在 exe 旁的 `saves/<存档名>/`（save.bin + meta.txt + thumb.png）。
- 材质包制作：在 exe 旁的 `resourcepacks/<包名>/` 放 `pack.txt`（name/desc/version/author）与 `colors.txt`（`mode = type_color` + `background/ball_shield/ball_worker/ball_scout/free_ball/nucleus = r,g,b`）；内置示例 `resourcepacks/classic_flat/`。
- 前端文件在 exe 旁的 `web/` 目录，开发期直接改文件刷新浏览器即可，无需重编译。

后端接口：`GET /status`（菜单/运行/暂停/结束）、`GET /state`（逐球二进制快照）、`GET /schema`（参数表）、`GET /mods`（mod 清单 + 冲突警告）、`GET /saves`（存档列表）、`GET /packs`（材质包清单）、`POST /run`、`POST /save-config`、`POST /set-mods`、`POST /set-packs`、`POST /save`、`POST /load`、`POST /delete-save`、`POST /rename-save`、`POST /save-thumb`、`POST /control`。

---

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--width <float>` | 2000 | 世界宽度 |
| `--height <float>` | 2000 | 世界高度 |
| `--seed <int>` | 42 | 随机种子（`std::mt19937`，同种子完全可复现） |
| `--balls <int>` | 1000 | 初始自由小球数量 |
| `--nuclei <int>` | 20 | 初始随机核数量 |
| `--frames <int>` | 100000 | 总模拟帧数 |
| `--render` / `--no-render` | 开 | 控制台可视化总开关 |
| `--clear` / `--no-clear` | 开 | 每次渲染前是否清屏 |
| `--render-interval <int>` | 10 | 渲染间隔帧数，0 关闭 |
| `--sample-interval <int>` | 0 | 全场快照采样间隔，0 关闭 |
| `--trend-csv <file>` | 无 | 核参数时间序列输出路径 |
| `--trend-interval <int>` | 100 | 时间序列采样间隔 |
| `--max-nuclei <int>` | 自动 | 环境承载力；0 = 按面积自动 |
| `--max-fps <float>` | 0 | 帧率上限；0 = 不限速 |
| `--serve` | 无 | 显式进入启动器模式（无参数双击默认即为启动器） |
| `--console` | 无 | 显式进入控制台模式（带任何参数时默认即为控制台） |
| `--port <int>` | 8765 | 启动器模式监听端口（占用时自动 +1 重试） |
| `--no-browser` | 无 | 启动器模式不自动打开浏览器（测试/自动化用） |
| `--schema-check` | 无 | 参数表键覆盖检查（测试用），通过则退出码 0 |
| `--energy-summary` | 无 | 结束后打印按原因汇总的能量收支（调试） |
| `--env <file>` | 自动 | 环境配置文件 |
| `--config <file>` | 无 | 注入自定义核（22 字段/行） |

优先级：**默认值 < env 配置文件 < 显式命令行参数**。

---

## 配置文件 env_config.txt

`key = value` 格式，`#` 注释，范围参数写 `lo, hi`。程序启动时按 `exe 同目录 → 当前目录` 顺序自动查找。全部键见文件内注释，分为五类：

| 分类 | 键（示例） |
|------|-----------|
| 世界与运行 | `width` `height` `seed` `balls` `nuclei` `frames` `visualization` `clear_screen` `grid_cols` `grid_rows` `lock_console` `pause_on_exit` `render_interval` `sample_interval` `trend_interval` `max_fps` |
| 环境与资源 | `max_nuclei` `territory_per_nucleus` `absorb_radius` `ball_spawn_probability` `base_ball_energy` `grid_cell_size` |
| 小球动力学 | `ball_damping` `radial_k` `tangential_k` `near_repel_radius` `ball_ball_repel_radius` `ball_ball_repel_k` `ball_same_owner_attract_radius` `ball_same_owner_attract_k` `ball_max_speed_shield` `ball_max_speed_worker` `ball_max_speed_scout` `ball_vel_range` `bounce_restitution` `follow_boost` `follow_coupling` `detach_radius` `ball_loss_cost` |
| 核动力学 | `nucleus_damping` `attack_k` `avoid_k` `combat_rate` `influence_margin` `forage_k` `forage_radius` `wander_k` `wander_turn_rate` `basal_cost` `speed_cost_k` `speed_cost_exponent` `reproduction_cost_ratio` `child_energy_ratio` `nucleus_init_energy_ratio` `reproduction_offset` `nucleus_min_separation` `nucleus_repel_k` `newborn_grace_frames` |
| 遗传参数范围 | `affinity_init` `orbit_radius_init` `absorb_preference_init` `repel_strength_init` `attack_range_init` `attack_strength_init` `avoid_range_init` `avoid_strength_init` `max_speed_init` `energy_threshold_init` `mutation_rate_init`（初始化范围）+ 对应 `*_clamp`（变异裁剪） |

---

## 数学模型

> 记号约定：每帧为离散时间步 $\Delta t = 1$；$\mathbf{x},\mathbf{v},\mathbf{F}$ 分别为位置、速度、加速度（质量归一化为 1）；下标 $b$ 表小球、$n$ 表核；$t\in\{\mathrm{Shield,Worker,Scout}\}$ 为小球类型。以下均为**每帧、每个实体**的更新规则，顺序即 `World::step()` 的顺序。

### 1. 小球动力学（`updateBallDynamics`）

设小球 $b$ 与其附近核 $n$ 的相对位移 $\mathbf{d} = \mathbf{x}_b - \mathbf{x}_n$，距离 $r = \|\mathbf{d}\|$，径向单位向量 $\hat{\mathbf{e}}_r = \mathbf{d}/r$，切向单位向量 $\hat{\mathbf{e}}_\theta = (-e_{r,y},\, e_{r,x})$（逆时针）。

**核力场**（对影响半径 $r_{\mathrm{inf}} = \max_t R_t + \Delta$ 内的所有存活核求和，$\Delta$ 为 `influence_margin`）：

径向轨道弹簧（轨道修正，把球拉到期望半径）：

$$\mathbf{F}_{\mathrm{rad}} = -a_t\, k_r\, \bigl(r - R_t\bigr)\, \hat{\mathbf{e}}_r$$

切向环绕驱动（形成绕核公转）：

$$\mathbf{F}_{\mathrm{tan}} = a_t\, k_\tau\, \hat{\mathbf{e}}_\theta$$

近距排斥（$r < r_{\mathrm{repel}}$ 时，维持分层防止贴核）：

$$\mathbf{F}_{\mathrm{repel}} = s_t \Bigl(1 - \tfrac{r}{r_{\mathrm{repel}}}\Bigr)\, \hat{\mathbf{e}}_r$$

其中 $a_t, R_t, s_t$ 分别为核对该类型小球的 `affinity / orbitRadius / repelStrength`；$k_r$=`radial_k`、$k_\tau$=`tangential_k`。亲和力满足归一化约束 $\sum_t a_t = 1$。

**归属耦合弹簧**（仅已归属的球；作用距离延至脱附半径 $r_{\mathrm{detach}}$，强度为 $c_f$=`follow_coupling` 倍）：

$$\mathbf{F}_{\mathrm{follow}} = -a_t\, k_r\, c_f\, \bigl(r - R_t\bigr)\, \hat{\mathbf{e}}_r, \qquad r < r_{\mathrm{detach}}$$

这使快核甩不掉自己的球群（普通力场只到 $r_{\mathrm{inf}}$，归属弹簧延伸至 $r_{\mathrm{detach}}$）。

**小球间相互作用**（$d$ 为两球距离）：

$$\mathbf{F}_{bb} = \begin{cases} +k_{\mathrm{rep}}\left(1-\tfrac{d}{d_{\mathrm{rep}}}\right)\hat{\mathbf{e}}, & d < d_{\mathrm{rep}}\\[2pt] -k_{\mathrm{attr}}\left(1-\tfrac{d}{d_{\mathrm{attr}}}\right)\hat{\mathbf{e}}, & d_{\mathrm{rep}} \le d < d_{\mathrm{attr}}\ \text{same owner}\end{cases}$$

即：短程排斥 + 同核小球吸引（聚成球群）。

**积分（半隐式欧拉 + 阻尼 + 限速）**：

$$\mathbf{v}_b \leftarrow \lambda_b\bigl(\mathbf{v}_b + \mathbf{F}_{\mathrm{total}}\bigr), \qquad \lambda_b = 1 - \text{ball damping}$$

$$\mathbf{v}_b \leftarrow \min\Bigl(1, \tfrac{V_{\max}}{\|\mathbf{v}_b\|}\Bigr)\mathbf{v}_b, \qquad V_{\max} = \max\Bigl(v_t^{\max},\ c_{\mathrm{boost}}\cdot\|\mathbf{v}_{\mathrm{owner}}\|\Bigr)$$

$$\mathbf{x}_b \leftarrow \mathbf{x}_b + \mathbf{v}_b$$

其中 $v_t^{\max}$ 为类型限速（护盾/资源/侦察球默认 25/50/80），第二项是**跟随加速**：归属球的限速提升到其核当前速率的 $c_{\mathrm{boost}}$（=`follow_boost`，默认 1.5）倍。

**边界反弹**：越界分量按 $\mathbf{v}_\perp \leftarrow -e_r\, \mathbf{v}_\perp$ 反转并衰减（$e_r$=`bounce_restitution`），位置反射后截断到 $[0,W]\times[0,H]$。

### 2. 归属与失球惩罚（`updateOwnership`）

所有权 = 脱附半径内最近的存活核：

$$\mathrm{owner}(b) = \begin{cases}\arg\min_n \|\mathbf{x}_b - \mathbf{x}_n\|, & \min_n \|\mathbf{x}_b - \mathbf{x}_n\| < r_{\mathrm{detach}}\\[2pt] -1\ (\text{free}), & \text{otherwise}\end{cases}$$

维护上一帧归属 $o^-$：若 $o^- \ge 0$ 且 $o^- \neq o$（脱附或被抢），原归属核扣除 $c_{\mathrm{loss}}$（=`ball_loss_cost`）能量；被吸收移除的球不触发惩罚。

### 3. 能量吸收（`absorbBalls`）

核吸收属于自己的、距离 $r < r_{\mathrm{abs}}$（=`absorb_radius`）的小球：

$$\Delta E_n \;+=\; E_{\mathrm{base}}\cdot p_t$$

其中 $E_{\mathrm{base}}$=`base_ball_energy`、$p_t$=`absorbPreference[t]`，小球被移除并由补充机制再生。

### 4. 核间争斗（`nucleusCombat`）

对每对存活核 $(A,B)$，若 $d < \min(D_{\mathrm{attack}}^A, D_{\mathrm{attack}}^B)$、$d \ge D_{\mathrm{sep}}$、双方年龄均 $\ge g$（新生保护期），则攻击强度高者吸取低者：

$$\Delta = \bigl(S_A - S_B\bigr)\, k_c, \qquad E_{\text{strong}} \;+=\; \Delta, \quad E_{\text{weak}} \;-=\; \Delta$$

其中 $S$=`attackStrength`、$k_c$=`combat_rate`。

### 5. 核运动（`nucleusMovement`）——趋利避害决策

核的加速度由四部分构成，**两阶段更新**（先用帧首位置统一求力，再统一积分，保证对称性与确定性）：

**贴脸斥力**（$d < D_{\mathrm{sep}}$，跳过攻击与避让）：

$$\mathbf{F}_{\mathrm{sep}} = -k_{\mathrm{sep}}\bigl(D_{\mathrm{sep}} - d\bigr)\,\hat{\mathbf{e}}_d$$

**攻击（趋利）**——只追击比自己弱的对手，且双方过了新生保护期：

$$\mathbf{F}_{\mathrm{attack}} = S_n\, k_a \Bigl(1 - \tfrac{d}{D_{\mathrm{attack}}}\Bigr)\, \hat{\mathbf{e}}_d, \quad d < D_{\mathrm{attack}},\ S_n > S_o$$

**避让（避害）**——只躲避比自己强的对手：

$$\mathbf{F}_{\mathrm{avoid}} = -S_n^{\mathrm{av}}\, k_v \Bigl(1 - \tfrac{d}{D_{\mathrm{avoid}}}\Bigr)\, \hat{\mathbf{e}}_d, \quad d < D_{\mathrm{avoid}},\ S_n < S_o$$

**觅食（趋利）**——按吸收偏好加权朝附近小球移动：

$$\mathbf{F}_{\mathrm{forage}} = k_f \sum_{b\in B(r<R_{\mathrm{forage}})} p_{t(b)} \Bigl(1 - \tfrac{r_b}{R_{\mathrm{forage}}}\Bigr)\,\hat{\mathbf{e}}_{r_b}$$

**探索游走**——持久方向 + 缓慢转向（避免原地震颤）：

$$\theta_w \leftarrow \theta_w + U(-\omega, \omega), \qquad \mathbf{F}_{\mathrm{wander}} = k_w\bigl(\cos\theta_w,\, \sin\theta_w\bigr)$$

积分与限速：

$$\mathbf{v}_n \leftarrow \lambda_n\bigl(\mathbf{v}_n + \mathbf{F}_{\mathrm{total}}\bigr), \qquad \mathbf{v}_n \leftarrow \min\Bigl(1,\tfrac{v_{\max}}{\|\mathbf{v}_n\|}\Bigr)\mathbf{v}_n, \qquad \mathbf{x}_n \leftarrow \mathbf{x}_n + \mathbf{v}_n$$

**硬分离约束**（防重叠）：若任意两核 $d < D_{\mathrm{sep}}$，各沿连线外推 $(D_{\mathrm{sep}}-d)/2$，并将法向接近速度归零。

### 6. 能量系统（代谢、繁殖、死亡）

每帧代谢消耗（基础项 + 速度的**超线性**项，$v=0$ 时消耗不为零）：

$$L(v) = c_b + c_s\, v^{\beta}, \qquad \beta > 1\ (\text{default } 2)$$

繁殖：能量满足 $E_n \ge E_{\mathrm{th}}$ 时，

$$E_n \leftarrow E_n - \theta\, E_{\mathrm{th}}, \qquad E_{\mathrm{child}} = \lambda\, E_{\mathrm{th}}$$

其中 $\theta$=`reproduction_cost_ratio`（默认 0.8）、$\lambda$=`child_energy_ratio`（默认 0.5）。

死亡判定（任意能量入出口统一执行）：$E_n \le 0 \Rightarrow \mathrm{alive} = \mathrm{false}$。

### 7. 变异与遗传（`reproduce`）

子代复制父代 22 个参数后逐项做**乘法高斯扰动**：

$$p_i' = \mathrm{clamp}\Bigl(p_i\bigl(1 + \mu\, z\bigr),\ p_i^{\min},\ p_i^{\max}\Bigr), \qquad z \sim \mathcal{N}(0,1)$$

其中 $\mu$=`mutationRate`（本身也可遗传、可变异）；亲和力扰动后重新归一化 $\hat{a}_t = a_t / \sum_j a_j$；子核出生位置为父核 $\pm$`reproduction_offset`，`age` 从 0 开始（进入 `newborn_grace_frames` 帧的保护期）。

### 8. 小球补充与承载力

每帧以概率 $p_{\mathrm{spawn}}$（=`ball_spawn_probability`）随机补充一个自由球，且小球总数维持 $\ge N_{\mathrm{ball}}^0$；环境承载力：

$$N_{\max} = \max\Bigl(N_{\mathrm{nuc}}^0,\ \tfrac{W \cdot H}{T}\Bigr)$$

其中 $T$=`territory_per_nucleus`。达到承载力后停止繁殖。

### 9. 空间哈希（`SpatialGrid`）

网格单元 $c$（=`grid_cell_size`，默认 60）。实体按 $\mathrm{cell}(x) = \lfloor x/c \rfloor$ 分桶（只存索引不复制对象），查询半径 $r$ 时遍历单元区间 $\bigl[\lfloor (x-r)/c \rfloor,\ \lfloor (x+r)/c \rfloor\bigr]^2$，配合**平方距离早退** $\|\mathbf{d}\|^2 > r^2$ 剪枝。复杂度由 $O(N^2)$ 降为 $O(N \cdot n_{\mathrm{cell}})$。

---

## 遗传参数与进化

22 个可遗传参数（全部浮点，见 `src/NucleusParams.h`）：

| 参数 | 符号 | 含义 | 默认初始化范围 | 变异裁剪 |
|------|------|------|--------------|---------|
| `affinity[t]` ×3 | $a_t$ | 对类型 t 小球的吸引力权重（归一化） | $[0.1, 1.0]$ 后归一化 | $\ge 10^{-4}$ 再归一化 |
| `orbitRadius[t]` ×3 | $R_t$ | 类型 t 期望环绕半径 | $[30, 200]$ | $[10, 300]$ |
| `absorbPreference[t]` ×3 | $p_t$ | 吸收类型 t 的能量倍率 | $[0.5, 1.5]$ | $[0.1, 3.0]$ |
| `repelStrength[t]` ×3 | $s_t$ | 近距排斥强度 | $[0.5, 3.0]$ | $[0.1, 6.0]$ |
| `attackRange` | $D_{\mathrm{attack}}$ | 攻击触发距离 | $[20, 200]$ | $[5, 400]$ |
| `attackStrength` | $S$ | 攻击强度（吸能与追击加速度） | $[10, 100]$ | $[1, 300]$ |
| `avoidRange` | $D_{\mathrm{avoid}}$ | 避让触发距离 | $[30, 300]$ | $[10, 600]$ |
| `avoidStrength` | $S^{\mathrm{av}}$ | 避让加速度 | $[10, 80]$ | $[1, 200]$ |
| `maxSpeed` | $v_{\max}$ | 核最大速度 | $[30, 100]$ | $[5, 300]$ |
| `energyThreshold` | $E_{\mathrm{th}}$ | 繁殖阈值 | $[80, 200]$ | $[20, 1000]$ |
| `mutationRate` | $\mu$ | 变异幅度 | $[0.01, 0.2]$ | $[0.001, 0.5]$ |

进化由三个能量代价旋钮塑造：`basal_cost`/`speed_cost_k`（代谢）、`reproduction_cost_ratio`（繁殖代价）、`ball_loss_cost`（失球惩罚）。配合趋利避害决策（只打弱者、躲避强者），不同策略（掠夺者/逃跑专家/农场主）得以共存。

---

## 采样与数据分析

| 输出 | 触发 | 格式 |
|------|------|------|
| `balls_XXXXXX.csv` | `--sample-interval` | `x,y,type,ownerId` |
| `nuclei_XXXXXX.csv` | `--sample-interval` | `x,y,energy` + 22 参数（6 位小数） |
| `trend.csv` | `--trend-csv` | `frame,nucleus_id,energy` + 22 参数（每采样帧每核一行） |
| `survivors.csv` | 每次结束 | 存活核完整参数 |

脚本（`scripts/`，仅依赖 numpy）：

```bash
# 逐帧统计摘要（均值/标准差），观察参数演化趋势
python scripts/trend_summary.py --input trend.csv --out trend_summary.csv

# 批量运行 + k-means 聚类角色原型（手写 k-means++，无 sklearn）
python scripts/batch_cluster.py --exe simulator.exe --runs 6 --k 4 --out output/batch

# 回归测试
python scripts/phase05_test.py
python scripts/chase_test.py   # 失球/跟随耦合验证
python scripts/follow_test.py  # 跟随耦合小世界验证
```

---

## 性能与确定性

- **确定性**：所有随机走 `std::mt19937(seed)`；帧内迭代顺序固定；同种子逐字节一致。
- **空间哈希**：5000 球 / 500 核、无渲染 ≥ 60 FPS（实测 ~82 FPS，10000 帧约 122 s）。
- 关闭渲染（`--no-render`）性能显著提升。

---

## 文件结构

```
src/
  Vec2.h                二维向量
  BallType.h            小球类型枚举
  Ball.h/.cpp           小球实体
  NucleusParams.h/.cpp  22 遗传参数 + 随机/变异 + ParamRanges
  Nucleus.h             核实体（含 age/wanderAngle 等状态）
  World.h/.cpp          核心模拟（动力学/能量/繁殖/进化）
  SpatialGrid.h/.cpp    空间哈希网格
  EventBus.h            类型化事件总线（mod 订阅入口）
  EnergySystem.h/.cpp   能量唯一通道（按原因统计 + 统一死亡判定）
  mod_api.h             ModAPI 接口定义
  mods_registry.h/.cpp  mod 注册表（启用/添加 mod 的唯一地方）
  IRenderer.h           渲染器抽象接口
  ConsoleRenderer.h/.cpp 控制台字符渲染（IRenderer 默认实现）
  Sampler.h/.cpp        CSV 采样与趋势输出
  ConfigLoader.h/.cpp   自定义核配置文件解析
  EnvConfig.h/.cpp      环境配置文件解析
  main.cpp              参数解析与主循环
mods/                   mod 实现（example_periodic_drain.cpp 示例）
docs/PLAN.md            分阶段实施计划（Phase 0 / 0.5）
docs/PLAN_LAUNCHER.md   启动器 + 可视化 Mod 方案（v2）
scripts/                批量聚类与回归测试脚本
env_config.txt          环境配置示例（全部参数 + 注释）
custom_nuclei.txt       自定义核注入示例
CMakeLists.txt
```

---

## 许可

本项目为课程/研究用途的演化模拟实验，代码无第三方依赖，可自由修改使用。

---

## Mod 开发者指南

模拟核心通过 **ModAPI** 暴露四个接口，写一个 mod = 一个 `registerXxx(ModAPI&)` 函数 + 注册表一行。

### ModAPI 四个字段

| 字段 | 类型 | 约定 |
|------|------|------|
| `api.world` | `World&` | **只读状态**（balls/nuclei/frame/config 等）。要改能量必须走 `energy.apply`；不得直接修改位置/速度/遗传参数 |
| `api.energy` | `EnergySystem&` | **写能量的唯一通道**：`apply(nucleus, delta, EnergyReason)`；死亡判定与收支统计在这里统一处理 |
| `api.rng` | `std::mt19937&` | 种子驱动的确定性随机。**mod 内禁止使用未种子随机**（rand/random_device 等），否则破坏可复现性 |
| `api.events` | `EventBus&` | 事件订阅：`subscribe(EventType, callback)`；回调按订阅顺序触发，回调内实体指针只在 emit 期间有效 |

### 可用事件

| 事件 | 时机 | 载荷 |
|------|------|------|
| `FRAME_START` | 每帧 `++frame` 之后 | `frame` |
| `FRAME_END` | 每帧 step 末尾 | `frame` |
| `BALL_ABSORBED` | 每帧吸收结束后聚合发射一次 | `value`=本帧吸收总能量，`extra`=吸收球数指针 |
| `NUCLEUS_BORN` | 子核 push_back 之后 | `nucleus`=子核 |
| `NUCLEUS_DIED` | `EnergySystem::apply` 内部 | `nucleus`=死亡核 |

### 能量原因（EnergyReason）

`ABSORB / COMBAT / METABOLISM / REPRODUCTION / AGING / CROWDING / PLAGUE / BALL_LOSS`（后两项供 mod 使用；结束后 `--energy-summary` 可打印按原因汇总的收支）。

### 写一个 mod（完整示例）

新建 `mods/example_periodic_drain.cpp`（见仓库同路径示例）：

```cpp
#include "mod_api.h"
#include "World.h"

// 示例 mod：每 100 帧对所有存活核扣 10 能量。
void registerPeriodicDrain(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (e.frame > 0 && e.frame % 100 == 0) {
            for (auto& n : api.world.nuclei()) {
                if (n.alive) api.energy.apply(n, -10.0, EnergyReason::PLAGUE);
            }
        }
    });
}
```

### 如何启用 / 新建 mod

1. 新建 `mods/xxx.cpp`，写 `void registerXxx(ModAPI&)`。
2. 在 `src/mods_registry.h` 加声明 `void registerXxx(ModAPI&);`。
3. 在 `src/mods_registry.cpp` 的 `registerAllMods` 里加一行 `registerXxx(api);`。
4. 构建：CMakeLists.txt 的 `mods/` 显式文件列表加一行；或 g++ 直接编译 `g++ -std=c++17 -O2 -Isrc -o simulator src/*.cpp mods/*.cpp`。

### 确定性注意事项

- 所有随机一律走 `api.rng`（同种子逐字节可复现）；
- 事件派发按注册顺序（注册顺序 = `registerAllMods` 中的行顺序），mod 的执行次序是确定的；
- **不注册任何 mod 时，行为与旧版完全一致**（回归保障）。

