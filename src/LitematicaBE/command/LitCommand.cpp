#include "LitematicaBE/command/LitCommand.h"

#include "LitematicaBE/command/ProbeActions.h"
#include "LitematicaBE/command/SchematicActions.h"

#include "LitematicaBE/core/util/TextEncoding.h"
#include "LitematicaBE/ghost/ProjectionManager.h"
#include "LitematicaBE/ghost/Wireframe.h"
#include "LitematicaBE/hologram/ShapeDrawer.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"

#include <exception>
#include <string>
#include <utility>

namespace litematicabe::command {

/// 子命令；枚举成员名就是命令补全里的候选。
enum class LitAction : int {
    probe  = 0,
    send   = 1,
    batch  = 2,
    info   = 3,
    load   = 4,
    reload = 5,
    list   = 6,
    layer  = 7,
    unload = 8,
    resend = 9,
    rotate = 10,
    projects = 11,
    select   = 12,
    remove   = 13,
    place    = 14,
    replace  = 15,
    diag     = 16,
    plan     = 17,
    box      = 18,
};

struct LitParams {
    LitAction action;
};

struct LitFileParams {
    LitAction   action;
    std::string file;
};

/// `/lit load <file> <x> <y> <z>`：控制台也能指定原点铺设。
struct LitLoadAtParams {
    LitAction   action;
    std::string file;
    int         x;
    int         y;
    int         z;
};

/// `/lit layer <from> <to>`：显示层区间。
struct LitLayerParams {
    LitAction action;
    int       from;
    int       to;
};

/// `/lit layer <from>`：只看单层。
struct LitLayerOneParams {
    LitAction action;
    int       from;
};

namespace {

namespace util = litematicabe::util;

::Player* playerOf(::CommandOrigin const& origin) {
    auto* actor = origin.getEntity();
    if (actor == nullptr || !actor->isPlayer()) {
        return nullptr;
    }
    return static_cast<::Player*>(actor);
}

/// 命令回调里任何异常都不能冒到引擎：BDS 只会打印 `command threw an exception`，
/// 聊天栏什么都看不到，排查全靠日志。这里兜住并回报可读信息。
template <class Fn>
void guard(::CommandOutput& output, Fn&& body) noexcept {
    try {
        body();
    } catch (std::exception const& e) {
        output.error("§c内部错误§r：{}", util::sanitizeUtf8(e.what()));
    } catch (...) {
        output.error("§c内部错误§r：未知异常（详情见服务器日志）");
    }
}

void runWithoutFile(::CommandOutput& output, ::CommandOrigin const& origin, LitAction action) {
    switch (action) {
    case LitAction::probe:
        probe::printProbe(output);
        return;
    case LitAction::send:
    case LitAction::batch: {
        auto* player = playerOf(origin);
        if (player == nullptr) {
            output.error("该子命令需要以玩家身份执行");
            return;
        }
        if (action == LitAction::send) {
            probe::sendOne(output, *player);
        } else {
            probe::sendBatch(output, *player);
        }
        return;
    }
    case LitAction::info:
    case LitAction::load:
        output.error("该子命令需要文件参数：/lit {} <file>", action == LitAction::info ? "info" : "load");
        return;
    case LitAction::reload:
        schematic::reloadData(output);
        return;
    case LitAction::list:
        schematic::listFiles(output);
        return;
    case LitAction::projects:
        schematic::listProjects(output);
        return;
    case LitAction::diag:
        probe::diagnose(output);
        return;
    case LitAction::plan: {
        auto* session = ghost::ProjectionManager::getInstance().active();
        if (session == nullptr) {
            output.error("没有活动会话：先 /lit load <file> [x y z]");
            return;
        }
        auto const bounds = session->bounds();
        schematic::planProjection(output, bounds.from, bounds.to);
        return;
    }
    case LitAction::resend:
        schematic::resendAll(output);
        return;
    case LitAction::box: {
        auto& wireframe = ghost::Wireframe::getInstance();
        if (wireframe.enabled()) {
            wireframe.setEnabled(false);
            output.success("包围盒线框：§c关§r（已收回，/lit box 再打开）");
            return;
        }
        wireframe.setEnabled(true);
        auto& drawer = hologram::ShapeDrawer::getInstance();
        if (!drawer.available()) {
            output.error("包围盒线框：§c开§r，但形状通道不可用（{}）", drawer.reason());
            return;
        }
        ghost::ProjectionManager::getInstance().refreshWireframes();
        output.success("包围盒线框：§a开§r（形状通道：{}）", drawer.reason());
        return;
    }
    case LitAction::rotate:
        schematic::rotateProjection(output, playerOf(origin));
        return;
    case LitAction::place:
    case LitAction::replace: {
        auto* session = ghost::ProjectionManager::getInstance().active();
        if (session == nullptr) {
            output.error("没有活动会话：先 /lit load <file> [x y z]");
            return;
        }
        auto const bounds = session->bounds();
        schematic::placeProjection(
            output,
            playerOf(origin),
            bounds.from,
            bounds.to,
            action == LitAction::replace
        );
        return;
    }
    case LitAction::unload: {
        auto* player = playerOf(origin);
        if (player == nullptr) {
            schematic::reportSession(output);
            return;
        }
        schematic::unloadProjection(output, *player);
        return;
    }
    default:
        probe::printState(output);
        return;
    }
}

void runWithFile(
    ::CommandOutput&       output,
    ::CommandOrigin const& origin,
    LitAction              action,
    std::string const&     file
) {
    switch (action) {
    case LitAction::info:
        schematic::printInfo(output, file);
        return;
    case LitAction::send: {
        auto* player = playerOf(origin);
        if (player == nullptr) {
            output.error("该子命令需要以玩家身份执行");
            return;
        }
        // /lit send lit:g_stone 用来单独验证某个 ghost 方块能不能显示。
        probe::sendOne(output, *player, file);
        return;
    }
    case LitAction::load: {
        auto* player = playerOf(origin);
        if (player == nullptr) {
            output.error("控制台执行请指定原点：/lit load {} <x> <y> <z>", file);
            return;
        }
        schematic::loadProjection(output, *player, file);
        return;
    }
    default:
        output.error("该子命令不接受文件参数");
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
        guard(output, [&] { probe::printState(output); });
    });

