# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **面板一点按钮服务端就崩（0xC0000005，`_List_node_emplace_op2`）**：崩溃栈是
  `表单回调 → MenuState::get → unordered_map::_Try_emplace`，`RDI = 0xE400000001`（垃圾指针）。
  根因是线程竞争：表单回调来自**网络线程**，而 `summon()` 走主线程，两边同时在
  `MenuState::mStates` 上插入 → MSVC `unordered_map` 的 list 节点指针被写坏。
  现在：① 表单回调（含四个 CustomForm）一律投递回 `ServerThreadExecutor` 再执行，玩家按名字重查；
  ② `AnimatePacket` hook 里**完全不碰玩家对象**，只把 `runtimeId` 这个 8 字节值带到主线程再解析实体；
  ③ `MenuState` 的插入/删除加 `std::mutex` 兜底；④ 删掉只写不读的死字段 `showing`（那是跨线程写 `bool`）。
- **木剑面板进不了二级菜单、且此后主菜单再也唤不出**：`SimpleForm` 点任意按钮客户端都会**立即关闭**表单再回响应，而 `deliver()` 当时把 `showing` 留在 true 并走 `sendUpdate` —— 已经没有活表单可更新，于是二级菜单打不开；`showing` 从此永远为 true，下一次唤出同样走 `sendUpdate`，主菜单也彻底出不来。现在回调第一件事就是 `showing = false`，并一律 `sendTo`。
- **木剑面板第一版的 `Actor::swing` hook 会让服务端启动后崩溃（0xC0000005）**：`&::Actor::$swing` 能解析到符号、编译期检查也通过，但它指向的实体与 LeviLamina 头文件里的签名不一致，`origin()` 一进去就按错误调用约定读参数（崩在 `detour+0x23`，`mov r8, [r9+0x48]` / `R9=1`）。改用 `easyplace` 已经跑稳的 `Player::useItem` hook 加一个只读观察点（`setUseItemObserver`，与 `setUseOnInterceptor` 同一套做法），手势随之改为「手持木剑右键空气」。顺带确认：`EventBus` 的 `PlayerSwingEvent` 在这台服务器上确实没有 emitter，否则不会走到 hook 分支。
- **打印出来的方块被挖掉后出现「客户端 ghost + 服务端真方块」叠加态**：挖掉触发的 ghost 补发有 150 ms 延迟，而打印机的 100 ms 节拍会在窗口内把该格重新填成真方块；补发逻辑当时不校验状态，于是 ghost 后到、盖住真方块 —— 客户端能穿过去（`lit:g_*` 的 `collision_box` 是 false），服务端却把玩家弹回来。现在补发前先看「该格是否已重新填充」（`isFilled`）并按每个玩家自己的维度确认该格仍是空气，不满足就不发。`requestRepaint` 同时记下破坏时间戳。
- 打印机新增 `respectPlayerBreaks` / `playerBreakCooldownMs`（默认开、3 s）：玩家刚挖掉的格在冷却期内不补回，避免「挖掉 → 秒补回 → 反复触发 ghost 补发」，也避免玩家拆不掉打印区。
- **`.litematic` 按大端 NBT 解析**：Litematica 是 Java 工具，NBT 为大端；此前按小端读导致「缺少 Regions」。解析器现在支持两种字节序（`.mcstructure` 的小端入口已预留），并用真实投影验证（`horse` 3739 格、`640核刷铁机` 32400 格、`天安门` 563 万格）。
- **中文投影名「未找到该文件」**：Windows 上 `std::filesystem::path{std::string}` 按 CP_ACP 解释入参会抛 `std::system_error`；改为按 UTF-8/936/1252 多代码页解释 + Latin-1 双跳还原（BDS 控制台会把 GBK 字节当 Latin-1 再转 UTF-8），并加目录扫描兜底（忽略大小写、可省略扩展名）。
- 上传页对 `.NET HttpClient` 的 `filename=x`（不带引号）解析失败；现在带引号与不带引号两种 multipart 都能识别。
- 路径输出统一走 UTF-8（`path.string()` 在含中文路径时会抛异常）。

### Changed

