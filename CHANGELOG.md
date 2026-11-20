# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [4.0.0] - 2026-11-21

从 LSE Node.js 插件（3.1.1）迁移为 LeviLamina 原生模组。这一版只包含工程骨架，投影功能尚未迁移。

### Added

- 模组入口 `src/LitematicaBE/LitematicaBE.{h,cpp}`，实现 `load` / `enable` / `disable`，加载时准备 `data/` 与 `config/` 目录。
- 阶段 A（显示通道骨架）：
  - `mapping/BlockStateResolve`：状态串 → `Block::mNetworkId`（BDS 注册表 + 进程内缓存），与库的解析口径一致；
  - `ghost/BatchEncoder`：16³ 子区块分组、单包 ≤1024 条、调用 `holo_core.dll` 的 C ABI 编码；
  - `ghost/PacketSender`：varint 包头 + `NetworkPeer::sendPacket(Reliable, Compressible)`；
  - `command/`：`/lit`、`/lit probe`、`/lit send`、`/lit batch` 四个入口用于诊断与最小发包自检。
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
