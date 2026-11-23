#pragma once

namespace litematicabe::command {

/// 注册 `/lit` 系列命令。
///
///   /lit                    一行状态摘要
///   /lit probe              能力矩阵
///   /lit info <file>        解析 .litematic 并输出统计（不发包）
///   /lit load <file>        以玩家脚下为原点铺投影
///   /lit share [on|off]     投影可见性开关（按玩家，默认全体共享）
///   /lit material           当前选中投影的材料清单 → `<模组目录>/exports/*.xlsx`
///
/// `<file>` 相对路径按 `模组目录/schematics/` 解析。
void registerLitCommand();

} // namespace litematicabe::command
