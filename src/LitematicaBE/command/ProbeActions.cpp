#include "LitematicaBE/command/ProbeActions.h"

#include "LitematicaBE/Version.h"
#include "LitematicaBE/ghost/BatchEncoder.h"
#include "LitematicaBE/ghost/BlockHook.h"
#include "LitematicaBE/ghost/ProjectionManager.h"
#include "LitematicaBE/ghost/PacketSender.h"
#include "LitematicaBE/hologram/HologramFacade.h"
#include "LitematicaBE/mapping/BlockStateResolve.h"
#include "LitematicaBE/mapping/GhostMap.h"

#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace litematicabe::command::probe {

namespace {

namespace ghost = litematicabe::ghost;

std::string moduleState(wchar_t const* module) {
    return ::GetModuleHandleW(module) != nullptr ? "已加载" : "未加载";
}

std::size_t flush(::Player& player, std::vector<ghost::EncodedPacket>& packets) {
    auto&       sender = ghost::PacketSender::getInstance();
    std::size_t ok     = 0;
    for (auto& packet : packets) {
        if (sender.send(player, packet.packetId, packet.body)) {
            ++ok;
        }
    }
    return ok;
}

} // namespace

void printState(::CommandOutput& output) {
    auto&      facade     = hologram::HologramFacade::getInstance();
    auto const path       = facade.probe();
    auto&      states     = mapping::BlockStateResolve::getInstance();
    auto&      encoder    = ghost::BatchEncoder::getInstance();
    auto&      map        = mapping::GhostMap::getInstance();
    auto const stateStats = states.stats();
    auto const& sendStats = ghost::PacketSender::getInstance().stats();
    auto const mapStats   = map.stats();

    output.success("§b[LitematicaBE]§r v{}（{}）", kVersion, kTargetBaseline);
    output.success("显示通道：{} / {}", hologram::toString(path), facade.describe());
    output.success(
        "状态解析：命中 {} 未命中 {} 失败 {} 缓存 {}",
        stateStats.hits,
        stateStats.misses,
        stateStats.failures,
        stateStats.cached
    );
    output.success(
        "映射表：{} 块 / 命中 {} 未命中 {} 缓存 {}",
        mapStats.blocks,
        mapStats.hits,
        mapStats.misses,
        mapStats.cacheSize
    );
    output.success(
        "编码器：packetId={} 已编码 {} 包 / 失败 {}",
        encoder.packetId(),
        encoder.stats().packets,
        encoder.stats().failed
    );
    output.success(
        "发包：成功 {} 字节 {} noPeer {} noNetwork {}",
        sendStats.sent,
        sendStats.bytes,
        sendStats.noPeer,
        sendStats.noNetwork
    );

    // 队列当前不参与发送（load/clear 都是同步发），只报「未启用」，避免 tick=0 造成误判。
    auto&       manager   = ghost::ProjectionManager::getInstance();
    auto const  mgrStats  = manager.stats();
    output.success(
        "方块 hook：set {} 次 → repaint {} 次",
        ghost::blockSetCalls(),
        mgrStats.repaints
    );
    output.success("调度：同步发送（tick 队列未启用）");

    if (auto* session = manager.active()) {
        auto const mirror = session->mirror();
        output.success(
            "当前投影 #{} / 共 {} 份；镜像 {} 名玩家 / 每名 {} 格 / 缓存 {} 包；重发 {} 次",
            session->id(),
            mgrStats.sessions,
            mirror.viewers,
            mirror.blocksPerViewer,
            mirror.cachedPackets,
            session->stats().resyncs
        );
    } else {
        output.success("当前没有投影会话（/lit load <file> 新建）");
    }
}

