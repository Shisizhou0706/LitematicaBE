# LitematicaBE 三阶段路线图（假方块 → C++ 内核 → Endstone）

> 本文是执行计划，不是架构讨论。每个阶段都有：任务、产出文件、验收（DoD）、门禁（Gate）、回退方式。
> 阶段之间是**顺序依赖**而不是并行：Gate 不通过就停在当前阶段并重新评估下一阶段的收益。

- 阶段一：LL 平台接入 HologramLib，用发包假方块替换「写世界 ghost」——目的是**测库可用度**
- 阶段二：内核换语言（C++ / C ABI）——目的是**性能 + 为双平台共享算法**
- 阶段三：兼容 Endstone——目的是**一份内核两个宿主**

---

## 0. 全局约束（三阶段都适用）

| 编号 | 约束 | 落地方式 |
|---|---|---|
| G1 | 每阶段必须可回退、可单独发版 | feature flag + 保留旧路径至少一个大版本 |
| G2 | `npm test`（39 脚本 / 547 断言）在任何 PR 上必须全绿 | 新逻辑配新测试，旧断言不删只改语义 |
| G3 | 世界零污染的验证手段要一直在 | `/lit ghost sweep dry` 计数 + `ghost_expect.json` 体积 |
| G4 | 不引入运行时新依赖 | 阶段一零依赖（前置库是外部插件）；阶段二只加一个 DLL |
| G5 | 版本真源只有 `index.js` 的 `PLUGIN_VERSION` | `npm run version:sync` + `check:version` |
| G6 | 每阶段先打点再优化 | `[perf]` 一行日志，禁止凭感觉调参 |

版本与分支：

| 阶段 | 分支 | 目标版本 | 说明 |
|---|---|---|---|
| 一 | `feat/fakeblock-sink` | `3.2.0`（可先 `3.2.0-beta.1`） | 默认 `auto`：检测到库即走 packet，可一键切回 world |
| 二 | `feat/core-cpp` | `3.3.0` | core 可选（`core.enabled=false` 走纯 JS），默认关 |
| 二收尾 | 同上 | `4.0.0` | core 默认开，JS 实现标记 legacy（保留一个大版本） |
| 三 | 独立仓库或 `platform/endstone` | `1.0.0`（独立版本线） | 与 LL 版本号解耦 |

---

## 阶段一：HologramLib 假方块通道（LL）

**要回答的问题**：这个库在我们真实场景（自定义 `lit:g_*` 方块 + 完整状态 + 6 万~10 万格 + 多玩家 + 重登/跨维度/切层）到底可不可用？

### 任务

| ID | 任务 | 产出 |
|---|---|---|
| 1.1 | **能力探针**：`/lit ghost probe` 输出能力矩阵（导出是否存在、`blockFakeResolve` 对 `lit:g_oak_stairs[lit:dir=2]` 的结果、`blockBatchSelfTest` 耗时、`viewDescribe` 钩子计数） | `src/ghost/HoloProbe.js`、命令分支 |
| 1.2 | **未验证假设清单化**：① 自定义命名空间 + 带冒号状态键能否 resolve；② 26.60 哈希 nid 模式下 `lit:g_*` 的解析；③ `blockBatchSet` 的维度语义；④ 客户端区块未达时的丢包行为；⑤ 假方块的客户端碰撞/预测抖动 | 探针结果写进 `work/probe-report.md` |
| 1.3 | **Sink 抽象**：`WorldGhostSink`（现行为，零改动搬迁）+ `PacketGhostSink`（batch / viewBlockById / unpaint / stats） | `src/ghost/GhostSink.js`、`src/render/PacketGhostSink.js` |
| 1.4 | **显示层镜像**：`dimid → index → { gid, nid, st, owners }`，作为判定层唯一真源 | `src/ghost/DisplayVoxelStore.js` |
| 1.5 | **重发调度**：`onJoin`(延迟 20~40 tick) / `onRespawn` / 维度切换 / 区块差分(200ms) / 心跳(20s)，统一收口 `repaint(player,{layer})` | `src/render/GhostRepaintScheduler.js` |
| 1.6 | **多会话仲裁**：同一坐标多投影 → 主控优先，其余沉底（避免互相覆盖闪烁） | `DisplayVoxelStore` 的 owners 优先级 |
| 1.7 | **GhostRenderer 接 sink**：`_writeOne` / `_clearOne` / `_finishSession` 三处分派；`world` 路径行为与 3.1.0 一致 | 改 `src/ghost/GhostRenderer.js` |
| 1.8 | **Mega 接入**：`injectChunk` 输出 packed；切层从「整会话还原」改为「撤旧层 + 铺新层」 | 改 `src/render/MegaProjectionRenderer.js`、`ProjectionRenderer.applyLayer` |
| 1.9 | **判定层切换**：`GhostGuard.isAirLike` / `BlockVerifier` / `AttachedBlockSupport` / 轻松放置支撑判定改走 `DisplayVoxelStore` | 改 4 处读取点 |
| 1.10 | **事件语义**：玩家挖/放命中显示层 → 摘格 + repaint（复用 `markSatisfied`） | 改 `GhostGuard` 事件路径 |
| 1.11 | **配置与命令**：`render.ghostBackend`、`render.fakeBlock.{viewDistance,resendIntervalTicks,batchPerTick,pinLimit}`；`/lit ghost backend <world\|packet\|auto>`、`/lit ghost stats` | `config.json`、`index.js` 命令 |
| 1.12 | **测试**：packed 编解码、状态串构造（含 `lit:dir`）、维度不过界、优先级仲裁、重发去重与预算、静态断言「packet 后端不得调用 `mc.setBlock`」 | `tests/test_ghost_sink.js`、`tests/test_repaint_scheduler.js`、`tests/test_ghost_backend_static.js` |
| 1.13 | **灰度发布**：`3.2.0-beta.1` → 收 5~10 名玩家反馈 → `3.2.0` | CHANGELOG |

