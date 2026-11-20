# 阶段 A 验收清单（显示通道）

目标：证明「状态解析 → 批量编码 → 发包 → 客户端显示」这条链路成立，且**服务端世界零改动**。

## 自动化验证结果（已在真机跑通）

`tools/verify-stage-a.ps1` 在 `C:\Users\25491\Desktop\MCBEserver1.21.132`
（BDS 1.21.132 + LeviLamina 1.9.9 + HologramLib 21.132.1）上启动服务端、执行命令并停服，结果：

| 项 | 实测结果 |
|---|---|
| 插件加载 | `LitematicaBE 4.0.0 loading (target baseline: BDS 1.21.132.x / LeviLamina 1.9.x)` → `LitematicaBE loaded` |
| 显示通道 | `LitematicaBE enabled, display path batch-encoder: HologramLib: batch encoder ready` |
| 命令注册 | `command /lit registered (probe / send / batch)` |
| 编码器来源 | `holo_core.dll`，`完整 是`，`packetId 172` |
| 状态解析 | `stone → 2150698529`、`air → 3690217760`、`lit:g_acacia_door[lit:dir=2] → 2569869627`、`oak_stairs[upside_down_bit=false,weirdo_direction=2] → 3240922889` |
| 编码自检 | `1 包 / 16 字节 / 头部 00 00 00 01 00 00 00 A1 9C C4 81 08` |
| BP 注册 | `Pack Stack - [00] LitematicaBE Ghost Blocks (id: 70c71811-…, version: 1.1.89)` |
| 停服 | `Quit correctly`，无崩溃、无异常日志 |

包体自检解读：`00 00 00` = 子区块 (0,0,0)，`01` = 层数，`00 00 00` = 方块相对坐标，
`A1 9C C4 81 08` = runtime id `2150698529` 的 varint 编码，其后是 flags 与 sync 字段。
与 `UpdateSubchunkBlocks` 的结构一致，说明 C ABI 编码器工作在正确口径上。

**仍需人工验证**（必须有客户端在线）：`/lit send`、`/lit batch` 的客户端可见性，以及世界零污染。

