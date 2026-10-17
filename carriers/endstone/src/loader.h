#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "endstone/plugin/plugin_loader.h"

#include "managed_plugin.h"

namespace hosihikari::shim::endstone_carrier {

/// 本框架的包目录：`plugins/` 下凡是有 `manifest.json` 且 `type` 为 `hosihikari` 的那些
/// （ADR-0006）。目录名就是插件名。
std::vector<std::string> collectPackageDirectories(std::filesystem::path const &pluginsDirectory);

/// 把托管插件交给 Endstone 的注册表（ADR-0003）：每个子插件以 Endstone 插件的身份存在、
/// 由 Endstone 调度，我们只做协议转换与后端。
///
/// 与 LeviLamina 载体的一处结构差异：Endstone 的默认 `loadPlugins` 只认**文件**，
/// 而我们的包是目录，所以这里把它整个覆盖掉。
class HosihikariPluginLoader final : public endstone::PluginLoader {
public:
    HosihikariPluginLoader(endstone::Server &server, ManagedPluginEntries entries);

    [[nodiscard]] endstone::Plugin *loadPlugin(std::string file) override;
    [[nodiscard]] std::vector<endstone::Plugin *> loadPlugins(std::string directory) override;
    [[nodiscard]] std::vector<std::string> getPluginFileFilters() const override;

private:
    ManagedPluginEntries entries_;

    /// 与 Endstone 自己的 CppPluginLoader 一样，由 loader 持有插件对象的生命周期。
    std::vector<std::unique_ptr<ManagedPlugin>> plugins_;
};

}  // namespace hosihikari::shim::endstone_carrier