- **`/lit replace` 改名为 `/lit place`，旧的 `/lit place`（只填空位）删除**：直接放置现在只有一条指令 `/lit place [from] [to]`，语义固定为**覆盖式**（目标位置已有非空气方块也照样写）。
- **`/lit place` 新增使用门槛 `config/place.json`**：`requireAdmin`（需要管理员 / OP 身份，**默认关**）、`requireCreative`（需要创造模式，**默认开**）。判定在执行时（`core::place::checkAccess`），控制台不受这两条限制；改完 `/lit reload` 热重载，`/lit` 摘要会显示当前门槛。
- **`[wireframe] lines built: (x1,y1,z1)-(x2,y2,z2)` 从 INFO 降为 DEBUG**：包围盒线框只要坐标或颜色变了就重建 12 条棱（`Wireframe::build`），原本每次无条件 `info`，于是切层、切共享、重开线框都会在控制台刷一行坐标。现在默认写 `debug`（日志等级为 Info 时静默），需要排查「框画到哪了」时设环境变量 `LITEMATICABE_WIREFRAME_LOG=1` 恢复 INFO；绘制、收起、`kParkDistance` 下移等行为一字未动。README 补上 `/lit box` 与 `LITEMATICABE_NO_WIREFRAME` / `LITEMATICABE_WIREFRAME_LOG` 两个开关的说明。

### Removed

- **`/lit send` / `/lit batch`**：早期验证显示通道用的自检指令（脚下上方发一格假方块 / 发 5×5 平面）。通道自检已由 `/lit probe`（编码器 packetId、状态解析与映射样例）与 `/lit load`（真实投影）覆盖，故连同 `ProbeActions::sendOne` / `sendBatch`、`LitAction::send` / `batch` 一并删除。
- `LitAction::replace` 枚举项及 `/lit place` 的「只填空位」分支。
- **`/lit plan`**：早期用来在不写世界的前提下干跑统计放置结果的试跑指令，连同 `schematic::planProjection` 与 `LitAction::plan` 一并删除；放置统计现在直接看 `/lit place` 的回显（已放 / 跳过 / 解析失败 / 写失败 / 假成功）。
- **同一功能的命令变体清理（每个功能只留一个入口）**：
  - `/lit mat`、`/lit materials` 删除，只留 `/lit material`（`LitAction::mat` / `materials` 一并去掉）；
  - `/lit savestru`、`/lit savelite`、`/lit saveboth` 删除，只留 `/lit save stru|lite|both [名字]`（不带名字仍自动命名；`LitAction::savestru` / `savelite` / `saveboth` 一并去掉）；
  - `/lit saveat` 去掉「省略格式 = `.mcstructure`」的旧重载（`LitSaveAtQuickParams`）：格式与坐标必须一起给，`/lit saveat stru <名字> <x1> <y1> <z1> <x2> <y2> <z2>`。
  - 木剑面板的「导出材料统计」「保存为 …」按钮直接调用 `report::exportMaterials()` / `savetool::saveWithSelection()`，**不经过命令字符串**，命令别名增删不影响面板、`docs/wooden-sword-ui.md` 与 `docs/selection-save.md` 已同步。

### Added

- **投影的两处方块数上限改成配置项（`config/projection.json`）**：`maxLoadBlocks`（`/lit load` 的收集上限，默认 `400000`）、`maxPlaceBlocks`（`/lit place` 的单次写入上限，默认 `200000`）。此前分别是 `ghost/ProjectionSession.cpp` 里的 `kMaxCollectBlocks` 常量与 `PlaceOptions::maxBlocks` 的成员初始值 —— 大型投影放不下、小内存服又想调小，两头都只能改代码重编译。现在：① 新增 `ghost/ProjectionLimits`，照 `core::save::SaveConfig` 的写法（缺字段补默认、回写整份文件、`0` 与负数回落默认，防止负数转 `size_t` 变成天文数字）；② `ProjectionSession::begin()` 每次现取 `maxLoadBlocks`，`PlaceOptions::maxBlocks` 默认改为 `0` = 跟随配置（非 `0` 仍是调用方显式覆盖）；③ `/lit reload` 一起热重载并在回显里带出当前两值；④ 截断与超限的回显直接写明「改 `config/projection.json` 的哪个键」。

