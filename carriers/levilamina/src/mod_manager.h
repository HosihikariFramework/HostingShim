#pragma once

#include <string>
#include <string_view>

#include "coreclr_delegates.h"

#include "ll/api/Expected.h"
#include "ll/api/mod/Manifest.h"
#include "ll/api/mod/ModManager.h"

namespace hosihikari::shim::levilamina {

/// 托管侧交给载体的那几个入口。取法都一样，装在一起传，
/// 免得构造函数变成一长串同类型的指针。
struct ManagedPluginEntries {
    component_entry_point_fn load;
    component_entry_point_fn unload;
    component_entry_point_fn enable;
    component_entry_point_fn disable;
};

/// 把本框架的插件集交给 LeviLamina 的注册表（ADR-0003）：
/// 每个子插件以 LeviLamina mod 的身份存在、由 LeviLamina 调度，我们只做协议转换与后端。
///
/// 它之所以能被路由到，是因为 LeviLamina 扫到插件包的 manifest 后按 `type` 找管理器 ——
/// 而插件声明了对本 mod 的依赖，所以计划会先排我们（ADR-0006）。
class HosihikariModManager : public ll::mod::ModManager {
public:
    explicit HosihikariModManager(ManagedPluginEntries entries);

    /// <summary>故障卸载后由托管侧回调：把父级账上的那一条摘掉（ADR-0030）。</summary>
    void removeFromParent(std::string_view name);

private:
    ll::Expected<> load(ll::mod::Manifest manifest) override;
    ll::Expected<> unload(std::string_view name) override;

    ManagedPluginEntries mEntries;
};

}  // namespace hosihikari::shim::levilamina
