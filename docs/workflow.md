# 构建与部署流程

本仓库的日常循环只有一条命令。下面按「日常 / 三类改动 / 特殊情况 / 排错」组织。

## 一句话版本

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\update.ps1 -ServerRoot 'C:\Users\25491\Desktop\MCBEserver1.21.132'
```

它依次做四件事：

1. 版本自检（`Version.h` / `xmake.lua` / `lip.toml` / `CHANGELOG.md` 四处一致）；
2. `xmake -y` 构建（需要时先 `xmake f`）；
3. `tools\deploy.ps1` 备份并部署插件与 BP/RP；
4. 打印产物大小与实际构建时间。

**前提：服务端必须是停着的**——已加载的 DLL 无法覆盖。

### 参数

| 参数 | 作用 |
|---|---|
| `-Reconfigure` | 强制 `xmake f` 重新配置（改了 `xmake.lua` 的选项/依赖/链接时必须加） |
| `-SkipBuild` | 不构建，只部署现有的 `bin/LitematicaBE` |
| `-SkipDeploy` | 只构建，不碰服务端 |
| `-Verify` | 部署后自动启服 → 跑 `/lit probe` → 停服（无人值守验收） |
| `-Commands 'lit probe','lit send'` | 自定义验证时喂给控制台的命令 |
| `-NoBackup` | 跳过 `.deploy-backup/` 备份 |

例：改完功能直接构建 + 部署 + 验收

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\update.ps1 `
  -ServerRoot 'C:\Users\25491\Desktop\MCBEserver1.21.132' -Verify
