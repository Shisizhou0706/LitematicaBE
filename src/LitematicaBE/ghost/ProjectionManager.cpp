#include "LitematicaBE/ghost/ProjectionManager.h"

#include "LitematicaBE/core/util/TextEncoding.h"
#include "LitematicaBE/ghost/BlockHook.h"

#include "ll/api/mod/Mod.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace litematicabe::ghost {

namespace {

constexpr std::int32_t kTeleportThreshold = 64;

} // namespace

ProjectionManager& ProjectionManager::getInstance() {
    static ProjectionManager instance;
    return instance;
}

std::string ProjectionManager::nextLabel(std::string const& file) const {
    return file;
}

ProjectionSession* ProjectionManager::create(
    ::CommandOutput&             output,
    std::filesystem::path const& file,
    int                          baseX,
    int                          baseY,
    int                          baseZ
) {
    auto const id = mNextId++;
    auto session  = std::make_unique<ProjectionSession>(id, nextLabel(util::toUtf8(file.filename())));

    if (!session->begin(output, file, baseX, baseY, baseZ)) {
        return nullptr;
    }

    auto* raw      = session.get();
    mSessions[id]  = std::move(session);
    mActiveId      = id;
    return raw;
}

ProjectionSession* ProjectionManager::active() {
    auto const it = mSessions.find(mActiveId);
    return it == mSessions.end() ? nullptr : it->second.get();
}

ProjectionSession* ProjectionManager::find(std::uint32_t id) {
    auto const it = mSessions.find(id);
    return it == mSessions.end() ? nullptr : it->second.get();
}

bool ProjectionManager::select(::CommandOutput& output, std::uint32_t id) {
    if (mSessions.find(id) == mSessions.end()) {
        output.error("没有 #{} 这份投影（用 /lit projects 查看）", id);
        return false;
    }
    mActiveId = id;
    auto& session = *mSessions[id];
    output.success(
        "已选中 #{} {}（{} 格，层 {}~{}）",
        session.id(),
        session.label(),
        session.blockCount(),
        session.bounds().from,
        session.bounds().to
    );
    return true;
}

bool ProjectionManager::remove(::CommandOutput& output, std::uint32_t id, ::Player* player) {
    auto const it = mSessions.find(id);
    if (it == mSessions.end()) {
        output.error("没有 #{} 这份投影（用 /lit projects 查看）", id);
        return false;
    }

    auto& session = *it->second;
    if (player != nullptr) {
        session.clear(output, *player);
    }

    output.success("已删除投影 #{} {}（{} 格）", session.id(), session.label(), session.blockCount());
    mSessions.erase(it);

    if (mActiveId == id) {
        mActiveId = mSessions.empty() ? 0 : mSessions.rbegin()->first;
        if (auto* next = active()) {
            output.success("当前选中切换到 #{} {}", next->id(), next->label());
        }
    }
    return true;
}

void ProjectionManager::list(::CommandOutput& output) const {
    if (mSessions.empty()) {
        output.success("当前没有投影会话（/lit load <file> [x y z] 新建）");
        return;
    }

    output.success("§b[投影列表]§r 共 {} 份，#{} 为当前选中", mSessions.size(), mActiveId);
    for (auto const& [id, session] : mSessions) {
        auto const  mirror = session->mirror();
        auto const& stats  = session->stats();
        auto const  shown  = session->shown();
        char        shownText[64]{};
        if (mirror.cachedPackets == 0) {
            std::snprintf(shownText, sizeof(shownText), "未显示");
        } else {
            std::snprintf(shownText, sizeof(shownText), "层 %d~%d（%zu 包）", shown.from, shown.to, mirror.cachedPackets);
        }

        output.success(
            "{}#{} {}｜{} 格｜层 {}~{}｜旋转 {}°｜{}｜重发 {} 次",
            id == mActiveId ? "§a→ §r" : "  ",
            id,
            session->label(),
            session->blockCount(),
            session->bounds().from,
            session->bounds().to,
            session->rotation(),
            shownText,
            stats.resyncs
        );
    }
    output.success("§7/lit select <id> 切换；/lit remove <id> 收回并删除§r");
}