### 依赖的现有资产（不重写）

- `GhostBlockRegistry`（`javaMapByState` → `lit:g_*` + `lit:dir`）：**原样复用**，它就是"状态来源"
- `SurfaceExtractor` / `LODRenderer`：原样，收益更大（决定包数）
- `ProjectionRenderer` 的层/旋转/镜像：原样
- BP/RP 与 addon 版本校验：原样（但校验失败不再阻断投影）

### 验收（DoD）

| 指标 | 目标 | 测法 |
|---|---|---|
| 10 万格 Mega 首次铺设 | < 1 s | `ghost.done` 时间戳 |
| 单 tick 最长阻塞 | < 5 ms | `RenderScheduler` 打点 |
| 切层（6 万格） | < 0.3 s | 同上 |
| 重登/传送/跨区块恢复 | < 1 s | 人工 + 日志 |
| 世界污染 | `sweep dry` = 0，重启后 `ghost_expect.json` 不增长 | 命令统计 |
| 多玩家（5 人各自投影） | 无串味、无错位 | 人工 |
| `npm test` | 全绿（含 3 个新脚本） | CI |
| 回退 | `ghostBackend=world` 与 3.1.0 行为一致 | A/B |

### Gate 1（决定是否继续）

- ✅ 通过：状态正确率 ≥ 99%（抽查楼梯/朝向/活塞/半砖），且性能达标 → 进阶段二
- ⚠️ 有条件通过：状态串解析失败但 `viewBlockById` 可用 → 改用「identifier 展开（每状态一个方块）+ 覆盖表」，BP 方块数会增加，评估体积后再决定
- ❌ 不通过（显示不可用/抖动不可接受/库不稳定）：**停在 `world` 后端**，阶段二的收益重新评估（此时 C++ 只值"解析速度"这一项）

---

## 阶段二：C++ 内核（C ABI）

**要回答的问题**：能不能让"解析 → 映射 → 外壳 → 打包"只有一份实现，且性能足够养活 Endstone 侧？

**边界纪律**：内核**不 include、不链接**任何 LeviLamina / Endstone 头文件；只依赖 STL（zlib 用 miniz 单文件内联）。协议编码/发包一律交给 HologramLib。

### 任务

