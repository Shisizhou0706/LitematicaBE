#include "LitematicaBE/ghost/BlockHook.h"

#include "LitematicaBE/ghost/ProjectionManager.h"

#include "ll/api/memory/Hook.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include <atomic>
#include <cstdint>

namespace litematicabe::ghost {

namespace {

std::atomic<std::uint64_t> gSetCalls{0};

// 写入方块：BlockSource::$setBlock(pos, block, flags, syncMsg, ctx)
//
// 实测（LL 1.9.9 / BDS 1.21.132）玩家「挖」和「放」都走这里：
//   * 挖掉 → block 是 air，此时把那格的 ghost 补回去；
//   * 放方块 → block 不是 air，不覆盖（玩家盖上的真方块优先）。
// BlockSource::removeBlock 这条路径玩家破坏时不走（计数恒为 0），因此不挂。
LL_TYPE_INSTANCE_HOOK(
    BlockSetHook,
    ll::memory::HookPriority::Normal,
    ::BlockSource,
    &::BlockSource::$setBlock,
    bool,
    ::BlockPos const&              pos,
    ::Block const&                 block,
    int                            updateFlags,
    ::ActorBlockSyncMessage const* syncMsg,
    ::BlockChangeContext const&    changeSourceContext
) {
    ++gSetCalls;
    bool const result = origin(pos, block, updateFlags, syncMsg, changeSourceContext);
    if (result && block.isAir()) {
        // 多投影：分发给所有包含该坐标的会话（可能同时命中多份）。
        ProjectionManager::getInstance().requestRepaintAt(pos.x, pos.y, pos.z);
    }
    return result;
}

ll::memory::HookRegistrar<BlockSetHook> gSetHook;
bool                                    gInstalled{false};

} // namespace

void installBlockChangedHook() {
    if (gInstalled) {
        return;
    }
    gSetHook.hook();
    gInstalled = true;
}

void uninstallBlockChangedHook() {
    if (!gInstalled) {
        return;
    }
    gSetHook.unhook();
    gInstalled = false;
}

bool blockHookInstalled() { return gInstalled; }

std::uint64_t blockSetCalls() { return gSetCalls.load(); }

} // namespace litematicabe::ghost
