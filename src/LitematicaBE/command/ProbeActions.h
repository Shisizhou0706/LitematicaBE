#pragma once

#include <string>

class CommandOutput;
class Player;

namespace litematicabe::command::probe {

/// 一行状态摘要（`/lit`）。
void printState(::CommandOutput& output);

/// 完整能力矩阵（`/lit probe`）。
void printProbe(::CommandOutput& output);

/// 诊断：打印 Level / 维度 / region 能不能访问（找 BlockSource 用）。
void diagnose(::CommandOutput& output);

/// 在玩家脚下上方 2 格发一格假方块（`/lit send [block]`）。
/// @param block 方块状态串；默认 `minecraft:stone`，可传 `lit:g_stone[lit:dir=0]` 测 ghost 方块。
void sendOne(::CommandOutput& output, ::Player& player, std::string const& block = "minecraft:stone");

/// 以玩家脚下为原点铺 5×5 平面（`/lit batch`）。
void sendBatch(::CommandOutput& output, ::Player& player);

} // namespace litematicabe::command::probe
