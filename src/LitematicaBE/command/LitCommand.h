#pragma once

namespace litematicabe::command {

/// 注册 `/lit` 系列命令。
///
///   /lit                    一行状态摘要
///   /lit probe              能力矩阵
///   /lit send               发一格假方块（自检）
///   /lit batch              发 5×5 平面（自检）
///   /lit info <file>        解析 .litematic 并输出统计（不发包）
///   /lit load <file>        以玩家脚下为原点铺投影
///
/// `<file>` 相对路径按 `模组目录/schematics/` 解析。
void registerLitCommand();

} // namespace litematicabe::command
