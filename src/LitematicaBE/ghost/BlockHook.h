#pragma once

#include <cstdint>

namespace litematicabe::ghost {

/// 方块变化的底层 hook。
///
/// LL 1.9.9 的 `EventBus` 在服务端没有 `BlockChangedEvent` / `PlayerDestroyBlockEvent` 的 emitter
/// （`emplaceListener` 返回 null，不抛异常），所以改为直接 hook `BlockSource` 的虚函数：
///   * `removeBlock` —— 挖掉方块（变空气），必须补 ghost；
///   * `setBlock`    —— 写入方块，只有写成空气时才补（玩家放上去的真方块不抢）。
void installBlockChangedHook();
void uninstallBlockChangedHook();

[[nodiscard]] bool          blockHookInstalled();
[[nodiscard]] [[nodiscard]] std::uint64_t blockSetCalls();

} // namespace litematicabe::ghost
