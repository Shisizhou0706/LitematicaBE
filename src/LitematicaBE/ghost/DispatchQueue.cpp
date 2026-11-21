#include "LitematicaBE/ghost/DispatchQueue.h"

#include "LitematicaBE/ghost/PacketSender.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include <string>
#include <utility>

namespace litematicabe::ghost {

DispatchQueue& DispatchQueue::getInstance() {
    static DispatchQueue instance;
    return instance;
}

void DispatchQueue::start() {
    if (mRunning) {
        return;
    }
    auto& bus = ::ll::event::EventBus::getInstance();
    mListener = bus.emplaceListener<::ll::event::LevelTickEvent>([this](::ll::event::LevelTickEvent&) {
        onTick();
    });
    mRunning = true;
}

void DispatchQueue::stop() {
    if (!mRunning) {
        return;
    }
    if (mListener) {
        ::ll::event::EventBus::getInstance().removeListener(mListener);
        mListener.reset();
    }
    mRunning = false;
    mJobs.clear();
}

std::size_t DispatchQueue::enqueue(std::string const& playerName, std::vector<EncodedPacket> packets) {
    if (packets.empty()) {
        return 0;
    }
    auto const count = packets.size();
    mQueued += count;
    mJobs.push_back(Job{playerName, std::move(packets), 0});
    return count;
}

void DispatchQueue::clear() {
    mJobs.clear();
}

void DispatchQueue::onTick() {
    ++mTicks;
    if (mJobs.empty()) {
        return;
    }
    auto level = ::ll::service::getLevel();
    if (!level) {
        return;
    }

    auto&       sender = PacketSender::getInstance();
    std::size_t budget = mBudgetPerTick;

    while (budget > 0 && !mJobs.empty()) {
        auto& job = mJobs.front();

        bool found = false;
        level->forEachPlayer([&](::Player& player) {
            if (player.getRealName() != job.playerName) {
                return true;
            }
            found = true;
            while (budget > 0 && job.next < job.packets.size()) {
                auto const& packet = job.packets[job.next];
                if (sender.send(player, packet.packetId, packet.body)) {
                    ++job.next;
                    ++mSent;
                } else {
                    // 发送失败（刚断线等）：整组丢弃，避免每 tick 空转。
                    mDropped += job.packets.size() - job.next;
                    job.next = job.packets.size();
                }
                --budget;
            }
            return false;
        });

        if (job.next >= job.packets.size()) {
            mJobs.pop_front();
            continue;
        }
        if (!found) {
            // 玩家不在线（或名字对不上）：整组丢弃，并记一次原因。
            mDropped += job.packets.size() - job.next;
            if (auto logger = ::ll::mod::NativeMod::current()) {
                logger->getLogger().warn(
                    "dispatch: player '{}' not found, dropped {} packets",
                    job.playerName,
                    job.packets.size() - job.next
                );
            }
            mJobs.pop_front();
            continue;
        }
    }
}

DispatchQueue::Stats DispatchQueue::stats() const {
    Stats stats;
    stats.ticks  = mTicks;
    stats.queued = mQueued;
    stats.sent   = mSent;
    stats.dropped = mDropped;
    stats.pendingJobs = mJobs.size();
    for (auto const& job : mJobs) {
        stats.pendingPackets += job.packets.size() - job.next;
    }
    return stats;
}

} // namespace litematicabe::ghost
