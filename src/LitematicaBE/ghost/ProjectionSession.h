#pragma once

#include "LitematicaBE/ghost/BatchEncoder.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CommandOutput;
class Player;

namespace litematicabe::ghost {

/// 一份投影的显示会话（可同时存在多份，由 `ProjectionManager` 管理）。
///
/// 每个会话独立持有：剔除后的方块表、坐标索引、当前显示层的编码缓存、每玩家镜像。
/// 互相之间唯一共享的是「玩家状态」（维度/生死/位置），那部分在 Manager 里。
///
/// 分层显示收回旧层的做法：读世界的**真实方块**，批量编码发回客户端
/// （假方块只存在于客户端，发回真实方块就等于擦掉它）。
class ProjectionSession {
public:
    ProjectionSession(std::uint32_t id, std::string label);

    struct LayerRange {
        std::int32_t from{}; // 世界 y 下限（含）
        std::int32_t to{};   // 世界 y 上限（含）
    };

    [[nodiscard]] std::uint32_t id() const noexcept { return mId; }
    [[nodiscard]] std::string const& label() const noexcept { return mLabel; }

    /// 解析文件、映射、剔除，并把结果记进会话（不发送）。
    /// @return false 表示解析/映射失败（原因已写入 output）。
    bool begin(::CommandOutput& output, std::filesystem::path const& file, int baseX, int baseY, int baseZ);

    /// 显示指定层区间：先把当前显示的层收回，再发新区间。
    std::size_t showLayers(::CommandOutput& output, ::Player& player, std::int32_t from, std::int32_t to);

    /// 显示单层。
    std::size_t showLayer(::CommandOutput& output, ::Player& player, std::int32_t layer) {
        return showLayers(output, player, layer, layer);
    }

    /// 无玩家时预先准备层：只计算并缓存编码结果，不发送。
    std::size_t prepareLayers(::CommandOutput& output, std::int32_t from, std::int32_t to);

    /// 收回当前显示的全部方块（读真实方块发回）。
    std::size_t clear(::CommandOutput& output, ::Player& player);

    /// 整体旋转 90°（顺时针，绕 load 原点；player 为空时只更新缓存）。
    std::size_t rotate(::CommandOutput& output, ::Player* player);

    /// 把当前显示层重新推给指定玩家（进服/重生/传送/换维度/手动重发）。
    void sendCachedTo(::Player& player);

    [[nodiscard]] std::int32_t rotation() const noexcept { return mRotation; }
    [[nodiscard]] bool         active() const noexcept { return mActive; }
    [[nodiscard]] std::string const& file() const noexcept { return mFile; }
    [[nodiscard]] std::size_t  blockCount() const noexcept { return mBlocks.size(); }
    [[nodiscard]] LayerRange   bounds() const noexcept { return mBounds; }
    [[nodiscard]] LayerRange   shown() const noexcept { return mShown; }

    /// 该坐标是否属于这份投影（方块变化分发用）。
    [[nodiscard]] bool contains(std::int32_t x, std::int32_t y, std::int32_t z) const;

    /// 补发某一格（方块被挖掉后补回 ghost）。
    void requestRepaint(std::int32_t x, std::int32_t y, std::int32_t z);

    struct Stats {
        std::uint64_t shows{};
        std::uint64_t clears{};
        std::uint64_t clearedBlocks{};
        std::uint64_t sentPackets{};
        std::uint64_t repaints{};
        std::uint64_t resyncs{};
    };

    [[nodiscard]] Stats stats() const { return mStats; }

    struct MirrorInfo {
        std::size_t viewers{};
        std::size_t blocksPerViewer{};
        std::size_t cachedPackets{};
    };

    [[nodiscard]] MirrorInfo mirror() const;

private:
    struct Entry {
        std::int32_t  x{};    // 当前世界坐标（随旋转变化）
        std::int32_t  y{};
        std::int32_t  z{};
        std::uint32_t runtimeId{};
        std::int32_t  odx{};  // 相对 load 原点的原始 X（旋转前）
        std::int32_t  odz{};  // 相对 load 原点的原始 Z
    };

    static std::uint64_t packKey(std::int32_t x, std::int32_t y, std::int32_t z) noexcept;

    void repaintOne(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t runtimeId);

    std::uint32_t               mId{};
    std::string                 mLabel;
    bool                        mActive{};
    std::string                 mFile;
    std::vector<Entry>          mBlocks;
    std::unordered_map<std::uint64_t, std::uint32_t> mIndex;
    std::vector<std::size_t>    mShownIndices;
    std::vector<EncodedPacket>  mShownPackets;
    std::unordered_map<std::string, std::unordered_set<std::uint64_t>> mViewers;

    std::int32_t                mBaseX{};
    std::int32_t                mBaseZ{};
    std::int32_t                mFootprintW{};
    std::int32_t                mFootprintD{};
    std::int32_t                mRotation{};
    LayerRange                  mBounds{};
    LayerRange                  mShown{};
    Stats                       mStats{};
};

} // namespace litematicabe::ghost
