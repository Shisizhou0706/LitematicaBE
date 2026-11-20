# tools — 离线工具链

纯 Python 标准库，不参与运行时、不进入发布包。目的只有一个：把「版本 / 资源 / 映射」三处最容易漂移的东西在离线阶段拦住。

## 用法

```bash
cd tools
python -m lit_tools version     # Version.h / lip.toml / CHANGELOG 版本一致
python -m lit_tools addon       # BP/RP 版本、identifier 唯一性、贴图引用
python -m lit_tools mapping     # 生成 resources/data/generated/ghost_blocks.json
```

（也可以 `pip install -e .` 后直接用 `lit-tools <command>`。需要 Python ≥ 3.10，无第三方依赖。）

## 命令说明

| 命令 | 检查什么 | 失败意味着 |
|---|---|---|
| `version` | `src/LitematicaBE/Version.h` 的 `kVersion`、`xmake.lua` 的 `modVersion`、`lip.toml` 的 `version`、`CHANGELOG.md` 最新条目标题 | 发版会推出错误的版本号，`lip` 侧、打包出来的 `manifest.json` 与二进制三边不一致 |
| `addon` | BP/RP manifest 版本一致；`blocks/*.json` 的 identifier 唯一且在 `lit:` 下；`material_instances` 引用的贴图都存在于 RP 的 `terrain_texture.json` | 运行期假方块变紫黑格或串块，日志里看不出来 |
| `mapping` | 从 BP 枚举识别符与状态定义 | 映射层的输入缺失 |

`mapping` 的产物在 `resources/data/generated/` 下（已 gitignore）。需要固定版本时由发布流程打包，而不是提交进仓库。

## build.ps1

`tools/build.ps1` 是构建入口，做两件事：

1. **ATL 兜底**：读 vswhere 找到 VS 实例与最新 MSVC 工具集；如果该工具集没有 `atlmfc`（即没装 v143 的 ATL），就从同一实例里其它工具集借用 `atlmfc`，通过 `CL` / `LIB` / `_LINK_` 注入给本次构建进程。只影响当前进程，不改系统。
2. **调用 xmake**：默认 `f -y` + `xmake -y`，`-Reconfigure` 加 `-c` 重新配置，`-Incremental` 只跑 `xmake`。

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Mode debug -Reconfigure
```

> 兜底只是让「只有旧工具集 ATL」的机器也能构建。正解仍然是装
> 「适用于最新 v143 生成工具的 C++ ATL (x86 和 x64)」（组件 ID `Microsoft.VisualStudio.Component.VC.ATL`），
> 装好后脚本会显示 `ATL : 当前工具集自带`，不再注入。

## legacy/

`legacy/` 是旧仓库（3.1.1，LSE Node.js）的生成脚本与构建脚本快照，**不参与任何构建**，只为算法参考：

- `legacy/node/` — `flattening_map.json`、`gen_*.js`、`build-release.mjs` 等
- `legacy/scripts/` — `work/` 下一次性使用的 Python / JS / PowerShell 脚本

新实现不假设这些脚本可运行；需要复用其中的几何/映射数据时，先把它变成 `lit_tools` 里的一个有校验的生成步骤。
