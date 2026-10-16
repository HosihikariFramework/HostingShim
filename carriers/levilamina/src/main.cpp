// 载体目标：LeviLamina（ADR-0005 的第一个目标）。
//
// 它做两件事：
//   1. 把 CLR 托管进 BDS 进程 —— 与其它载体共用 common/ 的启动序列，不各写一份；
//   2. 把自己注册成 LeviLamina 的一个插件管理器（ADR-0003），此后子插件由父级调度。
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
};

ShimMod& getShim() {
    static ShimMod instance;
    return instance;
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
    auto const& logger = self->getLogger();
    auto const  dir    = self->getModDir();

    logger.info("正在把 CLR 托管进 BDS 进程");
    component_entry_point_fn entry = nullptr;
    const int                code  = hosihikari::shim::StartRuntime(
        (dir / L"Hosihikari.PluginManager.runtimeconfig.json").c_str(),
        (dir / L"Hosihikari.PluginManager.dll").c_str(),
        L"Hosihikari.PluginManagement.NativeEntry, Hosihikari.PluginManager",
        // 父级态入口：插件由 LeviLamina 按 type 路由过来，我们不扫 plugins/（ADR-0003）。
        L"StartUnderParent",
        &entry
    );
    if (code != 0 || entry == nullptr) {
        logger.error("启动序列失败：0x{:08X}", static_cast<unsigned>(code));
        return false;
    }

    if (const int managed = entry(nullptr, 0); managed != 0) {
        logger.error("托管入口返回 {}", managed);
        return false;
    }

    // 注册进父级注册表 —— 从这里起，本框架的插件由 LeviLamina 调度（ADR-0003）。
    if (!ll::mod::ModManagerRegistry::getInstance().addManager(std::make_shared<HosihikariModManager>())) {
        logger.error("注册插件管理器失败");
        return false;
    }

    logger.info("已接管：CLR 就绪，插件管理器已注册");
    return true;
}

}  // namespace hosihikari::shim::levilamina

LL_REGISTER_MOD(hosihikari::shim::levilamina::ShimMod, hosihikari::shim::levilamina::getShim());
