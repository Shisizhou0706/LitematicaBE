# 阶段二执行计划：C++ 内核（C ABI）

> 上游：`docs/plan-3stage-roadmap.md`（三阶段路线图）。
> 前置结论：**阶段一的库投影调用已可用**——HologramLib 的 `blockBatchSet` / `blockBatchClear` /
> `blockFakeResolve` 通路在真机跑通，`GhostSink` 的 `packet` 后端已接进 `GhostRenderer`。
> 因此本文只规划阶段二：把「解析 → 映射 → 外壳 → 打包」搬到 C++，世界显示仍由 HologramLib 发包。
>
> 本文是**执行计划**，不含架构漫谈。每个任务都有：产出文件、实现要点、验收方式、工时。
> 与路线图冲突处以本文为准，冲突点集中列在 §3.3。

---

## 1. 阶段一收口状态（阶段二的前提）

### 1.1 已落地（当作阶段二的既有资产）

| 能力 | 落点 | 阶段二怎么用 |
|---|---|---|
| 后端选择 `world` / `packet` / `auto` | `src/ghost/GhostSink.js`、`config.json` 的 `render.ghostBackend` | **回退开关的载体**：core 出问题不影响它 |
| 发包三通道 paint/pin/unpaint/restore | `src/render/PacketGhostSink.js` | core 的 `pack` 输出直接喂它的 `paint()` |
| 状态串唯一出口 | `src/mappings/BedrockStateSchema.js`（`toStrictStates` / `toStateString` / `normalizeName`） | 阶段二**不搬**，留在 JS；core 只输出「方块名 + 状态对象」 |
| 显示层镜像 | `src/ghost/DisplayVoxelStore.js` | 不动 |
| 重发调度 | `src/render/GhostRepaintScheduler.js` | 不动 |
| 能力探针 | `src/ghost/HoloProbe.js` | 真机排查 core 失败是否只是"库没装" |
| packed 文本组装 | `PacketGhostSink.packBatches()`（`maxPackedChars=256 KB`、`batchCells=4096`） | **核心差分对象**，先抽成纯函数（T2.1） |

### 1.2 阶段一遗留、必须在阶段二开工前落记录（0.5 人日，不计入阶段二）

1. `work/probe-report.md` 补上真机探针原始输出（`/lit ghost probe` 的报告文本），尤其是
   `resolve.custom` 的 `nid` 与 `batchSelfTest` 的毫秒数——阶段二的性能目标要拿这两个数字做基线。
2. `CHANGELOG.md` 补 `## [3.2.0]` 段落（阶段一的 DoD 表逐条打勾或标注未测）。
3. 三个**未收口**的风险要在文档里写明状态，不要假装已解决：
   - R2 客户端碰撞/预测抖动（假方块不可穿）；
   - R3 全局 8 包/tick 在多玩家同时加载时的排队表现；
   - R4 `blockBatchSet` 与 `viewBlockById` 混用时的覆盖冲突。
   这三条不阻塞阶段二，但**阶段二的 DoD 只承诺"解析侧"的性能**，不要把它们算进 core 的收益。

---

## 2. 阶段二的目标与非目标

### 2.1 目标（只这三条）

1. `解析 → 映射 → 外壳 → 打包` 只有一份实现，JS 与 C++ 的输出**逐字节一致**（差分 100%）。
2. 10 万格 `.litematic` 的「读盘 → packed」P90 < 150 ms（当前 JS 基线 1~3 s），单 tick 阻塞 < 5 ms。
3. core 与宿主无关：不 include / 不链接 LeviLamina、Endstone 任何头文件，只依赖 C++17 标准库 + miniz 单文件。
   宿主差异全部收在两个薄壳里（本阶段只做 LL 侧，Endstone 留给阶段三）。

### 2.2 非目标（明确不做，避免范围膨胀）

- 不做自有协议编码：`UpdateSubchunkBlocks` 的 16³ 分组、≤1024 格/包、8 包/tick 全交给 HologramLib。
- 不做多线程：core 全同步、无自有线程、无锁；并发靠 BDS 主线程顺序调用。
- 不替换 `GhostRenderer` 主循环、不改 tick 预算、不改 `RenderScheduler`。
- 不做 `.litproj` 预编译产物（那是阶段三 3.3）。
- 不做状态串（`lit:g_x[lit:dir=2]`）生成：留在 `BedrockStateSchema`，core 只给「名字 + 状态键值」。
- 不做 ffi（不引入 `node-ffi-napi`），跨语言只走 lrca（`ll.import`）。

### 2.3 成功判据（与路线图一致的量化表）

