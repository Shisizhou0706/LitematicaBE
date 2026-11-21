#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace litematicabe::web {

/// 内置的投影上传服务（HTTP/1.1，最小实现）。
///
/// 目的只有一件事：不碰服务器文件系统也能把 `.litematic` 放进 `plugins/LitematicaBE/schematics/`。
/// 上传成功后**不会自动加载**，玩家需要执行 `/lit load <file>`（避免误把大建筑铺到世界里）。
///
/// 配置：`<模组目录>/config/upload.json`
/// ```json
/// { "enabled": true, "bind": "127.0.0.1", "port": 8686, "token": "", "maxSizeMb": 64 }
/// ```
/// `token` 为空时启动会随机生成并写回文件（日志里也会打印）。
/// 默认只监听 `127.0.0.1`：要远程上传请自己改 `bind` 并确认防火墙/端口转发，token 是唯一的鉴权。
class UploadServer {
public:
    static UploadServer& getInstance();

    /// 读配置并启动；返回 false 表示未启用或启动失败（原因写在 lastError）。
    bool start();

    /// 停止监听并回收线程。
    void stop();

    [[nodiscard]] bool running() const noexcept { return mRunning.load(); }

    [[nodiscard]] std::string const& url() const noexcept { return mUrl; }
    [[nodiscard]] std::string const& token() const noexcept { return mToken; }
    [[nodiscard]] std::string const& lastError() const noexcept { return mLastError; }

    struct Stats {
        std::uint64_t accepted{};
        std::uint64_t rejected{};
        std::uint64_t bytes{};
    };

    [[nodiscard]] Stats stats() const;

private:
    UploadServer() = default;
    ~UploadServer();

    void        serve();
    void        handleClient(std::uintptr_t client);
    std::string schematicsDir() const;

    std::atomic<bool> mRunning{false};
    std::atomic<bool> mStop{false};

    std::uintptr_t mListen{static_cast<std::uintptr_t>(~0ull)};
    std::thread    mThread;
    std::string    mUrl;
    std::string    mToken;
    std::string    mBind;
    std::uint16_t  mPort{};
    std::size_t    mMaxBytes{};

    mutable std::mutex mMutex;
    std::string        mLastError;

    std::atomic<std::uint64_t> mAccepted{0};
    std::atomic<std::uint64_t> mRejected{0};
    std::atomic<std::uint64_t> mBytes{0};
};

} // namespace litematicabe::web