void ProjectionManager::requestRepaintAt(std::int32_t x, std::int32_t y, std::int32_t z) {
    for (auto& [id, session] : mSessions) {
        (void)id;
        if (!session->active()) {
            continue;
        }
        if (session->contains(x, y, z)) {
            session->requestRepaint(x, y, z);
            ++mRepaints;
        }
    }
}

void ProjectionManager::sendCachedTo(::Player& player) {
    for (auto& [id, session] : mSessions) {
        (void)id;
        session->sendCachedTo(player);
        ++mResyncs;
    }
}

void ProjectionManager::start() {
    if (mPolling) {
        return;
    }
    installBlockChangedHook();
    mPolling = true;
    schedulePoll(1000);

    if (auto mod = ::ll::mod::NativeMod::current()) {
        mod->getLogger().info("projection manager started (block hook + 1s player poll)");
    }
}

void ProjectionManager::stop() {
    if (!mPolling) {
        return;
    }
    mPolling = false;
    uninstallBlockChangedHook();
}

void ProjectionManager::schedulePoll(std::int64_t delayMs) {
    ::ll::thread::ServerThreadExecutor::getDefault().executeAfter(
        [this]() {
            if (!mPolling) {
                return;
            }
            pollPlayers();
            schedulePoll(1000);
        },
        std::chrono::milliseconds(delayMs)
    );
}

void ProjectionManager::pollPlayers() {
    auto level = ::ll::service::getLevel();
    if (!level) {
        return;
    }

    ++mPollTicks;
    // 兜底心跳：5 分钟一次全量补发（正常情况下进服/重生/传送/换维度都会被下面的信号捕获）。
    bool const heartbeat = (mPollTicks % 300) == 0;

    std::unordered_set<std::string> online;
    level->forEachPlayer([&](::Player& player) {
        auto const name = player.getRealName();
        online.insert(name);

        auto const dimension      = static_cast<std::int32_t>(player.getDimensionId());
        auto const seenDim        = mSeenDimension.find(name);
        bool const isNew          = seenDim == mSeenDimension.end();
        bool const dimensionMoved = !isNew && seenDim->second != dimension;
        mSeenDimension[name]      = dimension;

        // 重生：Actor::isDead() 由 true 变 false。
        bool const alive     = !player.isDead();
        auto const aliveIt   = mSeenAlive.find(name);
        bool const respawned = aliveIt != mSeenAlive.end() && !aliveIt->second && alive;
        mSeenAlive[name]     = alive;

        // 大跨度位移（传送/跨维度回程）会让客户端重建区块。
        auto const   pos = player.getPosition();
        PosKey const now{
            static_cast<std::int32_t>(std::floor(pos.x)),
            static_cast<std::int32_t>(std::floor(pos.y)),
            static_cast<std::int32_t>(std::floor(pos.z))
        };
        bool teleported = false;
        if (auto const posIt = mSeenPos.find(name); posIt != mSeenPos.end()) {
            auto const& prev = posIt->second;
            teleported       = std::abs(now.x - prev.x) > kTeleportThreshold
                       || std::abs(now.y - prev.y) > kTeleportThreshold
                       || std::abs(now.z - prev.z) > kTeleportThreshold;
        }
        mSeenPos[name] = now;

        if (isNew || dimensionMoved || respawned || teleported) {
            mPendingResend.insert(name);
            return true;
        }

        if (auto const pending = mPendingResend.find(name); pending != mPendingResend.end()) {
            mPendingResend.erase(pending);
            sendCachedTo(player);
            return true;
        }

        if (heartbeat) {
            sendCachedTo(player);
        }
        return true;
    });

    for (auto it = mSeenDimension.begin(); it != mSeenDimension.end();) {
        if (online.find(it->first) == online.end()) {
            mSeenAlive.erase(it->first);
            mSeenPos.erase(it->first);
            mPendingResend.erase(it->first);
            it = mSeenDimension.erase(it);
        } else {
            ++it;
        }
    }
}

ProjectionManager::Stats ProjectionManager::stats() const {
    Stats stats;
    stats.sessions = mSessions.size();
    stats.active   = mActiveId;
    stats.repaints = mRepaints;
    stats.resyncs  = mResyncs;
    return stats;
}

} // namespace litematicabe::ghost
