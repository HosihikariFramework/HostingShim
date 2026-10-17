#include "managed_plugin.h"

#include <utility>

namespace hosihikari::shim::endstone_carrier {
namespace {

/// 越过这条缝的字符串一律 UTF-8、以 null 结尾（ADR-0005）。
bool deliver(component_entry_point_fn entry, std::string const& text) {
    return entry(const_cast<char *>(text.c_str()), static_cast<int>(text.size() + 1)) == 0;
}

}  // namespace

ManagedPlugin::ManagedPlugin(std::string name, endstone::PluginDescription description, ManagedPluginEntries entries)
: name_(std::move(name)),
  description_(std::move(description)),
  entries_(entries) {}

const endstone::PluginDescription &ManagedPlugin::getDescription() const { return description_; }

void ManagedPlugin::onLoad() {
    // 真正装进 ALC 的是托管核心；这里只把包目录交给它（ADR-0003）。
    // 失败只让这一个插件倒下：父级看到的是它没被启用，其余照常（ADR-0009）。
    if (packageDirectory_.empty() || !deliver(entries_.load, packageDirectory_)) {
        getLogger().error("加载插件 {} 失败，原因已记进日志", name_);
        return;
    }

    loaded_ = true;
}

void ManagedPlugin::onEnable() {
    if (loaded_) {
        deliver(entries_.enable, name_);
    }
}

void ManagedPlugin::onDisable() {
    if (loaded_) {
        deliver(entries_.disable, name_);
    }
}

}  // namespace hosihikari::shim::endstone_carrier