```

## 部署脚本的落点

| 源 | 目标 | 覆盖方式 |
|---|---|---|
| `bin/LitematicaBE/*` | `plugins/LitematicaBE/` | **就地覆盖**，保留 `config/` 与 `data/` |
| `resources/addon/LitematicaBE_BP` | `behavior_packs/LitematicaBE_BP/` | **清空重写**（防止旧 LSE 版残留） |
| `resources/addon/LitematicaBE_RP` | `resource_packs/LitematicaBE_RP/` | **清空重写** |
| 部署前快照 | `.deploy-backup/<时间戳>/{plugin,BP,RP}/` | 可整份拷回回滚 |

`holo_core.dll` 由 `xmake.lua` 的 `after_build` 自动复制到 `bin/LitematicaBE/`，随插件一起发，不需要单独处理。

部署脚本还会校验：HologramLib 的版本/API/abiStamp、`holo_core.dll` 是否到位、**世界的 `world_behavior_packs.json` / `world_resource_packs.json` 是否引用了本 BP/RP**。

## 三类改动，处理方式不同

### 1. 只改 C++ 代码（最常见）

```powershell
tools\update.ps1 -ServerRoot '...'
```

不需要别的动作。想确认新逻辑生效就加 `-Verify`，看 `/lit probe` 的输出。

### 2. 改了 BP/RP（方块定义、纹理、模型、粒子）

**必须 bump 版本号**，否则客户端会继续用缓存里的同版本资源包，看不到新内容：

1. `resources/addon/LitematicaBE_BP/manifest.json` → `header.version` 与 `modules[].version`
2. `resources/addon/LitematicaBE_RP/manifest.json` → 同上
3. 世界引用里的版本也要同步：
   - `worlds/<世界名>/world_behavior_packs.json`
   - `worlds/<世界名>/world_resource_packs.json`
4. 校验：
   ```powershell
   cd tools
   python -m lit_tools addon
   ```
   会检查 BP/RP 版本一致、方块 identifier 唯一且带 `lit:` 前缀、`material_instances` 引用的纹理确实存在于 RP 的 `terrain_texture.json`。
5. 再跑 `update.ps1`。

### 3. 发版（改模组版本号）

四处必须同步，`python -m lit_tools version` 会校验：

| 位置 | 字段 |
|---|---|
| `src/LitematicaBE/Version.h` | `kVersion` |
| `xmake.lua` | `modVersion` |
| `lip.toml` | `version` |
| `CHANGELOG.md` | 顶部 `## [x.y.z]` |

```powershell
cd tools
python -m lit_tools version     # 输出四处版本并断言一致
python -m lit_tools mapping     # 重新生成 resources/data/generated/ghost_blocks.json
```

## 特殊情况

### 改了 `xmake.lua`（选项、依赖、链接库）

```powershell
tools\update.ps1 -ServerRoot '...' -Reconfigure
```

### 清理重建 / 换工具链

```powershell
xmake f -c -y -p windows -a x64 -m release
xmake -y
```

如果这一步报 `fatal error C1083: cannot open include file 'atlbase.h'`，说明 xmake 正在**从源码重编 levilamina 包**，而当前 MSVC 工具集没装 ATL：

- 正解（一次性）：VS Installer → 修改 → 单个组件 → 勾选「适用于最新 v143 生成工具的 C++ ATL」
- 临时绕过（只在重编包时需要）：

  ```powershell
  $atl = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.16.27023\atlmfc'
  $env:CL  = "$env:CL /I""$atl\include"""
  $env:LIB = "$atl\lib\x64;$env:LIB"
  ```

  日常增量构建**不需要**这些变量——包已经装好时 xmake 不会重编它。

### 前置 HologramLib 升级

1. 用新版整体替换 repo 根目录 `hologramlib/`：`include/hologramlib/*.h`、`lib/*.lib`、`holo_core.dll`（该目录已 gitignore，不入库）；
2. `-Reconfigure` 重新编译——ABI 变了必须重编，HologramLib 用 `abiStamp` 校验；
3. 服务端 `plugins/HologramLib/` 也要换成同版本，否则运行期探测失败。

### 版本号卫生

`modpacker` 在没有 git tag 时会把 manifest 里的版本写成 `0.0.0`，所以 `xmake.lua` 的 extraconf 里固定了 `modVersion`。如果发现 `bin/LitematicaBE/manifest.json` 里版本不对，检查 `xmake.lua` 的 `add_extraconf` 段。

### CI

- `.github/workflows/build.yml`：push 时构建，产物 `LitematicaBE-<sha>-server-windows-x64`；
- `.github/workflows/release.yml`：打 tag 时发布 `LitematicaBE-server-windows-x64.zip`；
- 两者都是 server-only 单变体，`holo_core.dll` 已由 `after_build` 打进产物。

## 回滚

```powershell
# 停服后，把某次备份整份拷回
Copy-Item '.deploy-backup\20261121-040601\*' 'C:\...\plugins\' -Recurse
```

备份目录里分 `plugin/`、`BP/`、`RP/` 三份，可以只回滚其中一份。

## 排错对照

| 现象 | 原因 | 处理 |
|---|---|---|
| `Stop the server before deploying` | 服务端在跑，DLL 被占用 | 停服后重跑 |
| `Missing build artifact: holo_core.dll` | `hologramlib/holo_core.dll` 缺失 | 补齐 vendor 目录后重编 |
| 启动日志 `display path none` | HologramLib 没被加载 | 检查 `plugins/HologramLib/` |
| `display path per-cell-override` | 前置在，但没解析到批量编码器 | `/lit probe` 看「编码器：来源」；补 `holo_core.dll` |
| `/lit send` 回显「编码失败」 | 同上 | 同上 |
| `/lit send` 回显「已发 1 包」但客户端没变化 | 包体/包头/子区块原点问题 | 用 `/lit probe` 的「编码自检」头部字节对拍 |
| 中文日志变乱码 | GBK 控制台 | 日志已英文化；命令回显请在游戏内看 |
| PS 报 `Unexpected token` | `.ps1` 含非 ASCII 且无 BOM，PS 5.1 按 GBK 读 | 脚本保持纯 ASCII（现有三个脚本都是） |
| `lit:g_*` 解析失败 | BP 未加载或世界未引用 | `deploy.ps1` 会打印世界引用检查结果 |
| 改了纹理客户端看不到 | BP/RP 版本号没 bump，客户端用缓存 | 见「三类改动 → 2」 |

## 相关文件

- [tools/update.ps1](../tools/update.ps1) — 构建 + 部署（+ 可选验证）
- [tools/deploy.ps1](../tools/deploy.ps1) — 备份 + 部署 + 前置校验
- [tools/verify-stage-a.ps1](../tools/verify-stage-a.ps1) — 启服 → 跑命令 → 停服 → 报告
- [tools/lit_tools/](../tools/lit_tools/) — 版本/资源包/映射的离线校验
- [docs/stage-a-acceptance.md](stage-a-acceptance.md) — 阶段 A 验收清单
