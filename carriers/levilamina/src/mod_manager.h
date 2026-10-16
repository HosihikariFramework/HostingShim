#pragma once

#include <memory>
#include <string_view>

#include "ll/api/Expected.h"
#include "ll/api/mod/Manifest.h"
#include "ll/api/mod/ModManager.h"

namespace hosihikari::shim::levilamina {

/// 把本框架的插件集交给 LeviLamina 的注册表（ADR-0003）：
/// 每个子插件以 LeviLamina mod 的身份存在、由 LeviLamina 调度，我们只做协议转换与后端。
///
/// 它之所以能被路由到，是因为 LeviLamina 扫到插件包的 manifest 后按 `type` 找管理器 ——
/// 而插件声明了对本 mod 的依赖，所以计划会先排我们（ADR-0006）。
class HosihikariModManager : public ll::mod::ModManager {
public:
    HosihikariModManager();

private:
    ll::Expected<> load(ll::mod::Manifest manifest) override;
    ll::Expected<> unload(std::string_view name) override;
};

}  // namespace hosihikari::shim::levilamina