| ID | 任务 | 产出 |
|---|---|---|
| 2.0 | **基线打点**：解析 / 映射 / 外壳 / pack / tick 阻塞，输出 `[perf]` | `src/utils/PerfTrace.js` |
| 2.1 | **工程骨架**：`core/CMakeLists.txt`、`core/include/litcore.h`（C ABI v1：版本握手、POD、arena、错误码、`free_*` 成对） | `core/` |
| 2.2 | **移植 NBT + litematic**：小端/大端读、GZIP（miniz）、调色板位解包 | `core/src/nbt_reader.cpp`、`litematic.cpp` |
| 2.3 | **移植 mcstructure** | `core/src/mcstructure.cpp` |
| 2.4 | **移植映射**：`ghost_index.json` → 内存表；`javaMapByState` 多级回退与 `statesEqual` 口径必须一致 | `core/src/mapping.cpp` |
| 2.5 | **移植外壳提取**：`SurfaceExtractor` + `PistonStateInfer` | `core/src/surface.cpp` |
| 2.6 | **打包**：子区块分组（16³，≤1024/包）+ packed 文本 | `core/src/pack.cpp` |
| 2.7 | **Mega 分块 IO**：`BinaryChunkStorage` 的读（gunzip + palette 解码）进 core | `core/src/chunkstore.cpp` |
| 2.8 | **差分测试（核心）**：同一输入，JS 与 core 的 packed 逐字节一致；≥ 20 个边界样例（空、单格、全空气、超大 palette、负坐标、旋转/镜像、截断文件） | `core/tests/`、`tests/fixtures/`（脚本合成，不入库大文件） |
| 2.9 | **宿主桥**：`platform/levilamina` C++ 插件 + lrca 导出；JS 侧 `CoreBridge` 双实现 + `core.enabled` | `platform/levilamina/`、`src/core/CoreBridge.js` |
| 2.10 | **发布接线**：DLL 进 zip；`tooth.json` placements、`build-release.mjs`、`.gitignore` 白名单、`check-tracked` 四处同步 | tools/ |
| 2.11 | **CI**：Windows job 编 core + 跑单测 + 差分（不依赖 BDS） | `.github/workflows/ci.yml` |
| 2.12 | **灰度**：`3.3.0` 默认关 → 真机 A/B → `4.0.0` 默认开 | CHANGELOG |

### 验收（DoD）

| 指标 | 目标 |
|---|---|
| 10 万方块解析 | P90 < 150 ms（JS 基线 1~3 s） |
| 内存 / 10 万格 | < 8 MB（16 B/格 + arena） |
| 差分一致率 | 100%（20 个样例 × 3 种旋转） |
| 崩溃 | 0（100 次加载卸载 + 异常文件 fuzz 1 万次） |
| 回退 | `core.enabled=false` 与 `3.3.0` 纯 JS 行为一致 |
| 发布 | `npm run build` 产物含 DLL，`check:tracked` / `check:version` 通过 |

### Gate 2

- ✅ 通过：差分 100% + 性能达标 → 进阶段三（Endstone 可以只写薄壳）
- ⚠️ 有条件通过：差分有差异但都是"已知口径差" → 先在 JS 侧修正口径，再重跑；不达标不进入阶段三
- ❌ 不通过（C ABI 稳定性不足/崩溃）：放弃共享内核，阶段三改为 Python 独立实现（成本翻倍，但阶段一的假方块结论仍然有效）

---

## 阶段三：Endstone 兼容

**要回答的问题**：一份 core + 两个薄壳是否成立，代价是哪些功能降级？

### 任务

| ID | 任务 | 产出 |
|---|---|---|
| 3.0 | **最小闭环（只读投影）**：Python 插件 + ctypes 调 core + `fakeblock` 发包 + 1 条命令（`/lit load <file>`） | `platform/endstone/litematica_endstone/` |
| 3.1 | **补批量缺口**：`holo_core_encode_update_subchunk_blocks` + `send_packet`（否则 6 万格不可用）；失败则限 `maxBlocks` 并在日志明示 | `platform/endstone/batch.py` |
| 3.2 | **交互降级**：命令 + 坐标（无表单）；切层 / 旋转 / 锚点 / 清理 | 同上 |
| 3.3 | **可选预编译产物**：`.litproj`（core 直接产出 packed 段），Endstone 端可完全不解析 litematic | `core/src/litproj.cpp` |
| 3.4 | **共享资产核对**：同一份 BP/RP + `ghost_index.json`；nid 模式（`block-network-ids-are-hashes`）两端必须一致 | 文档 + 启动自检 |
| 3.5 | **测试**：core 的 ctypes 单测（无 BDS）、Python 侧 smoke（起服、加载、切层、退出清理） | `platform/endstone/tests/` |
| 3.6 | **分发**：独立版本线 `1.0.0`，独立 README（明确"功能子集"） | 仓库/子目录 |

### 验收（DoD）

