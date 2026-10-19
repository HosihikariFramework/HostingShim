#include "plugin.h"

#include <filesystem>
#include <memory>
#include <string>

#include "dotnet.h"
#include "loader.h"

namespace hosihikari::shim::endstone_carrier {
namespace {

/// 父级的日志出口：托管侧的每一行都从这里进 Endstone 的日志器（ADR-0007 的出口一侧）。
/// 等级由托管侧映射：0 调试、1 信息、2 警告、3 错误、4 致命。
///
/// 日志器是插件的实例成员，而这是个 C 回调 —— 所以在 onLoad 里把它记下来。
endstone::Logger* g_logger = nullptr;

void logBridge(void* /*user*/, int level, char const* utf8) {
    if (g_logger == nullptr) {
        return;
    }

    std::string const message = utf8 == nullptr ? std::string{} : std::string{utf8};
    switch (level) {
    case 1:
        g_logger->info(message);
        break;
    case 2:
        g_logger->warning(message);
        break;
    case 3:
    case 4:
        g_logger->error(message);
        break;
    default:
        g_logger->debug(message);
        break;
    }
}

/// 故障卸载后托管侧回叫这里。Endstone 的 PluginManager 没有单条摘除 API，
/// 只能记日志：父级账上会保留到重启（ADR-0030）。
void removalBridge(void* /*user*/, char const* nameUtf8) {
    if (g_logger == nullptr) {
        return;
    }
    g_logger->warning(
        "{} 已从托管侧移除；Endstone 不支持运行期摘除，父级账上会保留到重启",
        nameUtf8 == nullptr ? "（未知）" : nameUtf8
    );
}

/// 三个入口都住在同一个类型上，取法也一样。
constexpr auto kEntryType =
    CHARTEXT("Hosihikari.PluginManagement.NativeEntry, Hosihikari.PluginManager");

}  // namespace

void HosihikariPlugin::onLoad() {
    // 托管核心与 nethost.dll 都住在数据目录里（`plugins/hosihikari/`）——
    // 数据目录由父级按"插件名"给，不用自己去猜模块路径。
    auto const dataDirectory  = getDataFolder();
    auto const component      = dataDirectory / CHARTEXT("Hosihikari.PluginManager.dll");
    auto const runtimeConfig  = dataDirectory / CHARTEXT("Hosihikari.PluginManager.runtimeconfig.json");

    g_logger = &getLogger();

    getLogger().info("正在把 CLR 托管进 BDS 进程");
    component_entry_point_fn start = nullptr;
    if (auto const code = hosihikari::shim::StartRuntime(
            runtimeConfig.c_str(),
            component.c_str(),
            kEntryType,
            // 父级态入口：插件集归 Endstone，我们不扫目录（ADR-0003）。
            CHARTEXT("StartUnderParent"),
            &start
        );
        code != 0 || start == nullptr) {
        getLogger().error("启动序列失败：0x{:08X}", static_cast<unsigned>(code));
        return;
    }

    auto const fetch = [&](char_t const *method, component_entry_point_fn *slot) {
        return hosihikari::shim::GetManagedEntry(component.c_str(), kEntryType, method, slot);
    };

    // 日志出口要在托管入口跑之前交过去。
    component_entry_point_fn setLogSink = nullptr;
    if (fetch(CHARTEXT("SetLogSink"), &setLogSink) == 0 && setLogSink != nullptr) {
        struct LogSink {
            void (*callback)(void*, int, char const*);
            void* user;
        };
        LogSink sink{&logBridge, nullptr};
        setLogSink(reinterpret_cast<char*>(&sink), sizeof(sink));
    }

    if (auto const managed = start(nullptr, 0); managed != 0) {
        getLogger().error("托管入口返回 {}", managed);
        return;
    }

    // 父级对子插件能做的四件事各要一个入口（ADR-0003）。
    ManagedPluginEntries entries{};
    if (fetch(CHARTEXT("LoadPlugin"), &entries.load) != 0 || fetch(CHARTEXT("UnloadPlugin"), &entries.unload) != 0
        || fetch(CHARTEXT("EnablePlugin"), &entries.enable) != 0
        || fetch(CHARTEXT("DisablePlugin"), &entries.disable) != 0) {
        getLogger().error("取不到插件管理入口");
        return;
    }

    auto &manager = getServer().getPluginManager();
    manager.registerLoader(std::make_unique<HosihikariPluginLoader>(getServer(), entries));

    // 故障卸载成功后托管侧回叫这里（ADR-0030）。Endstone 没有单条摘除 API，只记日志。
    component_entry_point_fn setRemovalSink = nullptr;
    if (fetch(CHARTEXT("SetRemovalSink"), &setRemovalSink) == 0 && setRemovalSink != nullptr) {
        struct RemovalSink {
            void (*callback)(void*, char const*);
            void* user;
        };
        RemovalSink sink{&removalBridge, nullptr};
        setRemovalSink(reinterpret_cast<char*>(&sink), sizeof(sink));
    }

    // 父级已经收完了这一轮的候选（我们是它加载的插件之一），所以新 loader 不会被它回头再问。
    // 这里主动发起一轮只包含我们这些包的加载：按文件名解析会落到刚注册的 loader 上，
    // 它返回托管插件，再由父级自己的依赖图排序、装载。
    auto const packages = collectPackageDirectories(dataDirectory.parent_path());
    if (packages.empty()) {
        getLogger().info("已接管：CLR 就绪，尚无插件");
        return;
    }

    auto const loaded = manager.loadPlugins(packages);
    getLogger().info("已接管：CLR 就绪，{} 个插件", loaded.size());
}

void HosihikariPlugin::onEnable() {
    // 不用做事：托管核心在 onLoad 里就起来了，子插件的启停由父级按自己的节奏驱动。
}

void HosihikariPlugin::onDisable() {
    // ADR-0024：停用退不回去 —— CLR 已经在进程里。但 Endstone 的 onDisable 没有失败通道
    // （`void`，不像 LeviLamina 那样能拒绝），所以只能把理由写进日志。
    getLogger().warning(
        "框架停用做不到：CLR 已经托管进这个进程，.NET 不卸载运行时。要停框架请停用它的插件，要移除它请重启服务端"
    );
}

}  // namespace hosihikari::shim::endstone_carrier

ENDSTONE_PLUGIN("hosihikari", "0.2.2", hosihikari::shim::endstone_carrier::HosihikariPlugin)
{
    prefix      = "Hosihikari";
    description = "把 CLR 托管进 BDS，并把插件管理交给星光框架核心";
    authors     = {"星光"};
}
