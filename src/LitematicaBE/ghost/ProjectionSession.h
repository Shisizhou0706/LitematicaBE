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

    ~ProjectionSession();

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

    struct PlaceOptions {
        bool        overwrite{};       // true：连目标位置已有的非空气方块也覆盖
        std::size_t maxBlocks{200000}; // 单次上限，超出部分不处理（分层多次跑）
        bool        clearGhost{true};  // 放置前先收回 ghost 显示层
        bool        dryRun{};          // true：只跑解析统计，不写世界（/lit plan）
    };

    struct PlaceResult {
        std::size_t placed{};
        std::size_t skipped{};    // 目标位置非空气（overwrite=false）
        std::size_t unresolved{}; // 状态串解析不出可放置方块
        std::size_t failed{};     // setBlock 返回 false（区块未加载等）
        std::size_t pending{};    // 超出 maxBlocks 未处理
        std::size_t exact{};
        std::size_t mapped{};
        std::size_t defaulted{};
        std::size_t stateIgnored{};                 // 带状态但注册表没采纳（朝向/开关会丢）
        std::vector<std::string> ignoredSamples;    // 干跑时最多留 6 条
        std::size_t loadedChunks{};                 // 放置前拉起的区块数
        std::size_t failedChunks{};                 // 拉不起来的区块数
        std::size_t retried{};                      // 重载区块后重写成功的格数
        std::size_t mismatched{};                   // setBlock 报成功但回读不一致（假成功）
        std::string firstUnresolved;
        std::vector<std::string> unresolvedSamples; // 干跑时最多留 8 条，便于补映射表
        std::vector<std::string> defaultedSamples;  // 干跑：状态降级（只认出名字）的样例
    };

    /// 把投影里 [from, to] 层的方块**真实写入世界**（不是 ghost 显示）。
    ///
    /// 自下而上（y 升序、再 x、再 z）逐格 `setBlock`，方便依附方块落地。
    /// 维度优先级：player 所在维度 → 主世界。
    PlaceResult place(
        ::CommandOutput&     output,
        ::Player*            player,
        std::int32_t         from,
        std::int32_t         to,
        PlaceOptions const&  options
    );

    /// 把当前显示层重新推给指定玩家（进服/重生/传送/换维度/手动重发）。
    void sendCachedTo(::Player& player);

    /// 刷新包围盒线框：有显示层时框住显示层，否则框住整份投影。
    void refreshWireframe();

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
        std::uint32_t paletteId{}; // mPalette 下标：真实放置用的原始状态串
    };

    /// 把状态串收进调色板，返回下标（同一串只存一份）。
    std::uint32_t internPalette(std::string const& stateText);

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

    /// 原始状态串调色板（`.mcstructure` 是 Bedrock 口径，`.litematic` 是 Java 口径）。
    std::vector<std::string>                       mPalette;
    std::unordered_map<std::string, std::uint32_t> mPaletteIds;
    bool mIsBedrockSource{};

    std::int32_t                mBaseX{};
    std::int32_t                mBaseZ{};
    std::int32_t                mFootprintW{};
    std::int32_t                mFootprintD{};
    std::int32_t                mRotation{};
    // 当前旋转下的世界 XZ 包围盒（含端点），线框按 [min, max + 1] 使用。
    std::int32_t                mMinX{};
    std::int32_t                mMaxX{};
    std::int32_t                mMinZ{};
    std::int32_t                mMaxZ{};
    LayerRange                  mBounds{};
    LayerRange                  mShown{};
    Stats                       mStats{};
};

} // namespace litematicabe::ghost