| 指标 | 目标 | 测法 |
|---|---|---|
| 10 万方块解析 P90 | < 150 ms | `litcore_cli bench` + 真机 `[perf]` |
| 10 万方块 P50 | < 80 ms | 同上 |
| 内存 / 10 万格 | < 8 MB | arena 统计（`litcore_stats()`） |
| 差分一致率 | 100%（≥20 样例 × 3 旋转 × 2 镜像） | `npm test`（`tests/test_core_diff.js`） |
| 崩溃 | 0（100 次加载/卸载 + 1 万次 fuzz） | `litcore_cli fuzz` |
| 回退 | `core.enabled=false` 与 3.3.0 纯 JS 行为逐字一致 | A/B 断言（T2.11） |

---

## 3. 架构决策（先拍板，再动手）

### AD-1 跨语言只走 lrca，不做 ffi

core 编译成静态库 `litcore_static`；`platform/levilamina` 是**唯一** include LL 头的薄壳，链接静态库并把 C ABI 包装成 lrca 导出；JS 侧 `ll.import('LitCore', 'pack')`。

- 理由：HologramLib 已验证这条路（它就是 lrca 前置库）；`node-ffi-napi` 是运行时新依赖，违反 G4，且在 LSE 的 Node 构建里不可靠。
- 代价：DLL 必须以 **LL 插件**形态安装（`plugins/LitematicaBECore/` + 自己的 `manifest.json`），发布包多一个顶层目录（见 §7）。

### AD-2 部署形态：两个插件目录，不是一个

```
plugins/LitematicaBE/        # LSE JS 插件（现有）
plugins/LitematicaBECore/    # LL C++ 插件：lit_core.dll + manifest.json
```

- 不能把 DLL 塞进 `plugins/LitematicaBE/`：LL 按目录扫 `manifest.json`，一个目录只能是一种插件类型（`lse-nodejs` 与 native 冲突）。
- 两个包**同版本号发布**（`build-release.mjs` 一次产出），但 core 的 ABI 版本独立（`LITCORE_ABI_VERSION=1`）。
- JS 侧必须能在 `LitCore` 缺失时完全正常工作（`core.enabled=auto` → 检测不到就纯 JS）。

### AD-3 先冻结「JS 口径」再做 C++

阶段二最容易翻车的地方不是 C++ 写不出来，而是**JS 侧的行为没有单一入口**（`packBatches` 在 sink 实例方法里、映射分散在三处、外壳提取有 `transparentOccluders` 分支）。所以第一步是把要搬进 core 的四段逻辑抽成**无副作用的纯函数模块 + golden 用例**，它就是差分 oracle：

| 要搬的逻辑 | 现在在哪 | 抽到哪 |
|---|---|---|
| packed 行/分段 | `PacketGhostSink.entryLine/packBatches` | `src/ghost/PackedBatch.js` |
| Java→BE 状态 + ghost 选块 | `BlockMappingRegistry` + `GhostBlockRegistry.resolveWithState/buildStates` | `src/mappings/GhostResolve.js`（薄封装，规则表不搬） |
| 外壳提取 | `src/ghost/SurfaceExtractor.js` | 就地纯函数化（已有 `static extract`） |
| 活塞伸出态 | `src/ghost/PistonStateInfer.js` | 不动，只加 golden |

抽完之后 `PacketGhostSink` 改为调用新模块，**输出逐字节不变**（用现有 `tests/test_ghost_sink.js` 的断言锁死）。

### AD-4 core 的输入/输出只有两种粒度

| 入口 | 输入 | 输出 | 用途 |
|---|---|---|---|
| `litcore_parse` | 文件路径 + 解析选项 | block 数组（POD + 字符串池） | **差分定位**：哪一格错了能直接看出来 |
| `litcore_pack` | 文件路径 + 全选项（旋转/镜像/锚点/层/剔除/packed 预算） | 若干段 packed 文本 | **生产路径**：一次跨界拿到能直接 `paint()` 的东西 |

两个入口共用同一份内部实现（parse → resolve → surface → pack），`parse` 只是提前返回。差分先跑 `parse`（错误好定位），再跑 `pack`（字节级验收）。

### AD-5 映射表：JSON 是真源，BIN 是派生产物

`resources/data/ghost_index.json` 仍是唯一真源（`AGENTS.md` 的教训：`attached_rules` 抄表已出过事）。core 不自己解析这坨 JSON，而由新增的构建脚本生成 `resources/data/core_map.bin`：

- BIN 头部带 `sourceSha256`（= `ghost_index.json` 的字节哈希）+ `generatorVersion`；
- core `litcore_init` 校验 run-time 传入的 JSON 字节哈希与 BIN 头一致，不一致返回 `LITCORE_E_MAP_STALE`，JS 侧**整体回退纯 JS**（绝不用半新半旧的表）；
- `tools/build-release.mjs` 与 `npm run check:tracked` 增加"BIN 与 JSON 同步"校验（T2.6）。

