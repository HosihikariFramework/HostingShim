// 自持态 Windows 的垫片：被 hosihikari-server 注入 bedrock_server.exe，导出加载方要调的
// 那**一个**入口。
//
// 入口是导出函数，不是 DllMain（ADR-0016）：CLR 在导出被调用时启动，不在 loader lock 下。
// 托管核心与自持态 Linux 共用同一份启动序列（common/dotnet.cpp，ADR-0005）。

#include <Windows.h>

#include <cstdio>
#include <string>

#include "dotnet.h"

namespace {

/// 垫片自己所在的目录 —— 托管核心与它同住 plugins/hosihikari/。
std::wstring OwnDirectory() {
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OwnDirectory),
            &module)) {
        return {};
    }

    wchar_t path[MAX_PATH];
    const DWORD length = ::GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }

    std::wstring full(path, length);
    const auto slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : full.substr(0, slash + 1);
}

std::wstring Widen(const char* utf8) {
    if (utf8 == nullptr) {
        return {};
    }

    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], length);
    result.resize(static_cast<size_t>(length) - 1);
    return result;
}

void Fail(const char* step, int code) {
    std::fprintf(stderr, "[hosihikari] %s（%d）\n", step, code);
}

}  // namespace

/// 加载方在注入之后调用它。参数是托管核心所在目录（UTF-8，可传 nullptr 用垫片自身目录）。
/// 返回 0 表示成功。
extern "C" __declspec(dllexport) int HosihikariShimStart(const char* managedDirectory) {
    std::wstring directory = (managedDirectory != nullptr && managedDirectory[0] != '\0')
                                 ? Widen(managedDirectory)
                                 : OwnDirectory();
    if (directory.empty()) {
        Fail("取不到核心目录", 0);
        return -1;
    }

    if (directory.back() != L'\\' && directory.back() != L'/') {
        directory.push_back(L'\\');
    }

    const std::wstring configPath = directory + L"Hosihikari.PluginManager.runtimeconfig.json";
    const std::wstring assemblyPath = directory + L"Hosihikari.PluginManager.dll";
    if (::GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES ||
        ::GetFileAttributesW(assemblyPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"[hosihikari] 核心文件不在 %ls\n", directory.c_str());
        return -2;
    }

    component_entry_point_fn entry = nullptr;
    const int code = hosihikari::shim::StartRuntime(
        configPath.c_str(),
        assemblyPath.c_str(),
        L"Hosihikari.NativeHostingEntry, Hosihikari.PluginManager",
        L"Start",
        &entry
    );
    if (code != 0 || entry == nullptr) {
        Fail("启动序列失败", code);
        return code != 0 ? code : -3;
    }

    const int managed = entry(nullptr, 0);
    if (managed != 0) {
        Fail("托管入口返回", managed);
    }
    return managed;
}