    cmd.overload<LitParams>()
        .required("action")
        .execute([](::CommandOrigin const& origin, ::CommandOutput& output, LitParams const& params) {
            guard(output, [&] { runWithoutFile(output, origin, params.action); });
        });

    cmd.overload<LitFileParams>()
        .required("action")
        .required("file")
        .execute([](::CommandOrigin const& origin, ::CommandOutput& output, LitFileParams const& params) {
            guard(output, [&] { runWithFile(output, origin, params.action, params.file); });
        });

    cmd.overload<LitLayerParams>()
        .required("action")
        .required("from")
        .required("to")
        .execute([](::CommandOrigin const& origin, ::CommandOutput& output, LitLayerParams const& params) {
            guard(output, [&] {
                if (params.action == LitAction::place || params.action == LitAction::replace) {
                    schematic::placeProjection(
                        output,
                        playerOf(origin),
                        params.from,
                        params.to,
                        params.action == LitAction::replace
                    );
                    return;
                }
                if (params.action != LitAction::layer) {
                    output.error("只有 layer / place / replace 接受层号参数");
                    return;
                }
                auto* player = playerOf(origin);
                if (player == nullptr) {
                    // 控制台：先把层算好缓存起来，玩家进服后由轮询自动补发。
                    schematic::prepareLayers(output, params.from, params.to);
                    return;
                }
                schematic::switchLayers(output, *player, params.from, params.to);
            });
        });

    cmd.overload<LitLayerOneParams>()
        .required("action")
        .required("from")
        .execute([](::CommandOrigin const& origin, ::CommandOutput& output, LitLayerOneParams const& params) {
            guard(output, [&] {
                if (params.action == LitAction::select) {
                    schematic::selectProjection(output, params.from);
                    return;
                }
                if (params.action == LitAction::remove) {
                    schematic::removeProjection(output, params.from, playerOf(origin));
                    return;
                }
                if (params.action != LitAction::layer) {
                    output.error("该子命令不接受数字参数：/lit {} 用法见 /lit", params.action == LitAction::layer ? "layer" : "?");
                    return;
                }
                auto* player = playerOf(origin);
                if (player == nullptr) {
                    schematic::reportSession(output);
                    return;
                }
                schematic::switchLayers(output, *player, params.from, params.from);
            });
        });

    cmd.overload<LitLoadAtParams>()
        .required("action")
        .required("file")
        .required("x")
        .required("y")
        .required("z")
        .execute([](::CommandOrigin const& origin, ::CommandOutput& output, LitLoadAtParams const& params) {
            (void)origin;
            guard(output, [&] {
                if (params.action != LitAction::load) {
                    output.error("只有 load 支持坐标参数：/lit load <file> <x> <y> <z>");
                    return;
                }
                schematic::loadProjectionAt(output, params.file, params.x, params.y, params.z);
            });
        });
}

} // namespace litematicabe::command