- 理由：C++ 里塞 JSON parser 是白送的 bug 源；派生 + 哈希绑定既保住"只有一份数据"，又让 core 只读二进制。
- 退路（若 BIN 方案被判太重）：core 内嵌 120 行只读 JSON 解析器，仍读同一份 JSON。

### AD-6 CLI 是差分的唯一可行通道

`npm test` 是**纯离线 Node**，没有 ffi，加载不了 DLL。因此 core 必须自带命令行：

```
core/tools/litcore_cli.cpp  →  build/litcore_cli.exe
  litcore_cli abi                         # 版本握手
  litcore_cli parse <file> [--json]       # block 数组 → stdout
  litcore_cli pack <file> --opts=OPTS.json  # 段 → stdout（段分隔 \x1e）
  litcore_cli bench <file> --n=50         # P50/P90 + 峰值 arena
  litcore_cli fuzz --n=10000 --dir=fixtures/  # 崩溃计数（退出码 = 崩溃数）
```

`litcore_cli` 与 `lit_core.dll` **链接同一个静态库**（不是复制代码），这样 CI 里测的就是发布的那份实现。JS 测试用 `spawnSync` 调它、与 JS 实现逐字节对比；找不到 exe 时 **skip 而不是 fail**（本地无编译器的人仍能 `npm test`），CI 的 windows job 负责真正跑。

### AD-7 崩溃隔离：三层防护，不靠自觉

1. 每个导出入口 `try { ... } catch (...) { return LITCORE_E_INTERNAL; }`，异常**绝不跨 C ABI**；
2. core 编译选项 `/EHsc` + `/GS` + `/guard:cf`，`NDEBUG` 下不做 assert；所有文件读入前先 `stat` 大小上限（默认 256 MB，超限 `E_BUDGET`）；
3. 只在 result 句柄内部用 `new/delete`，对外只有 `litcore_*_free`；禁止跨边界传递 STL 容器。

### AD-8 不支持就明说，绝不降级产出

JS 的 `LitematicLoader` 有一堆历史兼容分支（`Region.LiteTickets` JSON 变体、缺 `Size`、非标准 palette 等）。core **只实现标准 `.litematic` / `.mcstructure` 子集**，遇到不认识的结构返回 `LITCORE_E_UNSUPPORTED`，JS 侧据此走旧路径并在日志里记一次「core 不支持该文件，已回退 JS」。

> 这条比"尽量兼容"重要：core 静默产出错数据 = ghost 显示错，比慢十倍难查得多。

---

## 4. C ABI v1 草案（`core/include/litcore.h`）

纯 C、固定宽度、无平台类型。`LITCORE_ABI_VERSION = 1`。