- **木剑控制面板（`ui/`）**：木剑现在**只作为投影控制器**。手持木剑 **左键**（潜行与否都算）
  唤出多级表单面板；**右键功能完全不变**（`selection/SwordTool` 的选点 / 潜行右键清空）。
  命令仍然全部保留（面板是补充，不是替代）。
  - 两条触发链路**同时**启用，靠 400 ms 冷却去重，不会重复弹两次：
    1. `ServerNetworkHandler::$handle(AnimatePacket)` —— 客户端左键挥动的服务端锚点；
    2. `easyplace` 的 `Player::useItem` 观察点 —— 「右键空气」兜底。
    为什么必须兜底：LL 的 `HookRegistrar::hook()` 返回 `void`，符号解析失败时
    **既不抛异常也不回报**，「没崩」不等于「挂上了」。
  - 可自证：`triggerStats()` 把「收到的 Swing 包数」与「真正触发面板数」暴露到面板诊断页；
    每个玩家第一次真正触发时还会在日志里留一条 `wooden sword gesture fired (via animate|useitem)`。
    若 Swing 包计数一直是 0，说明该锚点在这台服务端收不到包，此时左键开面板不可用，
    只能用右键空气。
  - 动作推迟 50 ms 到下一拍执行：`$handle(AnimatePacket)` 在网络收包栈上，
    直接在里面发 ghost 包 / 弹表单等于在收包路径里再发包。
  - **面板树严格按 `木剑ui设计.txt` 实现**，12 页：
    - *主菜单*：`LitematicaBE 4.0.0` / `选择一个操作` + 五个入口；
    - *投影管理*：当前控制投影 + 每个会话一行（`#id 名称`、`N 格 · 归属：owner`）+ 加载新投影；
    - *投影操作*：卸载 / 移除 / 选为当前控制 / 导出材料统计 / 重新加载；
    - *新的投影*：列出 `schematics/` 下尚未加载的文件，点击即 `load`；
    - *辅助功能*：轻松放置 / 打印机开关状态 + 当前投影 + 三个动作；
    - *选区保存*：三种格式保存、按坐标保存、清空选区；
    - *保存设置*：填名称（默认 `选区_MMDD_hhmm`，留空自动命名）；
    - *坐标选区*：分别填起始点 / 结束点坐标，再选保存为哪种格式；
    - *外观显示*：分享 / 包围盒开关状态 + 旋转 + 分层显示；
    - *分层显示*：上一层 / 下一层 / 全部层 / 特定层；
    - *显示特定层*：填起止层；
    - *关于投影*：`LitematicaBE 4.0.0` / `重构正式版`。
  - 每次翻页都是一次新的 `sendTo`：Bedrock 的 `SimpleForm` 点击即关闭表单，
    `sendUpdate` 在这套交互里不成立。回调第一件事把 `showing` 清掉 ——
    早期版本留着 true，导致翻页时面板消失、且此后再也唤不出来。
  - 「上次结果」渲染在页顶：`CommandOutput` 收集层（`ui/ActionSink`）把现有动作函数的回显截下来
    排版进面板，聊天栏不再刷屏；动作函数本身一行没改。
  - 「重新加载」只重发该会话的当前显示层给本人（`ProjectionSession::sendCachedTo`），
    不是 `/lit resend` 的全体重发；「导出材料统计」会先把目标会话 `select` 成当前再导出
    （`/lit material` 只认当前选中）。
  - 左键切层已移除：木剑定位为控制器，层控制改走面板按钮，
    避免「打怪 / 挖方块也发 Swing」带来的误切层。
- `ProjectionManager::forEachSession(fn)`：面板/诊断按 id 升序遍历会话，不必解析 `list()` 的文本。
- `easyplace::setUseItemObserver()` / `hasUseItemObserver()`：`Player::useItem` 的只读观察点
  （`origin()` 之后执行，不影响物品消耗与放置会计）。
