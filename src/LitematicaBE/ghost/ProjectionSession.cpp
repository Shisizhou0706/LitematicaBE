#include "LitematicaBE/ghost/ProjectionSession.h"

#include "LitematicaBE/core/schematic/LitematicFile.h"
#include "LitematicaBE/core/schematic/McstructureFile.h"
#include "LitematicaBE/core/util/TextEncoding.h"
#include "LitematicaBE/ghost/BlockHook.h"
#include "LitematicaBE/ghost/PacketSender.h"
#include "LitematicaBE/ghost/SurfaceCull.h"
#include "LitematicaBE/mapping/BlockStateResolve.h"
#include "LitematicaBE/mapping/GhostMap.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace litematicabe::ghost {

namespace {

namespace util = litematicabe::util;

constexpr std::size_t kMaxCollectBlocks = 400000;

std::string toUtf8String(std::filesystem::path const& path) { return util::toUtf8(path); }

} // namespace

ProjectionSession::ProjectionSession(std::uint32_t id, std::string label)
    : mId(id),
      mLabel(std::move(label)) {}

bool ProjectionSession::begin(
    ::CommandOutput&            output,
    std::filesystem::path const& file,
    int                         baseX,
    int                         baseY,
    int                         baseZ
) {
    auto& map     = mapping::GhostMap::getInstance();
    auto& states  = mapping::BlockStateResolve::getInstance();

    if (!map.ensureLoaded()) {
        output.error("映射表未加载：{}", map.lastError());
        return false;
    }

    auto const path = file;

    std::string ext = toUtf8String(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    bool const isMcstructure = ext == ".mcstructure";

    std::vector<PendingBlock> pending;
    pending.reserve(4096);

    std::size_t unmapped   = 0;
    std::size_t idFailures = 0;
    std::string firstBadId;
    bool        overflow = false;

    // 一份「方块 → 待发送项」的公共转换：两种文件格式共用，保证映射/状态串口径一致。
    auto pushBlock = [&](std::int32_t wx, std::int32_t wy, std::int32_t wz, core::schematic::BlockRef const& ref) -> bool {
        if (pending.size() >= kMaxCollectBlocks) {
            overflow = true;
            return false;
        }

        auto const resolved = map.resolve(ref.name, *ref.properties);
        if (!resolved.matched) {
            ++unmapped;
        }

        std::string stateString = resolved.ghost;
        if (!resolved.extraStates.empty() && stateString.find('[') == std::string::npos) {
            stateString.push_back('[');
            for (std::size_t i = 0; i < resolved.extraStates.size(); ++i) {
                if (i != 0) {
                    stateString.push_back(',');
                }
                stateString.append(resolved.extraStates[i].first);
                stateString.push_back('=');
                stateString.append(resolved.extraStates[i].second);
            }
            stateString.push_back(']');
        }

        std::uint32_t runtimeId = 0;
        if (!states.resolve(stateString, runtimeId)) {
            ++idFailures;
            if (firstBadId.empty()) {
                firstBadId = stateString;
            }
            return true;
        }

        pending.push_back({wx, wy, wz, runtimeId, std::string{ref.name}});
        return true;
    };

    if (isMcstructure) {
        core::schematic::McstructureFile structure;
        auto const                       error = core::schematic::readMcstructure(path, structure);
        if (!error.ok()) {
            output.error("解析失败：{}（{}）", error.message, toUtf8String(path));
            return false;
        }
        // 结构文件内部坐标就是局部坐标，直接以 base 为原点。
        core::schematic::forEachBlock(structure, [&](core::schematic::BlockRef const& ref) {
            return pushBlock(baseX + ref.x, baseY + ref.y, baseZ + ref.z, ref);
        });
    } else {
        core::schematic::LitematicFile litematic;
        auto const                       error = core::schematic::readLitematic(path, litematic);
        if (!error.ok()) {
            output.error("解析失败：{}（{}）", error.message, toUtf8String(path));
            return false;
        }

        auto const origin = litematic.regions.front().position;
        for (auto const& region : litematic.regions) {
            core::schematic::forEachBlock(region, [&](core::schematic::BlockRef const& ref) {
                return pushBlock(
                    baseX + (ref.x - origin[0]),
                    baseY + (ref.y - origin[1]),
                    baseZ + (ref.z - origin[2]),
                    ref
                );
            });
            if (overflow) {
                break;
            }
        }
    }

    if (pending.empty()) {
        output.error("没有可显示的方块（全部未映射或解析失败）");
        return false;
    }

    auto const cull = cullInterior(pending);

    mBlocks.clear();
    mBlocks.reserve(pending.size());
    std::int32_t minY = pending.front().y;
    std::int32_t maxY = pending.front().y;
    for (auto const& block : pending) {
        mBlocks.push_back(
            Entry{block.x, block.y, block.z, block.runtimeId, block.x - baseX, block.z - baseZ}
        );
        minY = std::min(minY, block.y);
        maxY = std::max(maxY, block.y);
    }

    mIndex.clear();
    mIndex.reserve(mBlocks.size() * 2);
    for (std::uint32_t i = 0; i < mBlocks.size(); ++i) {
        mIndex.emplace(packKey(mBlocks[i].x, mBlocks[i].y, mBlocks[i].z), i);
    }

    mFile   = toUtf8String(path.filename());
    mBounds = LayerRange{minY, maxY};
    mShown  = LayerRange{minY, minY - 1}; // 空区间

    // 旋转基准：load 原点 + 原始 XZ 尺寸（顺时针 90° 后宽深互换）。
    mBaseX      = baseX;
    mBaseZ      = baseZ;
    mFootprintW = 0;
    mFootprintD = 0;
    for (auto const& entry : mBlocks) {
        mFootprintW = std::max(mFootprintW, entry.odx + 1);
        mFootprintD = std::max(mFootprintD, entry.odz + 1);
    }
    mRotation = 0;
    mShownIndices.clear();
    mActive = true;
    ++mStats.shows;

    output.success(
        "§b[会话就绪]§r {}：{} 格（剔除 {}）→ 层 {} ~ {}（共 {} 层）",
        mFile,
        mBlocks.size(),
        cull.removed,
        minY,
        maxY,
        maxY - minY + 1
    );
    if (unmapped > 0) {
        output.success("未命中映射表（走了 default）：{} 格", unmapped);
    }
    if (idFailures > 0) {
        output.error("状态串解析失败 {} 格，例：{}", idFailures, firstBadId);
    }
    if (overflow) {
        output.error("投影超过收集上限 {} 格，只处理了前 {} 格", kMaxCollectBlocks, kMaxCollectBlocks);
    }
    return true;
}

std::size_t ProjectionSession::clear(::CommandOutput& output, ::Player& player) {
    if (mShownIndices.empty()) {
        output.success("当前没有显示的层");
        return 0;
    }

    auto level = ::ll::service::getLevel();
    if (!level) {
        output.error("当前没有加载的世界");
        return 0;
    }

    auto dimensionRef = level->getDimension(player.getDimensionId());
    auto dimension    = dimensionRef.lock();
    if (!dimension) {
        output.error("拿不到维度");
        return 0;
    }
    auto& source = dimension->getBlockSourceFromMainChunkSource();

    std::vector<HoloBlockChange> changes;
    changes.reserve(mShownIndices.size());
    for (auto const index : mShownIndices) {
        auto const& entry = mBlocks[index];
        auto const& block = source.getBlock(::BlockPos{entry.x, entry.y, entry.z});
        changes.push_back(
            {entry.x,
             entry.y,
             entry.z,
             static_cast<std::uint32_t>(block.mNetworkId),
             BatchEncoder::kUpdateFlags,
             0,
             0}
        );
    }

    std::vector<EncodedPacket> packets;
    auto const                 encoded = BatchEncoder::getInstance().encode(changes, packets);
    if (encoded > 0) {
        // 同步发：收回必须立刻生效，不能依赖 tick 队列。
        auto& sender = PacketSender::getInstance();
        level->forEachPlayer([&](::Player& p) {
            for (auto const& packet : packets) {
                sender.send(p, packet.packetId, packet.body);
            }
            return true;
        });
    }

    mStats.clears += 1;
    mStats.clearedBlocks += mShownIndices.size();
    mStats.sentPackets += encoded;

    auto const cleared = mShownIndices.size();
    mShownIndices.clear();
    mShownPackets.clear();
    mViewers.clear();
    mShown             = LayerRange{0, -1};

    output.success("已收回 {} 格（{} 包）", cleared, encoded);
    return encoded;
}

std::size_t ProjectionSession::showLayers(
    ::CommandOutput& output,
    ::Player&        player,
    std::int32_t     from,
    std::int32_t     to
) {
    if (!mActive) {
        output.error("没有活动会话：先 /lit load <file> [x y z]");
        return 0;
    }

    auto const low  = std::min(from, to);
    auto const high = std::max(from, to);
    if (high < mBounds.from || low > mBounds.to) {
        output.error("层 {}~{} 超出投影范围 {}~{}", low, high, mBounds.from, mBounds.to);
        return 0;
    }

    // 先收回旧层
    if (!mShownIndices.empty()) {
        std::size_t ignored = clear(output, player);
        (void)ignored;
    }

    std::vector<HoloBlockChange> changes;
    std::vector<std::size_t>     indices;
    for (std::size_t i = 0; i < mBlocks.size(); ++i) {
        auto const& entry = mBlocks[i];
        if (entry.y < low || entry.y > high) {
            continue;
        }
        indices.push_back(i);
        changes.push_back(
            {entry.x, entry.y, entry.z, entry.runtimeId, BatchEncoder::kUpdateFlags, 0, 0}
        );
    }

    if (changes.empty()) {
        output.success("层 {}~{} 没有方块", low, high);
        return 0;
    }

    std::vector<EncodedPacket> packets;
    auto const                 encoded = BatchEncoder::getInstance().encode(changes, packets);
    if (encoded == 0) {
        output.error("编码失败：批量通道不可用");
        return 0;
    }

    auto level = ::ll::service::getLevel();
    if (!level) {
        output.error("当前没有加载的世界");
        return 0;
    }

    auto&       sender  = ghost::PacketSender::getInstance();
    std::size_t players = 0;
    std::size_t sent    = 0;

    mShownIndices = std::move(indices);
    mShownPackets = packets; // 缓存：进服/重生重发时零编码开销
    mShown        = LayerRange{low, high};
    mStats.sentPackets += encoded;

    level->forEachPlayer([&](::Player& p) {
        ++players;
        // 同步发完：load/layer 必须立刻可见。队列（tick 预算）留作后续的大投影优化，
        // 不能让它成为「看不到投影」的唯一通道。
        for (auto const& packet : packets) {
            if (sender.send(p, packet.packetId, packet.body)) {
                ++sent;
            }
        }
        // 显示层镜像：记下这名玩家现在看到了哪些格。
        auto& keys = mViewers[p.getRealName()];
        keys.clear();
        keys.reserve(mShownIndices.size());
        for (auto const index : mShownIndices) {
            auto const& e = mBlocks[index];
            keys.insert(packKey(e.x, e.y, e.z));
        }
        return true;
    });

    if (players == 0) {
        output.success(
            "§b[层就绪]§r 层 {}~{}：{} 格 → {} 包（当前没有在线玩家，未发送；进入后重新执行即可）",
            low,
            high,
            changes.size(),
            encoded
        );
        return encoded;
    }
    output.success(
        "§b[层显示]§r 层 {}~{}：{} 格 → {} 包 × {} 名玩家（已发 {} 包）",
        low,
        high,
        changes.size(),
        encoded,
        players,
        sent
    );
    return encoded;
}

} // namespace litematicabe::ghost

