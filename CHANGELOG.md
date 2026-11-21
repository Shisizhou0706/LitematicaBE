# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **`.litematic` 按大端 NBT 解析**：Litematica 是 Java 工具，NBT 为大端；此前按小端读导致「缺少 Regions」。解析器现在支持两种字节序（`.mcstructure` 的小端入口已预留），并用真实投影验证（`horse` 3739 格、`640核刷铁机` 32400 格、`天安门` 563 万格）。
- **中文投影名「未找到该文件」**：Windows 上 `std::filesystem::path{std::string}` 按 CP_ACP 解释入参会抛 `std::system_error`；改为按 UTF-8/936/1252 多代码页解释 + Latin-1 双跳还原（BDS 控制台会把 GBK 字节当 Latin-1 再转 UTF-8），并加目录扫描兜底（忽略大小写、可省略扩展名）。
- 上传页对 `.NET HttpClient` 的 `filename=x`（不带引号）解析失败；现在带引号与不带引号两种 multipart 都能识别。
- 路径输出统一走 UTF-8（`path.string()` 在含中文路径时会抛异常）。

### Added

- 内置 HTTP 上传页（`127.0.0.1:8686`，token 鉴权，拖拽上传到 `schematics/`，只落盘不自动加载）。
- `/lit list`、`/lit reload`（热重载映射表）、`/lit load <file> <x> <y> <z>`（控制台也能用，发给所有在线玩家）。

## [4.0.0] - 2026-11-21

从 LSE Node.js 插件（3.1.1）迁移为 LeviLamina 原生模组。这一版只包含工程骨架，投影功能尚未迁移。

### Added

- 模组入口 `src/LitematicaBE/LitematicaBE.{h,cpp}`，实现 `load` / `enable` / `disable`，加载时准备 `data/` 与 `config/` 目录。
- 阶段 A（显示通道骨架）：
  - `mapping/BlockStateResolve`：状态串 → `Block::mNetworkId`（BDS 注册表 + 进程内缓存），与库的解析口径一致；
  - `ghost/BatchEncoder`：16³ 子区块分组、单包 ≤1024 条、调用 `holo_core.dll` 的 C ABI 编码；
  - `ghost/PacketSender`：varint 包头 + `NetworkPeer::sendPacket(Reliable, Compressible)`；
- 阶段 B（投影解析与数据驱动映射）：
  - `core/nbt`：小端 NBT 解析（Compound/List/数组/字符串），gzip 解压走 miniz（BDS 的 `Zlib` 未导出符号）；
  - `core/schematic`：`.litematic` 解析（多区域、`BlockStatePalette`、`BlockStates` 连续位流跨 long、属性字符串化、跳过空气与结构空位）；
  - `mapping/GhostMap`：数据驱动的方块映射 —— 状态树 + `{state}` 模板 + `set` 覆盖，全量迁入旧表的 1160 个方块 / 256 条状态树；**C++ 不含任何方块硬编码**；
  - `command/SchematicActions`：`/lit info <file>`（解析统计）与 `/lit load <file>`（按原坐标铺投影，上限 3 万格）；
  - `tools/lit_tools/mapgen.py`：旧索引 → `ghost_map.json` 并校验所有 ghost 名存在于 BP；
  - `tools/lit_tools/fixture.py` + `tools/parse-check/`：合成最小 `.litematic` 测例，并用独立 exe 做逐方块对拍（当前 17 格全一致）。
- 阶段 C（表面剔除 / 分帧 / 热加载）：
  - `core/schematic/OcclusionTable` + `resources/data/occlusion.json`：遮挡判定白名单（1160 个 Java 方块中 712 个不遮挡），**不完整方块入表后其相邻面不再被判为被遮挡**；表由 `lit-tools occlusion` 生成并体检；
  - `ghost/SurfaceCull`：去掉 6 个邻居都遮挡的内部方块（O(n)，空间索引一次构建）；
  - `ghost/DispatchQueue`：按 tick 预算发包（默认 8 包/tick），3 万格以上的投影不再一次性 flush；
  - `web/UploadServer` + 内嵌上传页：`127.0.0.1:8686` 拖拽上传 `.litematic`，token 鉴权，只落盘不自动加载；
  - 命令：`/lit list`、`/lit reload`、`/lit load <file> <x> <y> <z>`（控制台可用），文件扩展名可省略；
  - 修复 `.litematic` 字节序（Java NBT 是**大端**，此前按小端读会导致「缺少 Regions」）；`.mcstructure` 的小端入口已预留。