- **材料清单导出（`/lit material`，别名 `mat` / `materials`）**：把**当前选中**投影的方块按材料聚合，
  导出 `.xlsx` 到 `<模组目录>/exports/材料清单_<投影名>.xlsx`（同名覆盖），能力从旧 LSE 实现的
  `src/utils/MaterialCounter.js` 迁移。
  - 工作簿两张表：`材料清单`（序号 / 方块名 / 变体 / 数量 / 占比 / 可放置 / 状态串样例；首行冻结 +
    自动筛选 + 末行合计）与 `投影信息`（投影名、文件、会话、方块总数、材料种类、状态数、层范围、
    当前显示层、旋转、导出时间、插件版本、统计口径）。
  - 口径：统计**整份投影**（`mBlocks`），与「当前显示哪一层」无关；空气在解析阶段已跳过；
    按「名字 + 材质变体」合并 —— `color` / `wood_type` / `stone_type` / `sand_type` / `dirt_type`
    分开成不同材料，朝向与开关不拆行。旧实现按硬编码 `data` 值猜颜色/木种（并把它当「中文名」输出），
    这里直接读状态键，`.litematic`（Java 名）与 `.mcstructure`（基岩名）两种口径都成立。
  - `可放置` 列取自会话加载时的 `mPaletteBlocks`：状态串在当前映射表下解析不出方块时标「否」，
    避免玩家照单备料却放不出来；`投影信息` 里同时给出不可放置的格数。
  - 新增 `core/report/XlsxWriter`：极简 OOXML 工作簿写出（inlineStr 单元格 + styles 表 + 列宽 +
    冻结窗格 + 自动筛选 + docProps），zip 用 miniz 在**内存**里打包后按宽字符路径落盘
    —— `mz_zip_writer_init_file` 内部走 `fopen`，模组目录或投影名带中文时直接失败。
  - 新增 `core/report/MaterialCounter`：纯数据的「调色板 + 每格下标 → 材料聚合」，
    `ProjectionSession::materials()` 只负责把 `mBlocks` 的下标喂进去，聚合逻辑可脱离游戏回归。
  - 离线回归 `tools/xlsx-check/`：C++ 侧断言聚合口径与变体解析（颜色分开、朝向合并、越界下标不崩、
    带引号的状态键值），再用 openpyxl 真打开生成的文件，逐格校验 zip 条目、`[Content_Types].xml`、
    加粗表头、`#,##0` / `0.00%` 数字格式、冻结窗格、列宽与 XML 特殊字符往返。
- **选区与投影保存（木剑选点 + `/lit save`）**：把世界里的区域导出成投影文件，能力从旧 LSE 实现
  （`SelectionTool` + `SchematicSaver`）迁移。
  - 手势：主手木剑右键方块两次 = 两点定选区（500 ms 冷却、同维度校验、完成后报范围/尺寸/体积），
    潜行 + 右键清空；选区按玩家记录，下线自动清理；选点线框走 HologramLib 形状通道
    （`createBox`，六参 = 两个角点，取 `min` 与 `max+1` 正好框住含端点的整格；可配置关闭）。
    与轻松放置共用同一个 `Item::useOn` hook —— 通过新增的 `easyplace::setUseOnInterceptor` 回调在
    `origin()` 之前拦截，不重复挂同一个函数。
  - 导出：`.mcstructure`（默认，小端 NBT、两层 int32 网格、调色板 0 号 air、`structure_world_origin`
    = 选区最小角、方块实体写 `block_position_data`）与 `.litematic`（大端 NBT + gzip、位打包、
    `Region.Position = 0`、Metadata 全套字段）；`both` 只遍历一次世界。
  - 写入侧新增 `core/nbt/NbtWriter`（大端/小端序列化 + gzip 压缩，deflate 走 miniz）、
    `core/schematic/LitematicWriter`、`core/schematic/McstructureWriter`、
    `core/schematic/PaletteBits`（读写共用的位宽规则，避免 257~512 项时的自写自读错位）、
    `core/save/RegionScan`（选区扫描：主动拉区块、按 `BlockType`+`mData` 缓存状态提取、
    调色板化满网格）。
  - 状态提取：`BlockState::toNBT` 让引擎自己给出带类型的值（Byte/Int/String 保真），
    拿不到时退回整型并把该方块计入 `stateFallback` 统计；`/lit savediag` 可逐格查看。
  - 基岩 → Java：`mapping::BedrockToJava`（数据表 `resources/data/export_map.json` +
    楼梯/台阶/门/活板门/按钮/拉杆/铁轨/活塞/红石等族规则；活塞与按钮的水平四向与通用表相反的差异按
    旧项目实测口径处理），表由 `tools/lit_tools/exportmap.py` 生成。
  - 命令：`/lit sel`、`/lit sel clear`、`/lit save stru|lite|both <名字>`（**格式必须显式给**，
    配置里没有「默认格式」这一项）、`/lit saveat [stru|lite|both] <名字> <x1> <y1> <z1> <x2> <y2> <z2>`
    （控制台可用，默认带回读自检，省略格式按 `stru`）、`/lit savediag`、`/lit saveinfo`。
  - 配置 `config/save.json`：两份体积上限、是否含方块实体、
    是否写液体层、`blockVersion` / `dataVersion`、**同名不覆盖（默认报错）**、`requireOp`（默认不要求）、
    选区线框开关与样式（`box` / `lines`）。
  - 回读自检：写出的文件用本项目解析器读回，逐格比 `mSerializationIdHash`，输出一致率与首条不一致样例。
  - 回归与工具：`tools/write-check/`（写入侧离线回归：合成区域 → 写两种格式 → 读回逐格比对，
    覆盖位打包跨 long、索引顺序、大小端、gzip、大调色板位宽修正与 1×1 边界，当前 696 项检查全通过）、
    `tools/lit_tools/exportmap.py`、环境变量 `LITEMATICABE_SELF_TEST` 无人值守自检
    （控制台 stdin 被 LeviLamina 接管时用，结果写 `data/selftest.log`）。
  - 文档：[docs/selection-save.md](docs/selection-save.md)（手势、命令、配置、格式口径、已知限制）。