namespace litematicabe::ghost {

std::uint64_t ProjectionSession::packKey(std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
    constexpr std::uint64_t kMask = 0x1FFFFF;
    auto const              ux    = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) & kMask;
    auto const              uy    = static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) & kMask;
    auto const              uz    = static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) & kMask;
    return (ux << 42) | (uy << 21) | uz;
}

bool ProjectionSession::contains(std::int32_t x, std::int32_t y, std::int32_t z) const {
    if (!mActive) {
        return false;
    }
    return mIndex.find(packKey(x, y, z)) != mIndex.end();
}

void ProjectionSession::requestRepaint(std::int32_t x, std::int32_t y, std::int32_t z) {
    auto const it = mIndex.find(packKey(x, y, z));
    if (it == mIndex.end()) {
        return;
    }
    auto const& entry = mBlocks[it->second];
    repaintOne(entry.x, entry.y, entry.z, entry.runtimeId);
}

void ProjectionSession::repaintOne(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t runtimeId) {
    // 关键：不能立刻发。PlayerDestroyBlockEvent 在引擎把方块更新发给客户端之前触发，
    // 立刻发 ghost 会被随后到达的 air 覆盖。延迟 150ms 让引擎的更新先落地。
    ::ll::thread::ServerThreadExecutor::getDefault().executeAfter(
        [this, x, y, z, runtimeId]() {
            if (!mActive) {
                return;
            }
            std::vector<HoloBlockChange> changes{
                {x, y, z, runtimeId, BatchEncoder::kUpdateFlags, 0, 0}
            };
            std::vector<EncodedPacket> packets;
            if (BatchEncoder::getInstance().encode(changes, packets) == 0) {
                return;
            }
            auto level = ::ll::service::getLevel();
            if (!level) {
                return;
            }
            auto&       sender = PacketSender::getInstance();
            std::size_t sent   = 0;
            level->forEachPlayer([&](::Player& player) {
                for (auto const& packet : packets) {
                    if (sender.send(player, packet.packetId, packet.body)) {
                        ++sent;
                    }
                }
                return true;
            });
            ++mStats.repaints;
        },
        std::chrono::milliseconds(150)
    );
}

