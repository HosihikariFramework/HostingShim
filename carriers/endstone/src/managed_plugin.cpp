#include "managed_plugin.h"

#include <stdexcept>
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
    // Endstone 在 onEnable 前就 setEnabled(true)，返回而不抛会让父级以为它装上了；
    // 抛出去才是 Endstone 认识的加载失败。
    if (packageDirectory_.empty() || !deliver(entries_.load, packageDirectory_)) {
        getLogger().error("加载插件 {} 失败，原因已记进日志", name_);
        throw std::runtime_error("managed plugin load failed");
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
