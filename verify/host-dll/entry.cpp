// 从 common/ 的启动序列编出来的一个 DLL，导出加载方要调的那**一个**入口。
//
// 形状按 ADR-0016：入口是导出函数，**不是 DllMain** —— 在 DllMain 里初始化运行时等于
// 在 loader lock 下启动 CLR，那是死锁。加载方负责在加载完之后显式调它。
//
// 它住在 verify/ 下，因为它是验证脚手架：ADR-0022 暂缓的是"生产里谁加载"，
// 不是"能不能加载"。自持态 Windows 的目标将来从它起步。

#include <cstdio>
#include <cstdlib>
#include <string>

#include "dotnet.h"

#if defined(_WIN32)
    #include <Windows.h>
    #define SHIM_EXPORT extern "C" __declspec(dllexport)
#else
    #define SHIM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

// 验证脚手架用的逐点追踪：注入进别人的进程时没有调试器，只能靠它定位崩在哪一步。
// 载体目标落地时应当换成正式的日志通道。
void Trace(const char* step) {
    const char* temp = std::getenv("TEMP");
    if (temp == nullptr) {
        return;
    }

    std::string path = std::string(temp) + "\\hosihikari-shim-trace.log";
    if (FILE* file = std::fopen(path.c_str(), "a")) {
        std::fprintf(file, "%s\n", step);
        std::fclose(file);
    }
}

/// 入参是 UTF-8。Windows 的 char_t 是 wchar_t，要转一次；Linux 上它本来就是 char。
#if defined(_WIN32)
std::wstring Widen(const char* utf8) {
    if (utf8 == nullptr) {
        return {};
    }

    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, result.data(), length);
    result.resize(static_cast<size_t>(length) - 1);
    return result;
}
#else
std::string Widen(const char* utf8) {
    return utf8 == nullptr ? std::string() : std::string(utf8);
}
#endif

}  // namespace

/// 加载方在把本模块加载进来之后调用它。参数是核心的输出目录（UTF-8）。返回 0 表示成功。
SHIM_EXPORT int HosihikariShimStart(const char* managedDirectory) {
    Trace("01 导出已进入");

    std::string directory = managedDirectory == nullptr ? std::string() : std::string(managedDirectory);
    if (!directory.empty() && directory.back() != '/' && directory.back() != '\\') {
        directory.push_back('/');
    }

    const std::wstring configPath = Widen((directory + "Hosihikari.PluginManager.runtimeconfig.json").c_str());
    const std::wstring assemblyPath = Widen((directory + "Hosihikari.PluginManager.dll").c_str());
    const std::wstring typeName = Widen("Hosihikari.NativeHostingEntry, Hosihikari.PluginManager");
    const std::wstring methodName = Widen("Start");
    Trace("02 路径已备好");

    component_entry_point_fn entry = nullptr;
    const int code =
        hosihikari::shim::StartRuntime(configPath.c_str(), assemblyPath.c_str(), typeName.c_str(), methodName.c_str(), &entry);
    Trace("03 启动序列已走完");

    if (code != 0 || entry == nullptr) {
        return code != 0 ? code : -1;
    }

    const int managed = entry(nullptr, 0);
    Trace("04 托管入口已返回");
    return managed;
}