void printProbe(::CommandOutput& output) {
    auto& facade = hologram::HologramFacade::getInstance();
    auto  path   = facade.probe();

    output.success("§b=== LitematicaBE 能力探针 ===§r");
    output.success("版本：{} / 基线 {}", kVersion, kTargetBaseline);
    output.success(
        "构建：HologramLib={} 显示通道={}",
        hologram::HologramFacade::compiledWithHologramLib() ? "编入" : "未编入",
        hologram::toString(path)
    );
    output.success("前置模块：HologramLib.dll {}", moduleState(L"HologramLib.dll"));

    auto const& api = facade.batchApi();
    output.success(
        "编码器：来源 {} / 完整 {} / packetId {}",
        facade.batchEncoderModule(),
        api.complete() ? "是" : "否",
        ghost::BatchEncoder::getInstance().packetId()
    );

    auto&        states  = mapping::BlockStateResolve::getInstance();
    char const*  samples[] = {
        "minecraft:stone",
        "minecraft:air",
        "lit:g_acacia_door[lit:dir=2]",
        "minecraft:oak_stairs[upside_down_bit=false,weirdo_direction=2]",
    };
    for (auto const* text : samples) {
        std::uint32_t id = 0;
        auto const    ok = states.resolve(text, id);
        output.success("  {} → {}", text, ok ? std::to_string(id) : std::string{"失败"});
    }

    auto& map = mapping::GhostMap::getInstance();
    if (map.ensureLoaded()) {
        output.success("映射表：已加载 {} 个方块条目", map.stats().blocks);
        std::vector<std::pair<std::string, std::string>> stoneStates;
        auto const stone = map.resolve("minecraft:stone", stoneStates);
        std::vector<std::pair<std::string, std::string>> stairsStates{{"half", "top"}, {"facing", "east"}};
        auto const stairs = map.resolve("minecraft:oak_stairs", stairsStates);
        output.success("  minecraft:stone → {}", stone.ghost);
        output.success("  minecraft:oak_stairs[half=top,facing=east] → {}", stairs.ghost);
    } else {
        output.error("映射表未加载：{}", map.lastError());
    }

    output.success("§7/lit send 发一格；/lit batch 发 5×5；/lit info <file> 看投影统计§r");
}

void sendOne(::CommandOutput& output, ::Player& player, std::string const& block) {
    auto& states  = mapping::BlockStateResolve::getInstance();
    auto& encoder = ghost::BatchEncoder::getInstance();

    std::uint32_t id = 0;
    if (!states.resolve(block, id)) {
        output.error("状态解析失败：{}", block);
        return;
    }

    auto const                          pos = player.getFeetBlockPos();
    std::vector<ghost::HoloBlockChange> changes{
        {pos.x, pos.y + 2, pos.z, id, ghost::BatchEncoder::kUpdateFlags, 0, 0}
    };

    std::vector<ghost::EncodedPacket> packets;
    if (encoder.encode(changes, packets) == 0) {
        output.error("编码失败：批量通道不可用（先跑 /lit probe）");
        return;
    }
    auto const sent = flush(player, packets);
    output.success(
        "已发 {} 包 / {} 格 → ({}, {}, {})，方块 {}（runtimeId {}）",
        sent,
        changes.size(),
        pos.x,
        pos.y + 2,
        pos.z,
        block,
        id
    );
}

void sendBatch(::CommandOutput& output, ::Player& player) {
    auto& states  = mapping::BlockStateResolve::getInstance();
    auto& encoder = ghost::BatchEncoder::getInstance();

    std::uint32_t stone = 0;
    std::uint32_t glass = 0;
    if (!states.resolve("minecraft:stone", stone) || !states.resolve("minecraft:glass", glass)) {
        output.error("状态解析失败（stone / glass）");
        return;
    }

    auto const                          pos = player.getFeetBlockPos();
    std::vector<ghost::HoloBlockChange> changes;
    changes.reserve(25);
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
            auto const id = ((dx + dz) & 1) == 0 ? stone : glass;
            changes.push_back(
                {pos.x + dx, pos.y + 2, pos.z + dz, id, ghost::BatchEncoder::kUpdateFlags, 0, 0}
            );
        }
    }

    std::vector<ghost::EncodedPacket> packets;
    auto const                        encoded = encoder.encode(changes, packets);
    if (encoded == 0) {
        output.error("编码失败：批量通道不可用（先跑 /lit probe）");
        return;
    }
    auto const sent = flush(player, packets);
    output.success(
        "已发 {} 包（编码 {} 包 / {} 格）→ 以 ({}, {}, {}) 为中心的 5×5 平面",
        sent,
        encoded,
        changes.size(),
        pos.x,
        pos.y + 2,
        pos.z
    );
}

} // namespace litematicabe::command::probe
