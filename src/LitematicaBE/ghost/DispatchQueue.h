#pragma once

#include "LitematicaBE/ghost/BatchEncoder.h"

#include "ll/api/event/EventBus.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace litematicabe::ghost {

/// 按 tick 预算发假方块包。
///
/// 一个 3 万格的投影会编出上百个包，一次性 flush 会让主线程和客户端都顶不住；
/// 这里把包排进队列，每个服务器刻只发 `packetsPerTick` 个（默认 8，与库内部的发包预算同量级）。
///
/// 队列按玩家名归组：玩家中途退出时该组直接丢弃（不重发已发部分，重进后由调用方重新 load）。
class DispatchQueue {
public:
    static DispatchQueue& getInstance();

    void start();
    void stop();
    [[nodiscard]] bool running() const noexcept { return mRunning; }

    /// 入队；返回排队包数（0 表示没有可发的包）。
    std::size_t enqueue(std::string const& playerName, std::vector<EncodedPacket> packets);

    void setPacketsPerTick(std::size_t count) noexcept { mBudgetPerTick = count == 0 ? 1 : count; }
    [[nodiscard]] std::size_t packetsPerTick() const noexcept { return mBudgetPerTick; }

    /// 立刻把队列清空（`/lit stop` 或关服时用）。
    void clear();

    struct Stats {
        std::uint64_t ticks{};
        std::uint64_t queued{};
        std::uint64_t sent{};
        std::uint64_t dropped{};
        std::size_t   pendingJobs{};
        std::size_t   pendingPackets{};
    };

    [[nodiscard]] Stats stats() const;

private:
    DispatchQueue() = default;

    struct Job {
        std::string                playerName;
        std::vector<EncodedPacket> packets;
        std::size_t                next{};
    };

    void onTick();

    bool                      mRunning{};
    std::size_t               mBudgetPerTick{8};
    std::deque<Job>           mJobs;
    ll::event::ListenerPtr    mListener; // 定义在 .cpp（避免头文件拉事件系统）

    std::uint64_t mTicks{};
    std::uint64_t mQueued{};
    std::uint64_t mSent{};
    std::uint64_t mDropped{};
};

} // namespace litematicabe::ghost