```c
typedef enum {
    LITCORE_OK              = 0,
    LITCORE_E_ARGS          = 1,   /* 参数非法（空指针/负尺寸/未知选项） */
    LITCORE_E_IO            = 2,   /* 打开/读取失败 */
    LITCORE_E_FORMAT        = 3,   /* NBT/调色板/位打包损坏 */
    LITCORE_E_UNSUPPORTED   = 4,   /* 结构合法但超出 core 子集（JS 回退） */
    LITCORE_E_NOMEM         = 5,
    LITCORE_E_INTERNAL      = 6,   /* 兜底：C++ 异常被捕获 */
    LITCORE_E_MAP_STALE     = 7,   /* core_map.bin 与 ghost_index.json 不匹配 */
    LITCORE_E_BUDGET        = 8    /* 超出块数/字节预算 */
} litcore_status;

/* --- 版本与错误 --- */
int32_t     litcore_abi_version(void);   /* = 1，JS 启动时校验，不等则禁用 core */
const char* litcore_build_info(void);    /* 静态 JSON：git sha / built_at / miniz / flags */
const char* litcore_last_error(void);    /* thread_local 诊断串，下一次调用前有效 */

/* --- 生命周期 --- */
typedef struct litcore_handle litcore_handle;
int32_t litcore_init(const char* ghost_index_json, uint32_t len,
                     const char* core_map_bin, uint32_t map_len,
                     litcore_handle** out);
void    litcore_shutdown(litcore_handle* h);

/* --- 粒 1：结构级（差分定位用） --- */
typedef struct {
    int32_t  x, y, z;
    uint32_t name_off;    /* 字符串池偏移（UTF-8, NUL 结尾） */
    uint32_t state_off;   /* 同上；无状态时指向空串 */
    uint8_t  kind;        /* 0=方块 1=空气 2=未知（落 generic） */
    uint8_t  _pad[3];
} litcore_block;          /* 20 B，显式 packed 语义由头文件注释冻结 */

typedef struct litcore_result litcore_result;
int32_t           litcore_parse(litcore_handle*, const char* path_utf8,
                                const litcore_parse_opts*, litcore_result** out);
const litcore_block* litcore_result_blocks(const litcore_result*, uint32_t* count);
const char*       litcore_result_pool(const litcore_result*, uint32_t* bytes);
uint32_t          litcore_result_stats(const litcore_result*, uint32_t* dims /* x,y,z,layers */);
void              litcore_result_free(litcore_result*);

/* --- 粒 2：生产路径（逐字节差分对象） --- */
typedef struct {
    uint32_t rotation;            /* 0..3，顺时针 90°*n（只改坐标，不转状态：与 JS 一致） */
    uint8_t  mirror;              /* 0/1：沿 x 镜像 */
    uint8_t  cull;                /* 0/1：六向剔除 */
    uint8_t  includeAir;          /* 0/1 */
    uint8_t  transparentOccluders;/* 0/1：与 SurfaceExtractor 构造参数同义 */
    int32_t  layer;               /* -1=不限；否则只取该相对 y */
    int32_t  origin[3];           /* 锚点：世界坐标 = origin + 变换后相对坐标 */
    uint32_t maxPackedChars;      /* 与 PacketGhostSink.maxPackedChars 同义，默认 262144 */
    uint32_t batchCells;          /* 与 PacketGhostSink.batchCells 同义，默认 4096 */
} litcore_pack_req;

typedef struct litcore_pack_out litcore_pack_out;
int32_t              litcore_pack(litcore_handle*, const char* path_utf8,
                                 const litcore_pack_req*, litcore_pack_out** out);
uint32_t             litcore_pack_out_count(const litcore_pack_out*);
const char*          litcore_pack_out_seg(const litcore_pack_out*, uint32_t i, uint32_t* len);
const uint32_t*      litcore_pack_out_cells(const litcore_pack_out*, uint32_t i); /* 该段格数 */
void                 litcore_pack_out_free(litcore_pack_out*);

/* --- 3.0 才需要，先占位不实现 --- */
int32_t litcore_pack_mcmega(litcore_handle*, const char* dir_utf8,
                            int32_t cx, int32_t cy, int32_t cz, /* 指定分块，Mega 增量注入用 */
                            const litcore_pack_req*, litcore_pack_out** out);
```

约定：

- 所有 `const char*` 入参是 **UTF-8**，长度由调用方给（不让 core 依赖 `strlen` 之外的东西）；
- 所有返回指针的生命周期**绑在对应句柄/结果上**，`*_free` 之后失效；
- 每个导出都必须能接受 `nullptr` 而不崩（返回 `E_ARGS`）；
- 段分隔符在 lrca 包装层用 `\x1e`（packed 内不可能出现），不用 JSON 包（省一次解析）。

### 4.1 lrca 导出层（`platform/levilamina`）

```cpp
// 只在这里 include LL 头；core 对此一无所知
LL_REGISTER_EXPORT("litcore_pack")   // 具体注册写法以当前 LL 版本 API 为准（M0 spike 确认）
```

导出清单（JS 侧 `ll.import('LitCore', name)`）：

| 导出名 | 返回 | 说明 |
|---|---|---|
| `litcore_version` | string | `"1|git_sha|built_at"`，JS 用它做握手 |
| `litcore_pack` | string | 段用 `\x1e` 连接；失败返回 `"ERR:<code>:<msg>"` |
| `litcore_parse_json` | string | 结构级（调试/差分用，返回 JSON） |
| `litcore_stats` | string | arena 峰值 / 缓存命中 / 上次耗时 |
| `litcore_pack_mcmega` | string | T2.9 之后 |

**M0 必须先验的 spike（0.5 人日）**：

1. lrca 能不能传/回 256 KB 级字符串？不行就降级为：core 写到 `plugins/LitematicaBE/data/core_out.bin` + 返回 `"FILE:<path>:<len>"`，JS 读文件（仍然只有一次跨界）。
2. `ll.import` 返回的 string 是否会因 `EngineScope` 缺失而抛（`AGENTS.md` 的 UpdateChecker 教训）——**core 调用只允许发生在命令回调 / onTick 里**，不在异步回调里。
3. `manifest.json` 的 `dependencies` 是否要显式声明（core 插件依赖 LL 版本）。

---

## 5. 任务分解

工时单位：人日（含自测，不含缓冲）。依赖用 `→` 表示必须先完成。

