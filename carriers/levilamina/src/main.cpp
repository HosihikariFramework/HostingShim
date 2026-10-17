// 载体目标：LeviLamina（ADR-0005 的第一个目标）。
//
// 它做三件事：
//   1. 把 CLR 托管进 BDS 进程 —— 与其它载体共用 common/ 的启动序列，不各写一份；
//   2. 把自己注册成 LeviLamina 的一个插件管理器（ADR-0003），此后子插件由父级调度；
//   3. 拒绝父级停用或卸载自己（ADR-0024）。
//
// 入口是 LeviLamina 的 `ll_mod_load` 导出，由它调用 —— 不是在 DllMain 里做事（ADR-0016）。

// LeviLamina 只加载"统一内存分配"的 mod：它要能在自己这边释放 mod 分配的内存。
// 代价就是这个宏加一次包含 —— 头里替换掉本 dll 的全局 new/delete，并导出标记符号
// ll_memory_operator_overrided，父级扫的正是它。整个 dll 只能有一个源文件这么做。
#define LL_MEMORY_OPERATORS
#include "ll/api/memory/MemoryOperators.h"

#include <memory>
#include <string>

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include "dotnet.h"
#include "mod_manager.h"

namespace hosihikari::shim::levilamina {
namespace {

class ShimMod {
public:
    bool load();
    bool disable();
    bool unload();
};

ShimMod& getShim() {
    static ShimMod instance;
    return instance;
}

/// 父级的日志出口：托管侧的每一行都从这里进父级的日志器（ADR-0007 的出口一侧）。
/// 等级由托管侧映射：0 调试、1 信息、2 警告、3 错误、4 致命。
void logBridge(void* /*user*/, int level, char const* utf8) {
    auto&             logger  = ll::mod::NativeMod::current()->getLogger();
    std::string const message = utf8 == nullptr ? std::string{} : std::string{utf8};

    switch (level) {
    case 1:
        logger.info(message);
        break;
    case 2:
        logger.warn(message);
        break;
    case 3:
    case 4:
        logger.error(message);
        break;
    default:
        logger.debug(message);
        break;
    }
}

/// 故障卸载后托管侧回叫这里：把父级账上的那一条摘掉（ADR-0030）。
/// user 是 `HosihikariModManager*`，名字是 UTF-8、以 null 结尾。
void removalBridge(void* user, char const* nameUtf8) {
    auto* manager = static_cast<HosihikariModManager*>(user);
    if (manager == nullptr || nameUtf8 == nullptr) {
        return;
    }
    manager->removeFromParent(nameUtf8);
}

/// 停用与卸载共用同一条理由。父级把失败原样报给操作者（"The mod cannot be disabled/
/// unloaded"），原因得由这里补上，否则操作者只看到"不行"。
bool refuseOffline() {
    ll::mod::NativeMod::current()->getLogger().warn(std::string{
        "框架停用或卸载都做不到：CLR 已经托管进这个进程，.NET 不卸载运行时；"
        "而放掉本 dll 会让父级注册表里的插件管理器指向已卸载的代码。"
        "要停框架请卸载它的插件，要移除它请重启服务端"
    });
    return false;
}

}  // namespace
}  // namespace hosihikari::shim::levilamina

namespace hosihikari::shim::levilamina {

bool ShimMod::load() {
    // 本模组的身份由父级给：目录、日志都从它那里取，不自己拿 GetModuleFileName 猜。
    const std::shared_ptr<ll::mod::NativeMod> self = ll::mod::NativeMod::current();
    if (self == nullptr) {
        std::printf("[Hosihikari] 拿不到本模组，加载中止\n");
        return false;
    }
    auto const& logger   = self->getLogger();
    auto const  dir      = self->getModDir();
    auto const  assembly = dir / L"Hosihikari.PluginManager.dll";
    // 三个入口都住在同一个类型上，取法也一样。
    constexpr auto entryType = L"Hosihikari.PluginManagement.NativeEntry, Hosihikari.PluginManager";

    logger.info("正在把 CLR 托管进 BDS 进程");
    component_entry_point_fn start = nullptr;
    const int                code  = hosihikari::shim::StartRuntime(
        (dir / L"Hosihikari.PluginManager.runtimeconfig.json").c_str(),
        assembly.c_str(),
        entryType,
        // 父级态入口：插件由 LeviLamina 按 type 路由过来，我们不扫 plugins/（ADR-0003）。
        L"StartUnderParent",
        &start
    );
    if (code != 0 || start == nullptr) {
        logger.error("启动序列失败：0x{:08X}", static_cast<unsigned>(code));
        return false;
    }

    auto const fetch = [&](wchar_t const* method, component_entry_point_fn* slot) {
        return hosihikari::shim::GetManagedEntry(assembly.c_str(), entryType, method, slot);
    };

    // 日志出口要在托管入口跑之前交过去 —— 否则启动那几行还落在它自己的控制台上。
    component_entry_point_fn setLogSink = nullptr;
    if (fetch(L"SetLogSink", &setLogSink) == 0 && setLogSink != nullptr) {
        struct LogSink {
            void (*callback)(void*, int, char const*);
            void* user;
        };
        LogSink sink{&logBridge, nullptr};
        setLogSink(reinterpret_cast<char*>(&sink), sizeof(sink));
    }

    if (const int managed = start(nullptr, 0); managed != 0) {
        logger.error("托管入口返回 {}", managed);
        return false;
    }

    // 父级对子插件能做的四件事各要一个入口（ADR-0003）：交付、收回、启用、停用。
    ManagedPluginEntries entries{};
    if (fetch(L"LoadPlugin", &entries.load) != 0 || fetch(L"UnloadPlugin", &entries.unload) != 0
        || fetch(L"EnablePlugin", &entries.enable) != 0 || fetch(L"DisablePlugin", &entries.disable) != 0) {
        logger.error("取不到插件管理入口");
        return false;
    }

    // 注册进父级注册表 —— 从这里起，本框架的插件由 LeviLamina 调度（ADR-0003）。
    auto manager = std::make_shared<HosihikariModManager>(entries);
    if (!ll::mod::ModManagerRegistry::getInstance().addManager(manager)) {
        logger.error("注册插件管理器失败");
        return false;
    }

    // 故障卸载成功后托管侧回叫这里，把父级账上的那一条摘掉（ADR-0030）。
    component_entry_point_fn setRemovalSink = nullptr;
    if (fetch(L"SetRemovalSink", &setRemovalSink) == 0 && setRemovalSink != nullptr) {
        struct RemovalSink {
            void (*callback)(void*, char const*);
            void* user;
        };
        RemovalSink sink{&removalBridge, manager.get()};
        setRemovalSink(reinterpret_cast<char*>(&sink), sizeof(sink));
    }

    logger.info("已接管：CLR 就绪，插件管理器已注册");
    return true;
}

bool ShimMod::disable() { return refuseOffline(); }

bool ShimMod::unload() { return refuseOffline(); }

}  // namespace hosihikari::shim::levilamina

LL_REGISTER_MOD(hosihikari::shim::levilamina::ShimMod, hosihikari::shim::levilamina::getShim());
