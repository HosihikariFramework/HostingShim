#include "dotnet.h"

#include <filesystem>
#include <string>

#if defined(_WIN32)
    #include <Windows.h>
#else
    #include <dlfcn.h>
#endif

#include "hostfxr.h"
#include "nethost.h"

namespace hosihikari::shim {
namespace {

// 与 Hosihikari.PluginManagement.NativeEntry.HostingContractVersion 保持一致。
// 入口方法集合、回调布局或生命周期约定不兼容变化时两边一起递增。
constexpr int kHostingContractVersion = 1;

void* g_hostfxr = nullptr;
hostfxr_initialize_for_runtime_config_fn g_initializeForRuntimeConfig = nullptr;
hostfxr_get_runtime_delegate_fn g_getRuntimeDelegate = nullptr;
hostfxr_close_fn g_close = nullptr;
hostfxr_set_runtime_property_value_fn g_setRuntimeProperty = nullptr;
load_assembly_and_get_function_pointer_fn g_loadAssemblyAndGetFunctionPointer = nullptr;

// 跨平台差异之一（ADR-0005）：Windows 用 LoadLibraryW，Linux 用 dlopen。
void* OpenLibrary(const char_t* path) {
#if defined(_WIN32)
    return ::LoadLibraryW(path);
#else
    return ::dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
}

void* FindSymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return ::dlsym(library, name);
#endif
}

template <typename T>
T LoadSymbol(void* library, const char* name) {
    return reinterpret_cast<T>(FindSymbol(library, name));
}

}  // namespace

int LoadHostfxr() {
    // Endstone reload 会再次走到这里；hostfxr 只加载一次，重复 dlopen/LoadLibrary
    // 每次都加引用计数，却从来没有对应的释放。
    if (g_hostfxr != nullptr && g_initializeForRuntimeConfig != nullptr) {
        return 0;
    }

    char_t buffer[4096];
    size_t bufferSize = sizeof(buffer) / sizeof(char_t);

    int rc = get_hostfxr_path(buffer, &bufferSize, nullptr);
    if (rc != 0) {
        return rc;
    }

    g_hostfxr = OpenLibrary(buffer);
    if (g_hostfxr == nullptr) {
        return -1;
    }

    g_initializeForRuntimeConfig = LoadSymbol<hostfxr_initialize_for_runtime_config_fn>(
        g_hostfxr, "hostfxr_initialize_for_runtime_config");
    g_getRuntimeDelegate = LoadSymbol<hostfxr_get_runtime_delegate_fn>(g_hostfxr, "hostfxr_get_runtime_delegate");
    g_close = LoadSymbol<hostfxr_close_fn>(g_hostfxr, "hostfxr_close");
    g_setRuntimeProperty =
        LoadSymbol<hostfxr_set_runtime_property_value_fn>(g_hostfxr, "hostfxr_set_runtime_property_value");

    if (g_initializeForRuntimeConfig == nullptr || g_getRuntimeDelegate == nullptr || g_close == nullptr ||
        g_setRuntimeProperty == nullptr) {
        return -1;
    }

    return 0;
}

namespace {

/// 取路径里的目录部分，并做成绝对路径。Windows 与 Linux 的分隔符都处理。
///
/// 绝对这一步不能省：父级给过来的路径往往是相对的（LeviLamina 的 mods 根就是 `./plugins`），
/// 而基目录按约定必须是绝对路径 —— 托管侧拿它去建 PhysicalFileProvider 之类的对象时，
/// 相对路径会直接抛异常。补在**这里**，因为这是所有载体共用的那一步。
std::basic_string<char_t> DirectoryOf(const char_t* path) {
    std::basic_string<char_t> text = path == nullptr ? std::basic_string<char_t>() : std::basic_string<char_t>(path);
    const size_t separator = text.find_last_of(CHARTEXT("/\\"));
    if (separator == std::basic_string<char_t>::npos) {
        return {};
    }

    return std::filesystem::absolute(std::filesystem::path{text.substr(0, separator)}).native();
}

}  // namespace

int InitializeRuntime(const char_t* runtimeConfigPath, const char_t* hostPath) {
    hostfxr_initialize_parameters parameters{};
    parameters.size = sizeof(parameters);
    parameters.host_path = hostPath;

    hostfxr_handle context = nullptr;
    int rc = g_initializeForRuntimeConfig(runtimeConfigPath, &parameters, &context);

    // 正向的返回码也是成功：宿主（或别的框架）已经兼容地初始化过运行时时，
    // 这里返回的是 Success_HostAlreadyInitialized —— 复用那个运行时正是我们要的。
    // 只认 0 会把这种情形误判成失败，而"一个进程只有一个运行时"是硬约束。
    if (rc < 0 || context == nullptr) {
        if (context != nullptr) {
            g_close(context);
        }

        return rc < 0 ? rc : -1;
    }

    // 组件托管下运行时**不会**替你填 `AppContext.BaseDirectory` —— 它在托管侧是空串。
    // 按基目录找东西的代码（McMaster 就会按它枚举）拿到空路径就崩。属性只能在运行时载入之前设，
    // 所以就在这儿，取委托之前。返回值 rc > 0 表示运行时已经由上一次启动初始化过（Endstone
    // reload 就是这条路径），属性已经设过，也不能再设。
    const std::basic_string<char_t> componentDirectory = DirectoryOf(hostPath);
    if (rc == 0 && !componentDirectory.empty()
        && g_setRuntimeProperty(context, CHARTEXT("APP_CONTEXT_BASE_DIRECTORY"), componentDirectory.c_str()) != 0) {
        g_close(context);
        return -1;
    }

    rc = g_getRuntimeDelegate(
        context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&g_loadAssemblyAndGetFunctionPointer));
    g_close(context);

    if (rc < 0 || g_loadAssemblyAndGetFunctionPointer == nullptr) {
        return rc < 0 ? rc : -1;
    }

    return 0;
}

int GetManagedEntry(
    const char_t* assemblyPath,
    const char_t* typeName,
    const char_t* methodName,
    component_entry_point_fn* entryPoint) {
    void* address = nullptr;
    int rc = g_loadAssemblyAndGetFunctionPointer(
        assemblyPath, typeName, methodName, UNMANAGEDCALLERSONLY_METHOD, nullptr, &address);

    if (rc < 0 || address == nullptr) {
        return rc < 0 ? rc : -1;
    }

    *entryPoint = reinterpret_cast<component_entry_point_fn>(address);
    return 0;
}

int StartRuntime(
    const char_t* runtimeConfigPath,
    const char_t* assemblyPath,
    const char_t* typeName,
    const char_t* methodName,
    component_entry_point_fn* entryPoint) {
    int rc = LoadHostfxr();
    if (rc != 0) {
        return rc;
    }

    rc = InitializeRuntime(runtimeConfigPath, assemblyPath);
    if (rc != 0) {
        return rc;
    }

    // 先确认核心与垫片说同一版入口契约，再取真正的入口。
    component_entry_point_fn contractVersion = nullptr;
    rc = GetManagedEntry(
        assemblyPath, typeName, CHARTEXT("GetHostingContractVersion"), &contractVersion);
    if (rc != 0 || contractVersion == nullptr) {
        return -2;
    }
    if (contractVersion(nullptr, 0) != kHostingContractVersion) {
        return -3;
    }

    return GetManagedEntry(assemblyPath, typeName, methodName, entryPoint);
}

}  // namespace hosihikari::shim