// ─────────────────────────── 显示层镜像 / 重发调度 ───────────────────────────

std::size_t ProjectionSession::prepareLayers(::CommandOutput& output, std::int32_t from, std::int32_t to) {
    if (!mActive) {
        output.error("没有活动会话：先 /lit load <file> [x y z]");
        return 0;
    }

    auto const low  = std::min(from, to);
    auto const high = std::max(from, to);
    if (high < mBounds.from || low > mBounds.to) {
        output.error("层 {}~{} 超出投影范围 {}~{}", low, high, mBounds.from, mBounds.to);
        return 0;
    }

    std::vector<HoloBlockChange> changes;
    std::vector<std::size_t>     indices;
    for (std::size_t i = 0; i < mBlocks.size(); ++i) {
        auto const& entry = mBlocks[i];
        if (entry.y < low || entry.y > high) {
            continue;
        }
        indices.push_back(i);
        changes.push_back({entry.x, entry.y, entry.z, entry.runtimeId, BatchEncoder::kUpdateFlags, 0, 0});
    }

    if (changes.empty()) {
        output.success("层 {}~{} 没有方块", low, high);
        return 0;
    }

    std::vector<EncodedPacket> packets;
    auto const                 encoded = BatchEncoder::getInstance().encode(changes, packets);
    if (encoded == 0) {
        output.error("编码失败：批量通道不可用");
        return 0;
    }

    mShownIndices = std::move(indices);
    mShownPackets = std::move(packets);
    mShown        = LayerRange{low, high};
    mViewers.clear();

    output.success(
        "§b[层就绪]§r 层 {}~{}：{} 格 → {} 包（当前无玩家，已缓存；玩家进服后数秒内自动补发）",
        low,
        high,
        changes.size(),
        encoded
    );
    return encoded;
}