- 阶段 D（显示层镜像 / 重发调度 / 分层显示）：
  - `ProjectionSession` 保存「剔除后的方块表 + 坐标索引 + 当前层编码缓存」，支持 `/lit layer <from> [to]` 与 `/lit unload`；
  - **显示层镜像**：每名玩家记录已收到的坐标集合，配合编码缓存实现零开销重发；
  - **重发调度**：LL 1.9.9 服务端没有 join/respawn/disconnect/tick 事件的 emitter，改用 `ServerThreadExecutor::executeAfter` 的 1 秒轮询；用「`Actor::isDead()` 翻转 = 重生」「位移 > 64 格 = 传送/跨维度」两个状态信号替代高频心跳（心跳降到 5 分钟兜底），并提供 `/lit resend` 手动补发；
  - 控制台无玩家时 `/lit layer` 只计算并缓存，玩家进服后自动补发；
  - 方块变化改为引擎 hook（`set`）+ 150 ms 延迟补发，解决投影格被覆盖后无法自愈的问题。
- 阶段 E（`.mcstructure` 支持）：
  - `core/schematic/McstructureFile`：小端 NBT（未压缩或 gzip）、`block_indices` 双层（主层 + 液体层）、
    索引顺序 `x·sizeZ·sizeY + y·sizeZ + z`、状态键去 `minecraft:` 前缀、`flowing_water → water` 归一化；
  - `/lit info` 与 `/lit load` 按扩展名自动分派，两种格式共用同一套映射/剔除/编码管线；
  - 用样本 `双模式刷石.mcstructure`（2720 格 / 调色板 104）与独立 Python 参考实现逐格对拍：非空气 2186 一致，前 5 格坐标与方块名逐字相同。
- 阶段 F（整体旋转）：
  - `/lit rotate`：顺时针 90° 步进，以 `load` 原点为锚，宽深互换、四次回原位；
  - 旋转时先收回当前层 → 重算坐标与 `mIndex` → 重建编码缓存 → 重新显示同一层区间；控制台无玩家时只更新缓存；
  - 编码修正（会话外）：`TextEncoding`（`toUtf8`/`fromUtf8`/`fromAcp`，失败返回空 path 不抛异常）+ `resolveFile` 显式编码试探 + `/lit` 回调统一 `guard()` 把异常转成聊天栏错误，修复游戏内 UTF-8 文件名触发 `ERROR_NO_UNICODE_TRANSLATION` 的问题。
- 阶段 G（多投影管理）：
  - `ProjectionSession` 从单例改为可多实例（加 `#id` / label）；新建 `ProjectionManager` 负责会话表、当前选中、方块变化分发、玩家状态轮询；
  - 命令：`/lit projects`（列会话）、`/lit select <id>`（切换）、`/lit remove <id>`（收回并删除）；`layer/rotate/unload/resend` 作用于当前选中；
  - 方块 hook 改为 `requestRepaintAt` 分发到所有包含该坐标的会话；玩家进服/重生/传送/换维度时所有会话各自重发（会话外：`isDead` 翻转判重生 + 64 格位移判传送）；
  - 修复「加载第二个投影后失去对第一个的控制、重载区块后第一个消失」。
- 阶段 F（中文路径崩溃修复）：
  - 根因：`std::filesystem::path{std::string}` 会按本地代码页（GBK/936）转换，游戏内聊天栏传来的是 UTF-8，
    中文文件名触发 `MultiByteToWideChar` 失败 → 抛 `std::system_error`（`ERROR_NO_UNICODE_TRANSLATION` / 0x459），
    表现为「聊天栏无输出 + 控制台 `command threw an exception`」。
  - 新增 `core/util/TextEncoding`（`toUtf8` / `fromUtf8` / `fromAcp` / `sanitizeUtf8`），全程显式编码转换，
    失败返回空 path 而不是抛异常；`resolveFile` 按「UTF-8 原样 → 双跳还原后 ACP → ACP → 还原后 UTF-8」逐个试探。
  - `/lit` 所有回调加 `guard()`：异常不再冒到引擎，改为聊天栏输出 `§c内部错误§r：<原因>`。
  - 其余 `path.string()`（日志、映射表/遮挡表/NBT 错误信息、上传列表）统一换成 UTF-8 输出。
- 方块变化自动恢复：
  - hook BlockSource::（LL 1.9.9 上玩家挖/放都走它；