| ID | 任务 | 产出 | 实现要点 | 验收 | 工时 |
|---|---|---|---|---|---|
| **T2.0** | 基线打点 | `src/utils/PerfTrace.js` | 分段计时器（`begin/end/mark`），只在 `misc.debugLog` 或 `render.ghostPerf=true` 时收集；结束时一行 `[perf] parse=..ms map=..ms surf=..ms pack=..ms cells=.. segs=..`；零分配（复用一个 Float64Array 环形缓冲） | `/lit ghost stats` 能出 3.1.1 的真实基线（阶段一已含 packet） | 0.5 |
| **T2.1** | 冻结 JS 口径 | `src/ghost/PackedBatch.js`、`src/mappings/GhostResolve.js`、`tests/fixtures/packed/*.json` | `entryLine`/`packBatches` 抽成纯函数（签名吃 `{maxPackedChars,batchCells}`）；`PacketGhostSink` 改为调用；`GhostResolve` 薄封装 `GhostBlockRegistry.resolveWithState + buildStates`；产出 ≥20 条 golden（含 `lit:dir`、超长状态、含 `[`/`;` 的降级路径） | `tests/test_packed_batch.js` 全绿，且 `test_ghost_sink.js` 断言不变 | 1.0 |
| **T2.2** | core 骨架 + ABI + CLI | `core/CMakeLists.txt`、`core/include/litcore.h`、`core/src/api.cpp`、`core/tools/litcore_cli.cpp` | 静态库 + DLL 双 target；`litcore_abi_version` / `build_info` / `last_error`；`litcore_cli abi`；MSVC `/W4` 零警告；miniz 单文件入库 `core/third_party/miniz.c`（vendored，带 LICENSE） | `litcore_cli abi` 返回 `1`；CI 编过 | 1.5 |
| **T2.3** | NBT + `.litematic` | `core/src/nbt_reader.{h,cpp}`、`core/src/litematic.cpp` | 大端 NBT（对照 `src/core/NBT.js` 的 12 种 tag）；GZIP 走 miniz；`Regions` → `Size/Position/TileEntities/Palette/BlockStates`；位打包：`bits = max(2, ceil(log2(palette)))`，跨 long 边界按 JS 的 `PaletteDecoder` 口径；**标准子集之外返回 `E_UNSUPPORTED`** | `litcore_cli parse` 与 JS 的 blocks 逐格一致（≥8 样例：空/单格/负坐标/超大 palette/多层/旋转源） | 3.0 |
| **T2.4** | `.mcstructure` | `core/src/mcstructure.cpp` | 小端 NBT（对照 `src/core/McstructureLoader.js`）；`structure.block_indices` + `palette`；`block_position_data` 忽略 | 同上（≥5 样例） | 1.5 |
| **T2.5** | 映射表 | `core/src/mapping.cpp`、`tools/gen-core-map.mjs`、`resources/data/core_map.bin` | BIN 布局：header（magic/ver/sourceSha256）+ name pool + javaMap 定长哈希表 + javaMapByState 扁平前缀树（`_default` 单独标记）；`resolve` 的 BE 名反查（`bedrockToJava`）也要进 BIN | 20 个边界方块（`observer[powered=false]`、`piston[extended]`、`slime`→`slime_block`、未知方块→generic）解析出的 ghostId 与 JS 一致 | 2.0 |
| **T2.6** | 外壳 + 活塞 | `core/src/surface.cpp` | 逐字复刻 `SurfaceExtractor`：空气/透明/非满格三张集合（从 JS 导出成 `resources/data/surface_sets.json` 一并进 BIN，杜绝抄表）、按 y 分桶、按层升序输出、`cull` 的六向判定顺序；`PistonStateInfer` 的推断表同源 | 同一 blocks 输入，`kept/culled/layers` 三个统计与输出顺序完全一致 | 2.0 |
| **T2.7** | 打包 | `core/src/pack.cpp` | 复刻 `PackedBatch`：行格式 `x y z state`、`;` 连接、`maxPackedChars` 与 `batchCells` 双预算；**额外按子区块排序**（`floor(x/16),floor(y/16),floor(z/16)` 字典序）提高 HologramLib 的合并率——该排序在 JS 侧同步开启，否则差分不等 | `litcore_cli pack` 与 JS `packBatches` 逐字节一致（≥20 样例） | 1.0 |
| **T2.8** | 变换/层/锚点 | `core/src/transform.cpp` | 复刻 `transformPosition`（只改坐标、不转状态）+ `applyLayer` 的层过滤口径 | 3 旋转 × 2 镜像是逐格一致 | 0.5 |
| **T2.9** | `.mcmega` 分块读 | `core/src/chunkstore.cpp` | 只读 `BinaryChunkStorage` v2（`MCMEGA_MAGIC`/`HEADER_SIZE=9`/`CHUNK_SIZE=32`，格式冻结不许改）；gunzip + palette 解码 | 任取一个真机 mega 目录的 3 个分块，与 `loadChunkFromDisk` 逐格一致 | 1.0 |
| **T2.10** | 差分套件 | `core/tests/`（C++ 单测）、`tests/test_core_diff.js`（JS 驱动 CLI）、`tests/fixtures/core/`（脚本合成，不入库大文件） | 样例生成器 `tools/gen-core-fixtures.mjs`：程序化生成 20+ 样例（空、单格、全空气、超大 palette、负坐标、旋转/镜像、截断文件、坏 gzip、palette 越界）；JS 侧 `spawnSync(litcore_cli)` 比对；exe 缺失 → skip + 明确日志 | `npm test` 全绿；差分报告里每个样例一行 `OK/DIFF` | 2.0 |
| **T2.11** | 宿主桥 + JS 回退 | `platform/levilamina/`（CMake + manifest + 导出）、`src/core/CoreBridge.js` | `CoreBridge` 双实现：`JsBridge`（现有路径）与 `CppBridge`（`ll.import`）；`core.enabled = off/auto/on`；握手失败/`E_MAP_STALE`/`E_UNSUPPORTED` → 单次 warn + 落回 JS；`E_INTERNAL` → **熔断**（本次进程内永久回退，避免每格重试） | `core.enabled=false` 与 3.3.0 行为逐字一致（A/B 断言） | 2.0 |
| **T2.12** | 发布接线 | `tools/build-release.mjs`、`tooth.json`、`.gitignore`、`tools/check-tracked.mjs`、`tools/sync-version.mjs`、`tools/check-version.mjs` | zip 顶层加 `LitematicaBECore/`；`PLUGIN_FILES`/新 `CORE_FILES`；tooth 加第二个 placement（`src: LitematicaBECore/` → `dest: plugins/LitematicaBECore/`）；`.gitignore` 白名单放行 `core/`（含 `!core/third_party/miniz.*`）；core 的 `manifest.json` 版本进 `check:version`（第 6 处） | `npm run build` 产物含 DLL；`check:tracked`/`check:version` 通过 | 1.0 |
| **T2.13** | CI | `.github/workflows/ci.yml` | 新增 windows job：CMake configure+build → `litcore_cli` 单测 → 差分（`npm test` 里自动跑）→ 上传 exe 供排查；**不依赖 BDS** | PR 上 core job 绿 | 0.5 |
| **T2.14** | 灰度与收口 | `CHANGELOG.md`、`docs/renderer-contract.md` 补 core 段、`VERSIONING.md` 补 core 版本 | `3.3.0` 默认 `core.enabled=auto`（有 DLL 且握手过才用）→ 真机 A/B 抽样（楼梯/朝向/活塞/半砖、6 万格 Mega 切层）→ 决定是否 `4.0.0` 默认开 | DoD 表逐条打勾；Gate 2 结论落文档 | 1.0 |