void ProjectionSession::sendCachedTo(::Player& player) {
    if (mShownPackets.empty()) {
        return;
    }
    auto& sender = PacketSender::getInstance();
    for (auto const& packet : mShownPackets) {
        sender.send(player, packet.packetId, packet.body);
    }

    auto& keys = mViewers[player.getRealName()];
    keys.clear();
    keys.reserve(mShownIndices.size());
    for (auto const index : mShownIndices) {
        auto const& entry = mBlocks[index];
        keys.insert(packKey(entry.x, entry.y, entry.z));
    }
    mStats.sentPackets += mShownPackets.size();
    ++mStats.resyncs;
}

ProjectionSession::MirrorInfo ProjectionSession::mirror() const {
    MirrorInfo info;
    info.viewers         = mViewers.size();
    info.blocksPerViewer = mShownIndices.size();
    info.cachedPackets   = mShownPackets.size();
    return info;
}

std::size_t ProjectionSession::rotate(::CommandOutput& output, ::Player* player) {
    if (!mActive) {
        output.error("没有活动会话：先 /lit load <file> [x y z]");
        return 0;
    }

    auto const wasShown = !mShownIndices.empty();
    auto const from     = mShown.from;
    auto const to       = mShown.to;

    if (wasShown && player != nullptr) {
        std::size_t ignored = clear(output, *player);
        (void)ignored;
    }

    mRotation = (mRotation + 90) % 360;

    for (auto& entry : mBlocks) {
        std::int32_t nx = 0;
        std::int32_t nz = 0;
        switch (mRotation) {
        case 90:
            nx = mFootprintD - 1 - entry.odz;
            nz = entry.odx;
            break;
        case 180:
            nx = mFootprintW - 1 - entry.odx;
            nz = mFootprintD - 1 - entry.odz;
            break;
        case 270:
            nx = entry.odz;
            nz = mFootprintW - 1 - entry.odx;
            break;
        case 0:
        default:
            nx = entry.odx;
            nz = entry.odz;
            break;
        }
        entry.x = mBaseX + nx;
        entry.z = mBaseZ + nz;
    }

    mIndex.clear();
    mIndex.reserve(mBlocks.size() * 2);
    for (std::uint32_t i = 0; i < mBlocks.size(); ++i) {
        mIndex.emplace(packKey(mBlocks[i].x, mBlocks[i].y, mBlocks[i].z), i);
    }

    // 坐标全变了，旧镜像失效。
    mViewers.clear();

    // 诊断：旋转后的 XZ 包围盒（宽深应互换）。
    std::int32_t minX = INT32_MAX;
    std::int32_t maxX = INT32_MIN;
    std::int32_t minZ = INT32_MAX;
    std::int32_t maxZ = INT32_MIN;
    for (auto const& entry : mBlocks) {
        minX = std::min(minX, entry.x);
        maxX = std::max(maxX, entry.x);
        minZ = std::min(minZ, entry.z);
        maxZ = std::max(maxZ, entry.z);
    }
    output.success(
        "§b[旋转]§r 当前 {}°，占用 X {}~{}（{}）/ Z {}~{}（{}）",
        mRotation,
        minX,
        maxX,
        maxX - minX + 1,
        minZ,
        maxZ,
        maxZ - minZ + 1
    );

    if (!wasShown) {
        output.success("已旋转到 {}°（当前没有显示层；/lit layer <from> [to] 后再看）", mRotation);
        return 0;
    }
    if (player != nullptr) {
        return showLayers(output, *player, from, to);
    }
    return prepareLayers(output, from, to);
}

} // namespace litematicabe::ghost
