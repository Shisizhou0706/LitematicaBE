#pragma once

#include <cstdint>

class BlockSource;

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

/// 布置 BlockSource 捕获 hook。
///
/// @return **hook 是否挂上**（不代表已经捕获到实例）。世界还没加载 / 完全空闲时
///         `BlockSource` 的函数不会被调用，所以「已布置但未捕获」是正常状态。
bool installBlockSourceCapture();

/// hook 当前是否挂着（诊断用）。
bool blockSourceCaptureInstalled();

/// 捕获到的 BlockSource；还没捕获到时返回 nullptr。
::BlockSource* capturedBlockSource();

/// 卸载捕获 hook（拿到之后就没必要继续挂着）。
void stopBlockSourceCapture();

} // namespace litematicabe::ghost
