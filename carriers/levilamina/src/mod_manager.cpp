#include "mod_manager.h"

namespace hosihikari::shim::levilamina {

HosihikariModManager::HosihikariModManager() : ll::mod::ModManager("hosihikari") {}

ll::Expected<> HosihikariModManager::load(ll::mod::Manifest manifest) {
    // 到这一步 LeviLamina 已经把插件包的清单读出来、按 type 路由到我们了 ——
    // 也就是说"注册进父级注册表"这条链已经通了（ADR-0003）。
    // 把包交给托管侧真正装载，是下一刀的事。
    return ll::makeStringError("hosihikari: 装载尚未接到托管侧");
}

ll::Expected<> HosihikariModManager::unload(std::string_view name) {
    return ll::makeStringError("hosihikari: 卸载尚未接到托管侧");
}

}  // namespace hosihikari::shim::levilamina
