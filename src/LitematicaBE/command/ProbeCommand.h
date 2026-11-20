#pragma once

namespace litematicabe::command {

/// 注册 `/lit <action>`。
///
/// 阶段 A 只做诊断与最小发包自检：
///   /lit            —— 一行状态摘要
///   /lit probe      —— 能力矩阵（前置模块、C ABI 符号、状态解析、编码器、发包计数）
///   /lit send       —— 在你脚下上方 2 格发一格假方块（minecraft:stone）
///   /lit batch      —— 以你脚下为原点铺 5×5 的假方块平面（验证子区块分组与多包顺序）
void registerLitCommand();

} // namespace litematicabe::command