emoveBlock 恒不命中）；
  - 写成空气时延迟 150 ms 补发该格 ghost（让引擎的 air 更新先落地）；
  - 玩家在投影格放置的方块不被覆盖，挖掉后 ghost 自动回来；
  - /lit 增加 方块 hook：set N 次 → repaint R 次 用于观察；
  - 顺带移除无效的 
emoveBlock hook 与 DispatchQueue（LevelTickEvent 在服务端无 emitter）。
- 分层显示：
  - `ghost/ProjectionSession`：`/lit load` 建立会话（记录投影、原点、剔除后方块、y 范围），切层时**先读世界真实方块发回**再发新层；
  - 命令：`/lit layer <from> [to]`、`/lit unload`；控制台执行时只报告会话状态；
  - 实测：`horse.litematic` 3732 格 → 层 100~114（15 层）。
- 阶段 B 修正与热加载：
  - **修复 `.litematic` 字节序**：它是 Java NBT（大端），此前按小端解析会报「缺少 Regions」；现在 `nbt::parse(..., Endian)` 同时支持两种（`.mcstructure` 用小端）。已用真实投影验证（`horse` 3739 格、`640核刷铁机` 3.2 万格、`天安门` 563 万格）。
  - `/lit load <file> <x> <y> <z>`：控制台可指定原点铺设，发给所有在线玩家；没有玩家时明确回报「已编码 N 包，未发送」。
  - `/lit list`：列出 `schematics/` 下的投影（文件名按 UTF-8 输出）。
  - `/lit reload`：热重载 `ghost_map.json` 并清空状态解析缓存。
  - 内置 HTTP 上传页（Winsock，默认 `127.0.0.1:8686` + token）：拖拽上传到 `schematics/`，只落盘不自动加载；兼容 `filename="x"` 与 `filename=x` 两种 multipart 写法。
  - 中文文件名在控制台不再乱码（输出改用 `u8string`）。
- 工具链：
  - `tools/deploy.ps1`：备份 → 部署插件与 BP/RP → 校验 HologramLib 与 `holo_core.dll` → 检查世界包引用；
  - `tools/verify-stage-a.ps1`：自动启服、执行 `/lit probe`、收集日志、停服，用于阶段 A 无人值守验收。
- 阶段 A 真机验证通过：显示通道 `batch-encoder`、编码器来源 `holo_core.dll`、`packetId 172`、
  4 个状态串全部解析出 runtime id、编码自检产出 16 字节合法包体。
- 版本真源 `src/LitematicaBE/Version.h`（`kVersion` / `kTargetBaseline`）。
- HologramLib 前置封装 `src/LitematicaBE/hologram/HologramFacade.{h,cpp}`：编译期直链 + 运行期能力分级（`batch-encoder` / `per-cell-override` / `none`），启动日志给出可用的显示通道，缺失时降级而不是崩溃。
- 离线 Python 工具链 `tools/`：版本一致性校验、BP/RP 校验、假方块映射清单生成。
- 构建入口 `tools/build.ps1`：探测 MSVC 工具集，缺 v143 ATL 时从同实例其它工具集借用 `atlmfc` 注入构建进程；装好 ATL 后自动走原生路径。
- 行包与资源包 `resources/addon/`：沿用已验证的 `lit:g_*` 假方块定义（1277 个方块）与对应贴图/模型/粒子。
- 打包器接线：`modpacker` 的 `modName` / `modVersion` / `modPlatform` 走 extraconf 显式给出，版本与本仓库的四处真源一起由 `lit-tools version` 校验（原先会因「没有 git tag」写成 `0.0.0`、`${modPlatform}` 原样残留）。
- 架构说明 `docs/architecture.md`，与 `docs/plan-*.md`（历史规划）区分开。

### Changed

- 构建体系：xmake 目标从模板的 `my-mod` 改为 `LitematicaBE`，只保留 server 变体；`lip.toml` 从双变体收敛为单 `server` 变体。
- 目标基线固定为 BDS 1.21.132.x / LeviLamina 1.9.9 / HologramLib 21.132.1。
- 依赖 HologramLib 的方式从 lrca 运行期 `ll.import` 改为编译期头文件 + 导入库直链。
- `manifest.json` 声明对 `HologramLib` 的依赖，避免加载顺序导致的符号解析失败。
- 许可证年份更新为 2024-2026。

### Removed

- 模板遗留的 `src/mod/` 示例代码与 `my-mod` 命名。
- CI 的 client 构建矩阵（投影是服务端能力，客户端不需要产物）。
