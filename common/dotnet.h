#pragma once

// CLR 托管的启动序列（ADR-0005）。载体无关 —— 各载体的目标都编这一段，只是接上不同的入口。
//
// 形状照官方当前的托管教程（dotnet/core/tutorials/netcore-hosting）：
//   get_hostfxr_path → hostfxr_initialize_for_runtime_config
//   → get_runtime_delegate(hdt_load_assembly_and_get_function_pointer)
//   → 一次调用拿到托管入口 → hostfxr_close
//
// 两侧都是"最小的那一层"：原生侧只做定位、初始化、取入口；
// 其余一切（发现、依赖、隔离、生命周期）都在托管侧。

#include "coreclr_delegates.h"  // 来自随版本拉取的 runtime pack，由构建把它的 native 目录加进包含路径

// char_t 在 Windows 是 wchar_t、Linux 是 char（ADR-0005 列的跨平台差异之一）。
// 这是 TEXT() 的跨平台等价物：写字符串字面量时用它，两边都能编。
#if defined(_WIN32)
    #define CHARTEXT(text) L##text
#else
    #define CHARTEXT(text) text
#endif

namespace hosihikari::shim {

/// 定位并加载 hostfxr。成功返回 0。
int LoadHostfxr();

/// 以组件的 runtimeconfig.json 初始化运行时，取回 load_assembly_and_get_function_pointer。
///
/// `hostPath` 指向组件自身（托管程序集的路径）—— **必须给**。官方教程那段传的是 nullptr，
/// 代价是运行时拿不到宿主路径，托管侧的 `AppContext.BaseDirectory` 会是**空的**；
/// 任何按基目录找东西的代码（比如按目录枚举依赖）就会拿到空路径。
int InitializeRuntime(const char_t* runtimeConfigPath, const char_t* hostPath);

/// 载入托管程序集并取回入口的函数指针。
/// 目标方法须标 `[UnmanagedCallersOnly]` —— 走 UNMANAGEDCALLERSONLY_METHOD 可以省掉封送桩。
int GetManagedEntry(
    const char_t* assemblyPath,
    const char_t* typeName,
    const char_t* methodName,
    component_entry_point_fn* entryPoint);

/// 把启动序列一次走完。任一步失败即返回它的错误码，不继续。
/// 核心缺少契约版本入口时返回 -2；版本与垫片不一致时返回 -3。
int StartRuntime(
    const char_t* runtimeConfigPath,
    const char_t* assemblyPath,
    const char_t* typeName,
    const char_t* methodName,
    component_entry_point_fn* entryPoint);

}  // namespace hosihikari::shim
