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

    if (const int managed = start(nullptr, 0); managed != 0) {
        logger.error("托管入口返回 {}", managed);
        return false;
    }

    // 父级交付插件、父级收回插件 —— 各要一个入口（ADR-0003）。
    component_entry_point_fn loadPlugin   = nullptr;
    component_entry_point_fn unloadPlugin = nullptr;
    auto const fetch = [&](wchar_t const* method, component_entry_point_fn* slot) {
        return hosihikari::shim::GetManagedEntry(assembly.c_str(), entryType, method, slot);
    };
    if (fetch(L"LoadPlugin", &loadPlugin) != 0 || fetch(L"UnloadPlugin", &unloadPlugin) != 0) {
        logger.error("取不到插件交付入口");
        return false;
    }

    // 注册进父级注册表 —— 从这里起，本框架的插件由 LeviLamina 调度（ADR-0003）。
    if (!ll::mod::ModManagerRegistry::getInstance().addManager(
            std::make_shared<HosihikariModManager>(loadPlugin, unloadPlugin)
        )) {
        logger.error("注册插件管理器失败");
        return false;
    }

    logger.info("已接管：CLR 就绪，插件管理器已注册");
    return true;
}

bool ShimMod::disable() { return refuseOffline(); }

bool ShimMod::unload() { return refuseOffline(); }

}  // namespace hosihikari::shim::levilamina

LL_REGISTER_MOD(hosihikari::shim::levilamina::ShimMod, hosihikari::shim::levilamina::getShim());
