#include "LitematicaBE/LitematicaBE.h"

#include "LitematicaBE/Version.h"
#include "LitematicaBE/command/ProbeCommand.h"
#include "LitematicaBE/hologram/HologramFacade.h"

#include "ll/api/mod/RegisterHelper.h"

#include <filesystem>
#include <system_error>

namespace litematicabe {

LitematicaBE& LitematicaBE::getInstance() {
    static LitematicaBE instance;
    return instance;
}

bool LitematicaBE::load() {
    auto& self   = getSelf();
    auto& logger = self.getLogger();

    // 日志一律用英文：Windows 控制台默认 GBK 代码页，中文日志会变成乱码。
    // 面向玩家的命令回显仍然用中文（游戏内是 UTF-8，显示正常）。
    logger.info("LitematicaBE {} loading (target baseline: {})", kVersion, kTargetBaseline);

    // 目录准备：数据（解析缓存 / 投影会话）、配置、日志都落在模组目录下，
    // 卸载或重装插件时不会牵连世界存档。
    std::error_code ec;
    std::filesystem::create_directories(self.getDataDir(), ec);
    if (ec) {
        logger.error("failed to create data dir: {} ({})", self.getDataDir().string(), ec.message());
        return false;
    }
    std::filesystem::create_directories(self.getConfigDir(), ec);
    if (ec) {
        logger.error("failed to create config dir: {} ({})", self.getConfigDir().string(), ec.message());
        return false;
    }

    logger.info("LitematicaBE loaded");
    return true;
}

bool LitematicaBE::enable() {
    auto& logger = getSelf().getLogger();

    auto& hologram    = hologram::HologramFacade::getInstance();
    auto  displayPath = hologram.probe();
    if (displayPath == hologram::DisplayPath::None) {
        // 显示通道不可用不是致命错误：解析、投影管理、命令仍然可用，
        // 只是无法把假方块推给客户端。
        logger.warn("LitematicaBE enabled, display path UNAVAILABLE: {}", hologram.describe());
    } else {
        logger.info(
            "LitematicaBE enabled, display path {}: {}",
            hologram::toString(displayPath),
            hologram.describe()
        );
    }

    command::registerLitCommand();
    logger.info("command /lit registered (probe / send / batch)");
    return true;
}

bool LitematicaBE::disable() {
    getSelf().getLogger().info("LitematicaBE disabled");
    return true;
}

} // namespace litematicabe

LL_REGISTER_MOD(litematicabe::LitematicaBE, litematicabe::LitematicaBE::getInstance());
