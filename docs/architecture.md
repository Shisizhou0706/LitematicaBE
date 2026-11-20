# LitematicaBE 架构说明（4.x，原生模组）

> 与 `docs/plan-3stage-roadmap.md`、`docs/plan-stage2-core.md` 的关系：这两份是 **3.x 时代（LSE JS 插件 + 26.x 线）的历史规划**，其中的 JS 宿主、lrca 跨语言边界、`.litproj` 等结论在新基线下不再成立。本文取代它们作为实施依据，历史文档只保留决策脉络。

## 1. 定位

把 Litematica 投影以 **客户端假方块**渲染：服务端世界不改、存档不脏、退出即消失；玩家看到完整的方块形状与朝向，且可以按投影逐格放置。

非目标：不做客户端 Mod、不做世界写回、不做多人共享编辑。

## 2. 三条硬约束（先钉死，避免返工）

### C1 基线锁死，HologramLib 不能跨线

HologramLib 的发布线按 BDS 版本切分，且 C++ ABI 与头文件虚表布局绑定：

| 线 | HologramLib | API | ABI stamp | 对应底座 |
|---|---|---|---|---|
| 1.21.132 | `21.132.1` | `1.37.0`（`0x012B00`） | `2` | BDS 1.21.132.3 / LeviLamina 1.9.9 |
| 26.60 | `26.60.x` | 1.37.0 起 | 2 | BDS 1.26.60 / LL 26.60 |

结论：**本项目只对 1.21.132 线负责**。库换 ABI（虚表布局变化）必须重编本插件；`HOLOGLIB_ABI_STAMP` 与 `HOLOGLIB_API_VERSION` 只作运行期握手告警，不能当兼容承诺。

### C2 C++ 侧只有「逐格覆盖」，批量必须自己发

`IHologramLib::viewOverrides()`（1.21.132 线）提供：

```cpp
bool overrideBlock(std::string const& playerName, int x, int y, int z, BlockView const& view);
bool clearBlock(std::string const& playerName, int x, int y, int z);
void clearAll(std::string const& playerName);
std::string describeFor(std::string const& playerName) const;
// 1.36.0 追加：
EntityInfo queryEntity(std::int64_t entityUniqueId) const;
bool alignEntity(std::string const& playerName, std::int64_t entityUniqueId);
```

`BlockView` 只有 `std::string type;`（方块标识/状态串），本层**没有批量入口**。6 万～10 万格的投影逐格调用不可接受。

批量能力分散在另外三层，必须分清「在哪一层、能不能给第三方插件用」：

| 层 | 位置 | 内容 | 第三方 C++ 可用性 |
|---|---|---|---|
| 公开头 | `include/hologramlib/HologramLib.h`、`AbiCompat.h` | `IViewOverride` 逐格；9 个尾部域访问器（无批量域） | 只有逐格 |
| 库内实现 | `platform/levilamina/src/block/BlockBatchManager.h` | `setMany` / `setManyNear` / `setManyAll` / `clearMany` / `progress` / `stats` / `selfTest` / `tick` 预算 | **私有头，未导出** |
| LSE 导出 | `platform/levilamina/src/block/BlockBatchExporter.cpp` | `blockBatchSet` / `SetAll` / `SetNear` / `Clear` / `Stats` / `Progress` / `SelfTest`，经 `hologramlib::lse::exportAs` 注册进 `RemoteCall` 表 | 只有脚本 `ll.import("HologramLib", …)` 能拿到（需 LegacyRemoteCall 运行时） |
| C ABI | `core/c_api.h`；符号在 `holo_core.dll` 的导出表里 | `holo_core_encode_update_subchunk_blocks` / `holo_core_packet_id_update_subchunk_blocks` / `holo_free` 等 68 个符号 | **可以，但要挑对模块**：`HologramLib.lib` 只导入 `holo_core_packet_id_update_subchunk_blocks`（外加 actor/block 几个单格编码），**`encode_update_subchunk_blocks` 与 `holo_free` 只在 `holo_core.dll`**（214 KB，只依赖 VC 运行时，不依赖 HologramLib.dll） |

编码器签名：

```c
uint32_t holo_core_packet_id_update_subchunk_blocks(void);
int32_t  holo_core_encode_update_subchunk_blocks(
    int32_t sub_x, int32_t sub_y, int32_t sub_z,
    const holo_block_change* blocks, size_t count,
    uint8_t** out_body, size_t* out_len);

typedef struct holo_block_change {
    int32_t  x, y, z;
    uint32_t runtime_id;      // 网络 id，调用方负责解析
    uint32_t flags;
    uint64_t sync_entity;
    uint32_t sync_message;
} holo_block_change;
```

已落地（阶段 A）：**运行时多路径解析**。`HologramFacade::resolveBatchApi()` 依次尝试
`HologramLib.dll` 导出表 → 已加载的 `holo_core.dll` → 本插件目录 → `plugins/HologramLib/` → 系统搜索路径，
拿到 `{packetId, encode, free}` 后由 `ghost::BatchEncoder` 按子区块分组编码、`ghost::PacketSender` 拼 varint 包头
交给 `NetworkPeer::sendPacket(..., Reliable, Compressible)`（与库内部 `wire::sendRaw` 同一条路）。
全都拿不到时 `DisplayPath` 落到 `PerCellOverride`，批量自然降级。

发布形态仍待定（见 Q6）：`holo_core.dll` 随插件分发，还是请 `HologramLib` 把这两个符号一并 export。

路线 3（逐格 `overrideBlock`）只用于小投影、单格修补与降级路径，不作为主通道。


### C3 世界零污染是验收线，不是优化项

任何显示路径都不得调用世界写接口（`Level::setBlock` 之类）。显示状态只存在于：