一键复跑：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify-stage-a.ps1 -ServerRoot 'C:\Users\25491\Desktop\MCBEserver1.21.132'
```

## 0. 前提

| 项 | 要求 |
|---|---|
| 服务端 | BDS 1.21.132.x + LeviLamina 1.9.9 |
| 前置 | `plugins/HologramLib/HologramLib.dll`（HologramLib 21.132.1） |
| 本插件 | `plugins/LitematicaBE/{LitematicaBE.dll, manifest.json, holo_core.dll}` |
| 数据包 | `behavior_packs/LitematicaBE_BP`、`resource_packs/LitematicaBE_RP` |
| 客户端 | 与 1.21.132 匹配，进服前确认已下载服务端资源包 |

部署：

```powershell
xmake f -y -p windows -a x64 -m release
xmake -y
powershell -NoProfile -ExecutionPolicy Bypass -File tools\deploy.ps1 -ServerRoot 'D:\你的BDS目录'
```

`tools/deploy.ps1` 会备份旧的 `plugins/LitematicaBE`、BP、RP 到 `.deploy-backup/<时间戳>/`。

## 1. 启动日志检查点

服务端启动后在 `logs/` 或控制台确认三行（日志一律英文，避免 GBK 控制台乱码）：

```
[LitematicaBE] LitematicaBE 4.0.0 loading (target baseline: BDS 1.21.132.x / LeviLamina 1.9.x)
[LitematicaBE] LitematicaBE enabled, display path batch-encoder: HologramLib: batch encoder ready
[LitematicaBE] command /lit registered (probe / send / batch)
```

| 实际输出 | 含义 | 处理 |
|---|---|---|
| `display path none` | `HologramLib.dll` 不在进程内 | 装前置；确认 `plugins/HologramLib/manifest.json` 的 `type` 为 `native` |
| `display path per-cell-override` | 前置在，但找不到批量编码器 | 确认 `plugins/LitematicaBE/holo_core.dll` 存在（部署脚本会拷） |
| `HologramLib: not compiled in` | 本插件编译时缺 `hologramlib/` | 放好 `hologramlib/` 后重编 |

## 2. `/lit probe`（不进游戏也能看部分信息，但需要玩家身份的命令请在游戏内执行）

预期：

```
=== LitematicaBE 能力探针 ===
版本：4.0.0 / 基线 BDS 1.21.132.x / LeviLamina 1.9.x
构建：HologramLib=编入 显示通道=batch-encoder
前置模块：已加载
编码器：来源 holo_core.dll / 完整 是 / packetId 172
minecraft:stone → <数字>
minecraft:air → <数字>
lit:g_acacia_door[lit:dir=2] → <数字>
minecraft:oak_stairs[upside_down_bit=false,weirdo_direction=2] → <数字>
编码器 packetId=172（0 = 批量通道不可用）
```

判读：

| 现象 | 含义 |
|---|---|
| `packetId 172`（898 协议下的 UpdateSubchunkBlocks） | 编码器可用 |
| `packetId 0` | 编码器未解析到，批量不可用 |
| `lit:g_acacia_door` 解析成「失败」 | BP 未加载或未重启；确认行为包已装并在世界里启用 |
| 解析结果每次不同 | 不应发生；语义错误，需要报出来 |

## 3. `/lit send`

站在任意空地执行。预期：

- 命令回显：`已发 1 包 / 1 格 → (x, y+2, z)`
- 客户端：**自己脚下上方 2 格出现一块石头**（只有你看得见，别人看不见）

## 4. `/lit batch`

预期：

- 命令回显：`已发 N 包（编码 N 包 / 25 格）→ 以 (x, y+2, z) 为中心的 5×5 平面`
- 客户端：脚上方 2 格出现 5×5 交错石/玻璃平面
- 25 格都在同一个子区块内时应编成 **1 个包**

## 5. 世界零污染验证（关键）

在 `/lit send` 之后，用原版命令确认世界没有变：

```
/execute if block <x> <y+2> <z> minecraft:stone
```

预期：**失败**（"测试失败" / 条件不成立）。如果成功，说明写到了世界，属于严重缺陷。

辅助检查：

- 退出重进服务器，假方块消失（客户端缓存不落盘）。
- `/lit`（无参）查看 `发包：成功 …` 计数增长。

## 6. 失败排查

| 现象 | 可能原因 | 查什么 |
|---|---|---|
| 命令没注册 | `enable()` 抛异常或命令模块未编译进去 | 启动日志是否有加载失败 |
| 回显「编码失败：批量通道不可用」 | `holo_core.dll` 缺失 / 架构不符 | `/lit probe` 的「编码器：来源」 |
| 回显「已发 N 包」但客户端无变化 | 包体或包头不对（字段顺序 / 子区块原点 / packetId） | 与服务端自己的 `UpdateSubchunkBlocks` 抓包对拍 |
| `noPeer` 递增 | 玩家网络标识拿不到（刚断线 / 跨维度瞬间） | 稍后重试 |
| 显示成紫黑方块 | BP/RP 未装或 `lit:*` 未注册 | 行为包是否在世界内启用 |
| 显示位置整体偏移 | 子区块原点语义与库不一致 | 用 1 格单点验证（`/lit send`）先排除分组问题 |

## 7. 通过标准

- [ ] 启动日志显示 `batch-encoder`
- [ ] `/lit probe` 的 4 个样例全部解析出数字，`packetId != 0`
- [ ] `/lit send` 客户端看到 1 格石头，世界 `if block` 判定失败
- [ ] `/lit batch` 客户端看到 5×5 平面，25 格编成 1 包
- [ ] 重进服务器后假方块消失
- [ ] 全程服务端无报错、无崩溃

四项全过即可进入阶段 B（投影解析）。