**合计：21.5 人日**（路线图估 18）。多出的 3.5 主要来自路线图未列的两项：T2.1 冻结 JS 口径（1.0）、T2.6/T2.5 的「集合/映射表进 BIN」（1.0）、T2.10 的样例生成器（1.5）。
**要压回 18 的两条路（按需选一条）**：① 砍 T2.9（Mega 分块读仍在 JS，core 只吃内存 blocks——Mega 场景收益后置）；② T2.6 首版只做 `cull=true/false` 两档，不做透明遮挡细分。

### 5.1 依赖图与里程碑

```
M0  T2.0 ─┬─ spike(lrca 大字符串/导出注册)           0.5d
M1  T2.1 → T2.2 → T2.3 → T2.10(部分)                5.5d   ← 第一个可测的差分
M2  T2.4 → T2.5 → T2.6 → T2.8                      6.0d   ← parse 差分 100%
M3  T2.7 → T2.9 → T2.10(完整)                      4.0d   ← pack 逐字节 100%
M4  T2.11 → 真机 A/B                                2.0d
M5  T2.12 → T2.13 → T2.14                          2.5d
```

每个里程碑都可单独提交、可单独回退（core 默认关）。M3 结束时若差分不为 100%，**不进 M4**（Gate 2 的"有条件通过"只允许"已知口径差"，且必须先改 JS 侧统一口径再重跑）。

---

## 6. 算法口径清单（C++ 必须逐条复刻，逐条有 golden）

> 这一节是差分失败的定位表。每条右侧是对应 JS 源，改动任何一侧都要同步 golden。

