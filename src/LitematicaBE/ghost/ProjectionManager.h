#pragma once

#include "LitematicaBE/ghost/ProjectionSession.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class CommandOutput;
class Player;

namespace litematicabe::ghost {

/// 多投影管理：会话表 + 当前选中 + 全局的玩家状态轮询 + 方块变化分发。
///
/// 旧版（3.x）用「手持木剑选择投影」来表达"当前操作哪一份"；这里换成命令：
///   `/lit load <file>`      新建会话并自动选中（输出 `#id`）
///   `/lit projects`         列出所有会话（id / 文件 / 格数 / 层范围 / 当前显示）
///   `/lit select <id>`      切换选中
///   `/lit remove <id>`      收回并删除某个会话
///   `/lit layer|rotate|unload|resend` 都作用于**当前选中**会话。
class ProjectionManager {
public:
    static ProjectionManager& getInstance();

    /// 新建会话（解析 + 剔除 + 选中），失败返回 nullptr（原因写入 output）。
    ProjectionSession* create(
        ::CommandOutput&              output,
        std::filesystem::path const&  file,
        int                           baseX,
        int                           baseY,
        int                           baseZ
    );

    [[nodiscard]] ProjectionSession* active();
    [[nodiscard]] ProjectionSession* find(std::uint32_t id);

    bool select(::CommandOutput& output, std::uint32_t id);
    bool remove(::CommandOutput& output, std::uint32_t id, ::Player* player);
    void list(::CommandOutput& output) const;

    /// 方块变化分发：哪份投影包含该坐标就由谁补格（可能同时命中多份）。
    void requestRepaintAt(std::int32_t x, std::int32_t y, std::int32_t z);

    /// 玩家状态变化（进服/重生/传送/换维度）时，对所有会话补发当前显示层。
    void sendCachedTo(::Player& player);

    /// 重新绘制所有会话的包围盒线框（`/lit box` 重新打开时用）。
    void refreshWireframes();

    void start();
    void stop();

    struct Stats {
        std::size_t   sessions{};
        std::uint32_t active{};
        std::uint64_t repaints{};
        std::uint64_t resyncs{};
    };

    [[nodiscard]] Stats stats() const;

private:
    ProjectionManager() = default;

    void schedulePoll(std::int64_t delayMs);

    void schedulePump(std::int64_t delayMs);
    void pollPlayers();
    [[nodiscard]] std::string nextLabel(std::string const& file) const;

    std::map<std::uint32_t, std::unique_ptr<ProjectionSession>> mSessions;
    std::uint32_t mNextId{1};
    std::uint32_t mActiveId{0};

    bool          mPolling{};
    std::uint32_t mPollTicks{};

    // 玩家状态是全局的（与会话无关），检测到变化就通知所有会话补发。
    std::unordered_map<std::string, std::int32_t> mSeenDimension;
    std::unordered_map<std::string, bool>         mSeenAlive;

    struct PosKey {
        std::int32_t x{};
        std::int32_t y{};
        std::int32_t z{};
    };

    std::unordered_map<std::string, PosKey> mSeenPos;
    std::unordered_set<std::string>         mPendingResend;

    std::uint64_t mRepaints{};
    std::uint64_t mResyncs{};
};

} // namespace litematicabe::ghost
