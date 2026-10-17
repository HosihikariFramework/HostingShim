#include "mod_manager.h"

#include <filesystem>
#include <memory>
#include <string>

#include "ll/api/mod/Mod.h"
#include "ll/api/service/GamingStatus.h"

namespace hosihikari::shim::levilamina {
namespace {

/// 越过这条缝的字符串一律 UTF-8、以 null 结尾（ADR-0005）。
int deliver(component_entry_point_fn entry, std::string const& text) {
    return entry(const_cast<char*>(text.c_str()), static_cast<int>(text.size() + 1));
}

/// 交给托管侧的是**包目录** —— 清单只有一个读者，由它自己读（ADR-0003）。
///
/// 必须做成绝对路径：父级的 mods 根是相对的（`./plugins`），而托管侧把它交给
/// McMaster 之类按绝对路径办事的库时会直接抛异常。这类账已经在基目录上还过一次。
std::string packageDirectory(std::string const& name) {
    // 名字是 UTF-8；path 用 u8 构造，别让它在窄字符那步按系统代码页转。
    auto const path = std::filesystem::absolute(ll::mod::getModsRoot() / ll::string_utils::sv2u8sv(name));
    auto const utf8 = path.u8string();
    return std::string{utf8.begin(), utf8.end()};
}

}  // namespace

HosihikariModManager::HosihikariModManager(ManagedPluginEntries entries)
: ll::mod::ModManager("hosihikari"),
  mEntries(entries) {}

ll::Expected<> HosihikariModManager::load(ll::mod::Manifest manifest) {
    // 到这一步 LeviLamina 已经把清单读出来、按 type 路由到我们了 ——
    // 顺序与依赖也由它的计划排过（插件声明了对我们的依赖，ADR-0006）。
    std::string const name = manifest.name;
    if (deliver(mEntries.load, packageDirectory(name)) != 0) {
        return ll::makeStringError("hosihikari: 装载 " + name + " 失败，原因已记进日志");
    }

    // 登记一个父级自己的 Mod 对象。少了这一步，父级那边 `getMod(name)` 是空指针 ——
    // 它的 `enable`/`disable` 直接解引用、`show` 之类的读也读不到（实测会 segfault）。
    // 启停回调转发给托管侧：那是插件作者写的 OnEnable / OnDisable。
    auto mod = std::make_shared<ll::mod::Mod>(std::move(manifest));
    mod->onEnable([this, name](ll::mod::Mod&) { return deliver(mEntries.enable, name) == 0; });
    mod->onDisable([this, name](ll::mod::Mod&) { return deliver(mEntries.disable, name) == 0; });
    addMod(name, mod);

    // 父级对原生 mod 就是这么做的（`Mod::onLoad`）：装载时服务器已经在跑就顺手启用。
    // 少了这一步，`ll load` 会留下一个"装了但没跑"的插件，而操作者刚看到的是
    // "Load mod X successfully"。启动阶段这里不启用 —— 父级自己的启用阶段随后会做。
    if (ll::getGamingStatus() == ll::GamingStatus::Running) {
        enable(name);
    }

    return {};
}

void HosihikariModManager::removeFromParent(std::string_view name) { eraseMod(name); }

ll::Expected<> HosihikariModManager::unload(std::string_view name) {
    // 托管侧的答案只有两种：回收验证过了，或者放下了但还没通过。后者如实报失败 ——
    // 它确实还没卸干净，而再问一次就是重新验证（ADR-0004）。
    if (deliver(mEntries.unload, std::string{name}) != 0) {
        return ll::makeStringError("hosihikari: " + std::string{name} + " 已经放下，但回收验证还没通过");
    }

    // 确认回收之后才从父级的账上摘掉 —— 摘早了，父级就看不见一个其实还在的插件。
    eraseMod(name);
    return {};
}

}  // namespace hosihikari::shim::levilamina
