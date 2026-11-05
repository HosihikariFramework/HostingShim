#include "loader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <utility>

#include "endstone/plugin/plugin.h"

namespace hosihikari::shim::endstone_carrier
{
namespace
{

constexpr char kManifestName[] = "manifest.json";
constexpr char kRoutingType[] = "hosihikari";

/// 父级给来的字符串是 UTF-8；Windows 上 `fs::path(std::string)` 会按系统代码页解释，
/// 所以显式走 u8。
std::filesystem::path utf8Path(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string utf8String(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string { utf8.begin(), utf8.end() };
}

std::optional<nlohmann::json> readManifest(const std::filesystem::path& directory)
{
    const auto path = directory / kManifestName;
    std::ifstream stream(path);
    if (!stream)
    {
        return std::nullopt;
    }

    auto json = nlohmann::json::parse(stream, nullptr, false);
    if (json.is_discarded())
    {
        return std::nullopt;
    }

    return json;
}

bool isOurPackage(const std::filesystem::path& directory)
{
    const auto manifest = readManifest(directory);
    if (!manifest)
    {
        return false;
    }

    try
    {
        return manifest->value("type", std::string { }) == kRoutingType;
    }
    catch (const nlohmann::json::exception&)
    {
        // 字段类型错了就当它不是我们的包；直接点名的 loadPlugin 会给出具体原因。
        return false;
    }
}

/// Endstone 只接受 `^[a-z0-9_]+$`，且名字不能以 `endstone` 开头。托管侧仍按清单里的名字
/// 记账（那是插件自己的身份），这里只换**父级看得见**的那个名字。
std::string endstoneName(const std::string& name)
{
    std::string result;
    result.reserve(name.size());
    for (const char c : name)
    {
        if (c >= 'A' && c <= 'Z')
        {
            result.push_back(static_cast<char>(c - 'A' + 'a'));
        }
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
        {
            result.push_back(c);
        }
        else
        {
            result.push_back('_');
        }
    }

    if (result.empty())
    {
        result = "plugin";
    }
    if (result.starts_with("endstone"))
    {
        result = "hosihikari_" + result;
    }

    return result;
}

} // namespace

std::vector<std::string> collectPackageDirectories(const std::filesystem::path& pluginsDirectory)
{
    std::vector<std::string> packages;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory, ec))
    {
        if (!entry.is_directory())
        {
            continue;
        }
        if (isOurPackage(entry.path()))
        {
            packages.push_back(utf8String(entry.path()));
        }
    }

    return packages;
}

HosihikariPluginLoader::HosihikariPluginLoader(endstone::Server& server, ManagedPluginEntries entries)
    : endstone::PluginLoader(server), entries_(entries)
{
}

endstone::Plugin* HosihikariPluginLoader::loadPlugin(std::string file)
{
    // file 是**包目录**，不是文件：我们的包是目录，清单在里面（ADR-0006）。
    const auto directory = utf8Path(file);
    const auto manifest = readManifest(directory);
    if (!manifest)
    {
        server_.getLogger().error("读不了 {} 下的清单，跳过", file);
        return nullptr;
    }

    try
    {
        const auto name = manifest->value("name", std::string { });
        const auto version = manifest->value("version", std::string { "0.0.0" });
        if (name.empty())
        {
            server_.getLogger().error("{} 的清单缺少 name，跳过", file);
            return nullptr;
        }

        endstone::detail::PluginDescriptionBuilder builder;
        builder.description = manifest->value("description", std::string { "Hosihikari 插件" });
        builder.prefix = name;

        const auto author = manifest->value("author", std::string { });
        if (!author.empty())
        {
            builder.authors = { author };
        }

        for (const auto& dependency : manifest->value("dependencies", nlohmann::json::array()))
        {
            auto dependencyName = dependency.value("name", std::string { });
            // 对框架自身的依赖在这个载体上不成立：父级在调我们之前就已经把它装进来了，
            // 而嵌套的这一轮 loadPlugins 只看本轮候选，"已加载"不算数 —— 留着会被判成
            // 未知依赖，整个插件被剔掉。顺序由"我们在父级的 onLoad 里才发起这一轮"保证。
            if (!dependencyName.empty() && dependencyName != kRoutingType)
            {
                // 父级看到的插件名经过 endstoneName 规范化；依赖名必须用同一个形状，
                // 否则 hosihikari-rpc 与 hosihikari_rpc 对不上，依赖图排不出来。
                builder.depend.push_back(endstoneName(dependencyName));
            }
        }

        auto plugin = std::make_unique<ManagedPlugin>(name, builder.build(endstoneName(name), version), entries_);
        plugin->setPackageDirectory(file);

        return plugins_.emplace_back(std::move(plugin)).get();
    }
    catch (const nlohmann::json::exception& ex)
    {
        server_.getLogger().error("{} 的清单字段类型错误，跳过：{}", file, ex.what());
        return nullptr;
    }
}

std::vector<endstone::Plugin*> HosihikariPluginLoader::loadPlugins(std::string directory)
{
    std::vector<endstone::Plugin*> loaded;
    for (const auto& package : collectPackageDirectories(utf8Path(directory)))
    {
        if (auto* plugin = loadPlugin(package))
        {
            loaded.push_back(plugin);
        }
    }
    return loaded;
}

bool HosihikariPluginLoader::unloadPlugin(const std::string& name)
{
    // name_ 是清单里的原名，父级看到的是规范化后的形状；两边都规范化再比。
    const auto key = endstoneName(name);
    const auto it =
        std::ranges::find_if(plugins_, [&key](const auto& plugin) { return endstoneName(plugin->name()) == key; });
    if (it == plugins_.end())
    {
        return false;
    }

    // 对象留着：Endstone 的 plugins_ 里是裸指针，抹掉就成了悬垂。
    (*it)->markRemoved();
    return true;
}

std::vector<std::string> HosihikariPluginLoader::getPluginFileFilters() const
{
    // 只在父级按文件名解析 loader 时用得上（`loadPlugins(vector<string>)`）。
    // 匹配路径的最后一段，不限制点号 —— `Example.Plugin` 也是合法的包目录。
    return { "[/\\\\][^/\\\\]+$" };
}

} // namespace hosihikari::shim::endstone_carrier
