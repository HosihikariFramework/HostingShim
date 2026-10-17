#pragma once

#include <string>

#include "endstone/plugin/plugin.h"

#include "dotnet.h"

namespace hosihikari::shim::endstone_carrier {

/// 托管侧那几个入口。取法都一样，装在一起传（与 LeviLamina 载体同一个形状）。
struct ManagedPluginEntries {
    component_entry_point_fn load;
    component_entry_point_fn unload;
    component_entry_point_fn enable;
    component_entry_point_fn disable;
};

/// 一个托管插件在父级眼里的样子（ADR-0003）：以 Endstone 插件的身份存在，
/// 启停钩子转发给托管侧。它自己不加载任何东西 —— 装载由托管核心的 ALC 做，
/// 这个对象只负责让父级看得见、点得动。
class ManagedPlugin final : public endstone::Plugin {
public:
    ManagedPlugin(std::string name, endstone::PluginDescription description, ManagedPluginEntries entries);

    [[nodiscard]] const endstone::PluginDescription &getDescription() const override;

    void onLoad() override;
    void onEnable() override;
    void onDisable() override;

    /// 交给托管侧的是**包目录** —— 清单只有一个读者（ADR-0003）。
    void setPackageDirectory(std::string directory) { packageDirectory_ = std::move(directory); }

private:
    std::string               name_;
    endstone::PluginDescription description_;
    ManagedPluginEntries      entries_;
    std::string               packageDirectory_;
    bool                      loaded_ = false;
};

}  // namespace hosihikari::shim::endstone_carrier