- **投影可见性开关（`/lit share`）**：每个玩家可自行选择「自己放置的投影是否别人也看得到」，**默认全体共享**（旧行为）。`/lit share` 不带参数是切换，`/lit share on|off` 是显式设置。判定集中在发包点（`ProjectionSession::visibleTo`），显示层/切层/进服重生传送心跳补发/挖掉补格全部走同一道门；切成私有会**当场收回**旁人屏幕上的 ghost（读真实方块发回）并丢掉他们排队未发的 ghost 包（`DispatchQueue::dropPlayer`），切回共享立刻补发全体，全程不需要重新 `load`。偏好按玩家名存 `config/share.json` 的 `private` 名单（默认共享，文件里通常是空数组）；控制台 `load` 建的会话没有主人，恒为共享；`/lit projects` 每行标出「共享 / 私有」。私有会话不画包围盒线框（hologramlib 的 DebugShape 是全局形状，没有按玩家隔离的接口，画出来等于泄露位置）。
- **投影打印机（`/lit printer`）**：以玩家为球心、半径 = 手长（`config/printer.json` 的 `range`，默认 5 格）按节拍自动把投影方块写进世界，并从背包扣料（生存扣 1 格 / 创造默认豁免）。只作用于当前选中会话的显示层；写方块复用 `/lit place` 的 `setBlockAndRetainCompatibleBlockActor` + 回读校验 + 区块补载；写成功才扣料、一轮一次背包同步；放完即 `markFilled` 让 ghost 层让位。不挂 hook，可与轻松放置同时开启。阶段一不含特殊物品表 / 依附递归 / 多格方块 / 方块实体数据。
- `core/place/PlaceGuard`：轻松放置与投影打印机共用的「程序化放置」闸门（方块实体 / 多格 / 流体判定、方块↔物品匹配、可替换方块表），判定按 `BlockType` 指针缓存。
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
- 阶段 H（真实放置）：
  - 新命令 /lit place（只填空位）/ /lit replace（覆盖）/ /lit plan（干跑统计）/ /lit diag（环境诊断），都支持 <from> <to> 限定层；
  - ProjectionSession 增加原始状态串调色板（Entry.paletteId → mPalette），.mcstructure 走原样、.litematic 走名称映射；
  - 新增 mapping::PlaceResolve：候选链（原样 → java_bedrock.json → 规则变形 → 候选池近似 → 默认状态）+ Java 属性别名多套尝试 + 三级精度（原样/改名/仅默认状态），按状态串缓存；
  - 新增 mapping::BlockStateText（状态串解析/构造，BlockStateResolve 与 PlaceResolve 共用）；
  - 新工具 lit-tools placemap：从 VanillaBlockTypeIds.h 抽 1198 个 Bedrock 候选名 + 生成 48 条 Java→Bedrock 差异映射；
  - 放置按 y→x→z 自下而上，setBlockAndRetainCompatibleBlockActor 写入，单次上限 20 万格，收尾抽样回读校验；
  - 环境坑：本服务端 Level::getDimension() 恒返回空 weak_ptr，mRegions/mDimensions 读不出来（LL 头的结构布局与 BDS 不符），改用 Actor::getDimensionBlockSource()（MCFOLD）+ 运行时 hook 捕获 BlockSource；控制台放置因此要求至少一名玩家在线。
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
