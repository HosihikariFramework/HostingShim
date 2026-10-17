#include "loader.h"

#include <fstream>
#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

#include "endstone/plugin/plugin.h"

namespace hosihikari::shim::endstone_carrier {
namespace {

constexpr char kManifestName[] = "manifest.json";
constexpr char kRoutingType[]  = "hosihikari";

/// 父级给来的字符串是 UTF-8；Windows 上 `fs::path(std::string)` 会按系统代码页解释，
/// 所以显式走 u8。
std::filesystem::path utf8Path(std::string const &text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string utf8String(std::filesystem::path const &path) {
    auto const utf8 = path.u8string();
    return std::string{utf8.begin(), utf8.end()};
}

std::optional<nlohmann::json> readManifest(std::filesystem::path const &directory) {
    auto const    path = directory / kManifestName;
    std::ifstream stream(path);
    if (!stream) {
        return std::nullopt;
    }

    auto json = nlohmann::json::parse(stream, nullptr, false);
    if (json.is_discarded()) {
        return std::nullopt;
    }

    return json;
}

bool isOurPackage(std::filesystem::path const &directory) {
    auto const manifest = readManifest(directory);
    return manifest && manifest->value("type", std::string{}) == kRoutingType;
}

/// Endstone 只接受 `^[a-z0-9_]+$`，且名字不能以 `endstone` 开头。托管侧仍按清单里的名字
/// 记账（那是插件自己的身份），这里只换**父级看得见**的那个名字。
std::string endstoneName(std::string const &name) {
    std::string result;
    result.reserve(name.size());
    for (char const c : name) {
        if (c >= 'A' && c <= 'Z') {
            result.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            result.push_back(c);
        } else {
            result.push_back('_');
        }
    }

    if (result.empty()) {
        result = "plugin";
    }
    if (result.starts_with("endstone")) {
        result = "hosihikari_" + result;
    }

    return result;
}

}  // namespace

std::vector<std::string> collectPackageDirectories(std::filesystem::path const &pluginsDirectory) {
    std::vector<std::string> packages;

    std::error_code ec;
    for (auto const &entry : std::filesystem::directory_iterator(pluginsDirectory, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (isOurPackage(entry.path())) {
            packages.push_back(utf8String(entry.path()));
        }
    }

    return packages;
}

HosihikariPluginLoader::HosihikariPluginLoader(endstone::Server &server, ManagedPluginEntries entries)
: endstone::PluginLoader(server),
  entries_(entries) {}

endstone::Plugin *HosihikariPluginLoader::loadPlugin(std::string file) {
    // file 是**包目录**，不是文件：我们的包是目录，清单在里面（ADR-0006）。
    auto const directory = utf8Path(file);
    auto const manifest  = readManifest(directory);
    if (!manifest) {
        server_.getLogger().error("读不了 {} 下的清单，跳过", file);
        return nullptr;
    }

    auto const name    = manifest->value("name", std::string{});
    auto const version = manifest->value("version", std::string{"0.0.0"});
    if (name.empty()) {
        server_.getLogger().error("{} 的清单缺少 name，跳过", file);
        return nullptr;
    }

    endstone::detail::PluginDescriptionBuilder builder;
    builder.description = manifest->value("description", std::string{"Hosihikari 插件"});
    builder.prefix      = name;

    auto const author = manifest->value("author", std::string{});
    if (!author.empty()) {
        builder.authors = {author};
    }

    for (auto const &dependency : manifest->value("dependencies", nlohmann::json::array())) {
        auto dependencyName = dependency.value("name", std::string{});
        // 对框架自身的依赖在这个载体上不成立：父级在调我们之前就已经把它装进来了，
        // 而嵌套的这一轮 loadPlugins 只看本轮候选，"已加载"不算数 —— 留着会被判成
        // 未知依赖，整个插件被剔掉。顺序由"我们在父级的 onLoad 里才发起这一轮"保证。
        if (!dependencyName.empty() && dependencyName != kRoutingType) {
            builder.depend.push_back(std::move(dependencyName));
        }
    }

    auto plugin = std::make_unique<ManagedPlugin>(name, builder.build(endstoneName(name), version), entries_);
    plugin->setPackageDirectory(file);

    return plugins_.emplace_back(std::move(plugin)).get();
}

std::vector<endstone::Plugin *> HosihikariPluginLoader::loadPlugins(std::string directory) {
    std::vector<endstone::Plugin *> loaded;
    for (auto const &package : collectPackageDirectories(utf8Path(directory))) {
        if (auto *plugin = loadPlugin(package)) {
            loaded.push_back(plugin);
        }
    }
    return loaded;
}

std::vector<std::string> HosihikariPluginLoader::getPluginFileFilters() const {
    // 只在父级按文件名解析 loader 时用得上（`loadPlugins(vector<string>)`）。
    // 匹配"最后一段里没有点的路径" —— 也就是我们的包目录，而不是任何文件。
    return {"[/\\\\][^./\\\\]+$"};
}

}  // namespace hosihikari::shim::endstone_carrier