1. 客户端（假方块包）；
2. 服务端内存里的「显示层镜像」；
3. `data/` 下的会话快照（用于重登/重启恢复）。

## 3. 模块划分（`src/LitematicaBE/`）

| 目录 | 职责 | 关键点 |
|---|---|---|
| `core/schematic` | `.litematic` / `.mcstructure` 解析：NBT（小端/大端）、GZIP、调色板位解包 | 只实现标准子集；不认识的结构返回「不支持」而不是静默产出错数据 |
| `mapping` | Java 方块状态 → 基岩方块状态；`lit:g_*` 选择；状态串构造；`runtime_id` 解析与缓存 | 映射表是数据，不是代码；解析失败要能定位到具体格 |
| `ghost` | 显示层镜像、子区块分组、批量包组装、重发调度（进服/重生/跨维度/区块重发/心跳） | 单坐标单通道；多投影同格要有优先级 |
| `render` | 层切分、旋转/镜像、表面剔除（只发可见外壳）、LOD | 决定包数，是性能主战场 |
| `easyplace` | 放置射线、支撑与附着判定、库存与耗材、失败重试 | 只读投影 + 读世界，不写世界 |
| `command` | `/lit` 子命令（加载/切换/层/清理/探针/统计） | 权限分级；命令输出带 § 颜色与坐标 |
| `data` | 配置（`config.json`）、会话与显示层快照、日志 | 版本迁移要可回退 |
| `utils` | 日志、`[perf]` 打点、坐标文本 | 打点先行，禁止凭感觉调参 |

## 4. 数据流

```
文件(.litematic/.mcstructure)
  → 解析（NBT/GZIP/调色板）
  → Java 状态 → 基岩状态 → lit:g_* + 状态键
  → 表面剔除（只留外壳）
  → 层/旋转/镜像变换
  → 子区块分组（16³，单包 ≤1024 格变更）
  → holo_core 编码 → UpdateSubchunkBlocks → 目标玩家
```

显示层镜像独立维护：`dimid → 分组索引 → { runtime_id, state, owners }`，作为「玩家挖掉/放下后摘格」与「重发」的唯一真源。

## 5. 与旧 JS 实现的关系

旧实现（`3.1.1`，LSE Node.js + lrca）验证过的资产直接继承：

- BP/RP 的 `lit:g_*` 方块定义与贴图（1277 个方块定义、4094 个资源文件）；
- 状态串口径（`lit:g_x[lit:dir=2]`）与 Java→BE 的映射规则；
- 表面剔除、活塞伸出态推断、附着规则等纯算法；
- 生成脚本（`tools/legacy/`，Node/Python 混合，仅参考）。

**差分策略**：把「包体」当成契约。同一份投影输入，旧 JS 实现产出的分包结构与新实现逐字节比对（子区块分组、每包变更顺序、runtime_id 选择）。差异必须逐条解释，不允许「看起来正确」。

不继承的部分：表单 UI（改为命令 + 数据驱动 UI）、Node 运行时依赖、`.litproj` 预编译产物（暂无必要）。

## 6. 待决问题（进入实现前必须落定）

| ID | 问题 | 现状 / 候选 |
|---|---|---|
| Q1 | `runtime_id` 从哪来 | **已定**：库内走的是 BDS 注册表 —— `Block::tryGetFromRegistry(HashedString(name), Block::BlockStatesType)` 然后取 `block->mNetworkId`（`platform/levilamina/src/block/BlockState.cpp:108`）。本插件可以直接照用（LL 暴露 `mc/world/level/block/Block.h`），把「状态串 → runtime id」做进程内缓存即可，不需要 C ABI 提供解析函数，也不需要 LSE |
| Q2 | 发包 API 的落点 | 包 id 已由 `holo_core_packet_id_update_subchunk_blocks()` 给出，body 由编码器给出；剩下要确认的是「包头（PacketHeader）是否由 LeviLamina 的 NetworkSystem 自动补」——需要一次最小对拍（1 格、1 子区块）确认 |
| Q3 | 映射表真源 | `resources/data/` 下的表由 `tools` 从 BP + 映射规则生成，还是继续维护手工表？生成链要能被 CI 校验 |
| Q4 | 线程模型 | 首版全同步、主线程分帧（每 tick 预算 < 5 ms）；解析能否放到工作线程取决于 LL 的 API 线程约束 |
| Q5 | 崩溃隔离 | 每个入口 `try/catch(...)` 转错误码，异常不跨边界；`holo_core` 调用失败要能安全回退逐格通道 |
| Q6 | 是否改前置库 | 路线 2（给 `HologramLib` 加 `holo_core_block_batch_set/clear` 的 C 导出，内部转调 `BlockBatchManager`）能省掉「packed 解析 + 分组 + tick 预算」全部重写，但会让本插件依赖一个非上游的库版本；需与部署方确认能否接受自维护的 HologramLib 构建 |

## 7. 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| HologramLib 换代（ABI stamp 变化） | 编译产物失效 | 版本号写进 README 与启动日志；换库强制重编 |
| `holo_core` 的 `sub_*` 语义（子区块原点）未对拍 | 包错位，客户端显示整体偏移 | 先做 1 格/1 子区块的最小对拍，再放量 |
| 批量包与逐格覆盖混用 | 同格互相顶掉、闪烁 | 单坐标单通道；混用需显式分区 |
| 客户端碰撞/预测抖动（假方块不可穿） | 体验问题，不可修 | 产品层处理（可穿区域才用批量通道） |
| 映射口径漂移 | 显示错块，难排查 | 差分 + 「不认识就报错」策略 |
| BP/RP 与插件版本不同步 | 状态串解析失败 | 启动自检 + `lit-tools addon` 校验 |
