#include "LitematicaBE/command/ProbeCommand.h"

#include "LitematicaBE/Version.h"
#include "LitematicaBE/ghost/BatchEncoder.h"
#include "LitematicaBE/ghost/PacketSender.h"
#include "LitematicaBE/hologram/HologramFacade.h"
#include "LitematicaBE/mapping/BlockStateResolve.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace litematicabe::command {

/// 子命令。枚举成员名就是命令补全里的候选（LL 的 EnumName 负责映射）。
enum class LitAction : int {
    probe = 0,
    send  = 1,
    batch = 2,
};

struct LitParams {
    LitAction action;
};

namespace {

namespace ghost = litematicabe::ghost;

/// 取命令执行者；不是玩家时返回 nullptr。
::Player* playerOf(::CommandOrigin const& origin) {
    auto* actor = origin.getEntity();
    if (actor == nullptr || !actor->isPlayer()) {
        return nullptr;
    }
    return static_cast<::Player*>(actor);
}

/// 把已编码的包逐条发给玩家，返回成功条数。
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

void printState(::CommandOutput& output) {
    auto&        facade     = hologram::HologramFacade::getInstance();
    auto const   path       = facade.probe();
    auto&        states     = mapping::BlockStateResolve::getInstance();
    auto&        encoder    = ghost::BatchEncoder::getInstance();
    auto const   stateStats = states.stats();
    auto const&  sendStats  = ghost::PacketSender::getInstance().stats();

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
    output.success(
        "前置模块：{}",
        ::GetModuleHandleW(L"HologramLib.dll") != nullptr ? "已加载" : "未加载"
    );

    auto const& api = facade.batchApi();
    output.success(
        "编码器：来源 {} / 完整 {} / packetId {}",
        facade.batchEncoderModule(),
        api.complete() ? "是" : "否",
        ghost::BatchEncoder::getInstance().packetId()
    );

    // 编码自检：真跑一次「1 格石头」的编码，把包体长度与开头几字节打出来。
    // 这样在控制台就能确认 C ABI 产出了合法包体，不需要玩家在线。
    auto&         states  = mapping::BlockStateResolve::getInstance();
    auto&         encoder = ghost::BatchEncoder::getInstance();
    std::uint32_t stoneId = 0;
    if (states.resolve("minecraft:stone", stoneId)) {
        std::vector<ghost::HoloBlockChange> probeChanges{
            {0, 0, 0, stoneId, ghost::BatchEncoder::kUpdateFlags, 0, 0}
        };
        std::vector<ghost::EncodedPacket> probePackets;
        auto const                        encoded = encoder.encode(probeChanges, probePackets);
        if (encoded == 0 || probePackets.empty()) {
            output.error("编码自检：失败（编码器无输出）");
        } else {
            auto const& body = probePackets.front().body;
            std::string head;
            for (std::size_t i = 0; i < body.size() && i < 12; ++i) {
                char buffer[4]{};
                std::snprintf(buffer, sizeof(buffer), "%02X ", static_cast<unsigned char>(body[i]));
                head += buffer;
            }
            output.success("编码自检：{} 包 / {} 字节 / 头部 {}", encoded, body.size(), head);
        }
    } else {
        output.error("编码自检：minecraft:stone 解析失败");
    }

    char const* samples2[] = {
        "minecraft:stone",
        "minecraft:air",
        "lit:g_acacia_door[lit:dir=2]",
        "minecraft:oak_stairs[upside_down_bit=false,weirdo_direction=2]",
    };
    for (auto const* text : samples2) {
        std::uint32_t id = 0;
        auto const    ok = states.resolve(text, id);
        output.success("  {} → {}", text, ok ? std::to_string(id) : std::string{"失败"});
    }

    output.success("编码器 packetId={}（0 = 批量通道不可用）", ghost::BatchEncoder::getInstance().packetId());
    output.success("§7/lit send 发一格；/lit batch 发 5×5 平面§r");
}

void sendOne(::CommandOutput& output, ::Player& player) {
    auto& states  = mapping::BlockStateResolve::getInstance();
    auto& encoder = ghost::BatchEncoder::getInstance();

    std::uint32_t id = 0;
    if (!states.resolve("minecraft:stone", id)) {
        output.error("状态解析失败：minecraft:stone");
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
    output.success("已发 {} 包 / {} 格 → ({}, {}, {})", sent, changes.size(), pos.x, pos.y + 2, pos.z);
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

void runAction(::CommandOutput& output, ::CommandOrigin const& origin, LitAction action) {
    switch (action) {
    case LitAction::probe:
        printProbe(output);
        return;
    case LitAction::send:
    case LitAction::batch: {
        auto* player = playerOf(origin);
        if (player == nullptr) {
            output.error("该子命令需要以玩家身份执行");
            return;
        }
        if (action == LitAction::send) {
            sendOne(output, *player);
        } else {
            sendBatch(output, *player);
        }
        return;
    }
    default:
        printState(output);
        return;
    }
}

} // namespace

void registerLitCommand() {
    auto& registrar = ll::command::CommandRegistrar::getInstance(false);
    registrar.tryRegisterEnum<LitAction>();

    auto& cmd = registrar.getOrCreateCommand("lit", "LitematicaBE 投影插件", ::CommandPermissionLevel::Any);

    cmd.overload().execute([](::CommandOrigin const& origin, ::CommandOutput& output) {
        (void)origin;
        printState(output);
    });

    cmd.overload<LitParams>()
        .required("action")
        .execute([](::CommandOrigin const& origin, ::CommandOutput& output, LitParams const& params) {
            runAction(output, origin, params.action);
        });
}

} // namespace litematicabe::command
