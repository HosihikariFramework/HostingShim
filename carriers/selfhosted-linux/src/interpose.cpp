// Self-hosted Linux carrier: intercept __libc_start_main, bring the CLR up
// before BDS' main, and let the core enumerate plugins/ (ADR-0005 / 0013 / 0018).
//
// Loaded with LD_PRELOAD by hosihikari-server. The real __libc_start_main is
// resolved with RTLD_NEXT; rtld_fini is passed through unchanged (ADR-0005).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

#include "dotnet.h"

namespace {

using MainFunction = int (*)(int, char**, char**);
using LibcStartMain = int (*)(MainFunction, int, char**, void (*)(void), void (*)(void), void (*)(void), void*);

/// 自持态的托管入口在核心目录里；目录由本模块的装载路径推出来。
void StartSelfHosted() {
    try {
        Dl_info info{};
        if (::dladdr(reinterpret_cast<void*>(&StartSelfHosted), &info) == 0 || info.dli_fname == nullptr) {
            std::fprintf(stderr, "[hosihikari] 取不到本模块路径，跳过托管启动\n");
            return;
        }

        const auto directory = std::filesystem::absolute(info.dli_fname).parent_path();
        const auto runtimeConfig = directory / "Hosihikari.PluginManager.runtimeconfig.json";
        const auto component = directory / "Hosihikari.PluginManager.dll";
        if (!std::filesystem::exists(runtimeConfig) || !std::filesystem::exists(component)) {
            std::fprintf(
                stderr,
                "[hosihikari] 核心文件不在 %s（需要 runtimeconfig.json 与 dll）\n",
                directory.string().c_str()
            );
            return;
        }

        component_entry_point_fn start = nullptr;
        const int code = hosihikari::shim::StartRuntime(
            runtimeConfig.c_str(),
            component.c_str(),
            "Hosihikari.NativeHostingEntry, Hosihikari.PluginManager",
            "Start",
            &start
        );
        if (code != 0 || start == nullptr) {
            std::fprintf(stderr, "[hosihikari] 启动序列失败：0x%08X\n", static_cast<unsigned>(code));
            return;
        }

        const int managed = start(nullptr, 0);
        if (managed != 0) {
            std::fprintf(stderr, "[hosihikari] 托管入口返回 %d\n", managed);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[hosihikari] 自持态启动抛出：%s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "[hosihikari] 自持态启动抛出未知异常\n");
    }
}

}  // namespace

extern "C" int __libc_start_main(
    MainFunction main,
    int argc,
    char** argv,
    void (*init)(void),
    void (*fini)(void),
    void (*rtld_fini)(void),
    void* stack_end
) {
    static LibcStartMain real = nullptr;
    static bool started = false;

    if (!started) {
        started = true;

        // 子进程（例如插件起的工具）不能再把垫片预加载一遍。
        ::unsetenv("LD_PRELOAD");

        real = reinterpret_cast<LibcStartMain>(::dlsym(RTLD_NEXT, "__libc_start_main"));
        if (real == nullptr) {
            std::fprintf(stderr, "[hosihikari] 找不到真正的 __libc_start_main\n");
            ::_exit(127);
        }

        StartSelfHosted();
    }

    // rtld_fini 必须原样透传：传 NULL 会静默关掉整个进程的析构函数（ADR-0005）。
    return real(main, argc, argv, init, fini, rtld_fini, stack_end);
}