| # | 口径 | JS 源 | 易错点 |
|---|---|---|---|
| 1 | `.litematic` 是 **大端** NBT + GZIP；`.mcstructure` 是 **小端** | `src/core/NBT.js`（`getUint16` 大端）、`src/core/McstructureLoader.js` | 端序反了会"能读但坐标全错" |
| 2 | palette 位宽 `bits = max(2, ceil(log2(n)))`，位打包**跨 long 不回绕** | `src/core/PaletteDecoder.js` | 跨 64 位边界是最常见 off-by-one |
| 3 | 负坐标：区域 `Position` 可能为负，相对坐标 → 世界坐标是**加法** | `LitematicLoader` | 用无符号读会变成天文数字 |
| 4 | ghost 选块：先 `javaMap` 直查 → 再 `bedrockToJava` 反查候选 → 落 `lit:g_generic` | `GhostBlockRegistry.resolve` | 漏掉 BE 名反查会让 `minecraft:slime` 变灰块 |
| 5 | 按状态换块：`javaMapByState` 逐层 `_drillDown`，缺失值用该层 `_default`（**保留上层语义**） | `GhostBlockRegistry._drillDown` | 直接 fallback 到 `javaMap` 会把伸出活塞画成收缩 |
| 6 | 状态收窄：只输出 ghost 声明的键，缺失补 `spec.default`，`int` 夹 `min/max`，`bool` 转真布尔 | `GhostBlockRegistry.buildStates` | 多写一个键 → BDS `setBlock` 返回 false（静默失败） |
| 7 | 空气判定包含 `*_air` 后缀；透明判定含 `*_glass*` 后缀；非满格集合是**三张表 + 前缀 + 后缀** | `src/ghost/SurfaceExtractor.js` | 表必须从 JS 导出，不许 C++ 手抄 |
| 8 | 外壳输出顺序：按 y 升序，同层保持输入顺序（稳定） | `SurfaceExtractor.extract` | 顺序变了 → packed 字节不等，但显示一样（最难查） |
| 9 | 旋转只改坐标（`transformPosition`），**不改方块状态朝向** | `src/ghost/GhostRenderer.js` | 这是长期既有行为，别"顺手修好" |
| 10 | 层过滤用**投影内相对 y**（与 `applyLayer` 同口径），不是世界 y | `ProjectionRenderer.applyLayer` | 锚点偏移后两者相差 origin.y |
| 11 | 段预算：先判 `curChars + add > maxPackedChars` 再判 `cur.length >= batchCells`，**先切再放** | `PacketGhostSink.packBatches` | 边界格（恰好等于预算）差一格 |
| 12 | 状态含 `;,()[]` 时降级为纯 id（丢状态保 packed 完整） | `PacketGhostSink.stateString` | 只在 core 输出「名字+状态」，此判定留在 JS 侧 → **core 不实现**，但 T2.7 必须保证名字里不含分隔符 |

---

## 7. 发布接线（T2.12 细节）

| 位置 | 现状 | 阶段二改动 |
|---|---|---|
| `build-release.mjs` | `PLUGIN_FILES` 只有 JS 侧 | 新增 `CORE_FILES = ['manifest.json', 'lit_core.dll']`，从 `build/core/` 拷到 zip 的 `LitematicaBECore/`；DLL 缺失 → 直接 fail（不要静默出无 core 的包） |
| `tooth.json` | 1 个 placement | 新增 `{type:'dir', src:'LitematicaBECore/', dest:'plugins/LitematicaBECore/'}` |
| `.gitignore` | 白名单制 | 放行 `core/`（源码），**排除** `core/build/`；`!core/third_party/miniz.*` 必须显式入库（否则 CI 干净检出编不过） |
| `check-tracked.mjs` | 校验被引用文件入库 | 增加 core 源文件与 `core_map.bin` 的入库校验 |
| `check-version.mjs` | 校验四处 + 三元组 + addon 五处 | 增加 `LitematicaBECore/manifest.json` 的 `version`（第 6 处）；`sync-version.mjs` 一并复写 |
| `AGENTS.md` | 无 core 说明 | 新增「core」小节：ABI 版本、`core.enabled`、`litcore_cli` 用法、`core_map.bin` 派生关系 |

`core.enabled` 配置（`config.json`）：

```jsonc
"core": {
  "enabled": "auto",        // off | auto | on
  "dll": "",                // 空 = 用 LitCore 导出的默认插件
  "fuseOnError": true,      // E_INTERNAL 一次即熔断到进程结束
  "benchmark": false        // true 时打 [perf] 行
}
```

---

## 8. DoD 与 Gate 2