- 能加载一个 10 万方块投影并在客户端正确显示
- 切层 < 0.5 s
- 与 LL 版同一份 BP/RP、同一份 core DLL（哈希一致）
- 明确的功能对照表（LL 有 / Endstone 无）写进 README
- 不崩、不吞日志、退出时清干净

### Gate 3

- ✅ 通过：作为"功能子集"长期维护
- ❌ 不通过（批量缺口补不上，性能不可接受）：Endstone 版只保留"小投影/预览"定位，不做主力

---

## 横向：风险登记表

| ID | 风险 | 阶段 | 影响 | 缓解 | 触发信号 |
|---|---|---|---|---|---|
| R1 | 自定义方块 + 带冒号状态键无法解析 | 1 | 阻塞整个方案 | 早验（1.1）；退 `viewBlockById`；再退 identifier 展开 | probe 返回 -1 |
| R2 | 假方块带来客户端碰撞/预测抖动 | 1 | 体验 | 不可修，产品决策（必要时只在"可穿"区域用 `blockBatchSetNear`） | 玩家反馈"走不动" |
| R3 | 全局 8 包/tick 预算，多玩家排队 | 1 | 铺设变慢 | 视距裁剪 + 自己的分帧配额 + `blockBatchProgress` 上报 | 多人同时加载 |
| R4 | 覆盖表与 batch 互相覆盖 | 1 | 显示错乱 | 单坐标单通道（`pinLimit` 分区） | 覆盖 + batch 混用 |
| R5 | C ABI 崩溃带走 BDS | 2 | 严重 | 每个导出入口 `try/catch(...)` 转错误码；fuzz | 任何一次崩溃 |
| R6 | DLL/宿主 CRT 堆不共享 | 2 | 崩溃 | arena + `free_*` 成对；禁止跨边界 `new/delete` | 首次联调 |
| R7 | LeviLamina 升级导致 lrca 导出失效 | 2/3 | 单平台失效 | `ll.hasExported` 探测 + JS 回退 | 版本升级后 |
| R8 | Endstone 无批量导出发包 | 3 | 不可用 | ctypes 调 `holo_core_encode_*`；否则限规模 | 3.1 任务 |
| R9 | Endstone 无表单 | 3 | 功能降级 | 命令参数化，接受子集 | 3.2 |
| R10 | 发布包/白名单漂移 | 1/2 | CI 干净检出失败 | `check:tracked` + 四处同步清单 | PR CI 红 |

---

## 横向：工程量（人日，含自测，不含缓冲）

| 阶段 | 明细 | 小计 |
|---|---|---|
| 一 | 探针 0.5 + 抽象 2 + 镜像 1 + 重发 1.5 + GhostRenderer 1.5 + Mega 1 + 判定层 1 + 测试 1.5 + 发布 1 | **11** |
| 二 | 打点 0.5 + 骨架 1 + NBT/litematic 4 + mcstructure 1 + mapping 2 + surface 2 + pack 1 + chunkstore 1 + 差分 2 + 桥 2 + 发布/CI 1.5 | **18** |
| 三 | 最小闭环 3 + 批量 2 + 交互 3 + litproj 1 + 测试 2 + 分发 1 | **12** |
| 合计 | +30% 缓冲 | **≈ 53** |

---

## 横向：每阶段结束时必须回答的三个问题

| 阶段 | 问题 1 | 问题 2 | 问题 3 |
|---|---|---|---|
| 一 | 库在我们场景可用吗？ | 世界零污染后哪些子系统可以删？ | 性能提升是否已经吃掉大部分痛点？ |
| 二 | 内核能不能成为唯一算法源？ | 差分为 0 的边界在哪？ | 崩溃与 ABI 风险可控吗？ |
| 三 | 一份 core 两个宿主成立吗？ | 功能子集的边界清楚吗？ | 维护成本（两套宿主）是否可接受？ |

---

## 附：第一周就能做的 5 件事（不必等决定）

1. 写 `work/probe.md`：列出阶段一全部未验证假设，逐条去真机验（1.2）
2. 加 `[perf]` 打点（2.0 提前做），拿到 3.1.0 的真实基线数字
3. 用现有 `work/` 脚本合成一个 10 万方块的 `.litematic` 与 `.mcstructure` 测试样例（差分测试的前置）
4. 把 `GhostRenderer._writeOne` 的三个动作（读判 / 记账 / 写世界）在注释里标清边界，为 1.3 抽象做准备
5. 在 `config.json` 预留 `render.ghostBackend`（默认 `"world"`），先让配置链路跑通，再填实现