| 指标 | 目标 | 测法 | 闸门 |
|---|---|---|---|
| 差分一致率（parse） | 100%（20 样例） | `tests/test_core_diff.js` | 硬 |
| 差分一致率（pack） | 100%（20 样例 × 3 旋转 × 2 镜像） | 同上 | 硬 |
| 10 万格 P90 | < 150 ms | `litcore_cli bench` + 真机 `[perf]` | 硬 |
| 10 万格 P50 | < 80 ms | 同上 | 软 |
| 内存 / 10 万格 | < 8 MB | `litcore_stats` arena 峰值 | 硬 |
| 单 tick 阻塞 | < 5 ms | `RenderScheduler` 打点 | 软（真机） |
| 崩溃 | 0 | 100 次加载/卸载 + 1 万次 fuzz | 硬 |
| 回退 | `core.enabled=false` 与纯 JS 逐字一致 | A/B 断言 | 硬 |
| 发布 | zip 含 DLL、`check:tracked`/`check:version` 通过 | CI | 硬 |

**Gate 2 判定**：

- ✅ 全绿 → 进阶段三（Endstone 只写薄壳）。
- ⚠️ 差分有差异但**全是已知口径差**（顺序、边界格）→ 先改 JS 侧统一口径（改 JS 而不是改 core，因为 JS 是 oracle），重跑；仍不达标不进阶段三。
- ❌ ABI 崩溃 / 映射表漂移 / lrca 大字符串不通 → 放弃共享内核，阶段三改 Python 独立实现；阶段一的假方块结论仍然有效。

---

## 9. 风险登记（只列阶段二新增/变化项）

| ID | 风险 | 影响 | 缓解 | 触发信号 |
|---|---|---|---|---|
| R11 | lrca 传不了 256 KB 字符串 | 生产路径退化 | M0 spike；退路 = core 写文件 + JS 读（仍一次跨界） | spike 返回被截断 |
| R12 | `litcore_cli` 与 DLL 不同构（不同编译选项） | 差分通过但真机错 | 同一静态库 target；CI 断言 DLL 与 CLI 的 `build_info` 一致 | 真机与 CI 结论打架 |
| R13 | `core_map.bin` 与 `ghost_index.json` 漂移 | 全服 ghost 错块 | header `sourceSha256` + `E_MAP_STALE` 整体回退；`check:tracked` 校验 | 启动 warn `MAP_STALE` |
| R14 | 双插件安装顺序/版本错配 | core 装了但 JS 不用，或反之 | JS 握手 `litcore_version` 与 `PLUGIN_VERSION` 比对；不一致只 warn 并回退 | `/lit ghost backend` 显示 core=off |
| R6' | DLL/宿主 CRT 堆不共享 | 崩溃 | 只在 result 内 `new/delete`；`*_free` 成对；禁止跨边界 STL | 首次联调 |
| R7' | LL 升级导致导出失效 | core 单平台失效 | 握手失败 → 纯 JS；`ll.hasExported` 探测 | 升级后 core=off |
| R15 | core 静默产出错数据（最坏） | 显示错、难查 | AD-8：不支持就 `E_UNSUPPORTED`；`E_INTERNAL` 熔断 | 差分差异 + 真机显示异常 |

---

## 10. 需要拍板的三个点（已给推荐，未定则按推荐执行）

1. **部署形态**：接受「`plugins/LitematicaBECore/` 第二个插件目录」吗？
   推荐：接受。否则只能走 ffi（新依赖）或把 core 编进 LSE（不可能）。
2. **映射表形态**：`core_map.bin` 派生 + 哈希绑定 vs core 内嵌 JSON parser？
   推荐：BIN。真源仍是 `ghost_index.json`，漂移由哈希拦住。
3. **M3 的压缩路径**：要压回 18 人日就砍 T2.9（Mega 分块读留 JS）？
   推荐：**不砍**。Mega 是 6 万~10 万格的主场景，砍了等于阶段二在最重要的场景上只收益一半；宁可接受 21.5 人日。

---

## 11. 开工第一周的具体动作（照抄即可）

1. 跑 `/lit ghost probe`，把报告贴进 `work/probe-report.md`（0.5 h）。
2. 写 `src/utils/PerfTrace.js` + `render.ghostPerf`，拿到 3.1.1 真实基线（T2.0）。
3. 抽 `src/ghost/PackedBatch.js`，`PacketGhostSink` 改为调用，`npm test` 全绿（T2.1 前半）。
4. 建 `core/` 骨架 + `litcore.h` + `litcore_cli abi`，CI 先只编不测（T2.2）。
5. M0 spike：写 30 行的 lrca 探针插件，导出一个返回 256 KB 字符串的函数，JS 侧 `ll.import` 校验长度（AD-7 的前提，不通过就先改方案再写 C++）。
