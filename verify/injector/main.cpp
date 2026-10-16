// 把 Shim 的 DLL 注入一个挂起启动的进程，再显式调用它的导出。
//
// 这是**测试脚手架**，不是交付物：ADR-0022 暂缓的是"生产里谁加载"（PreLoader 只给 DllMain），
// 不是"能不能加载"。用它可以在真实 BDS 上验一次整条链，而那条链不经任何父级加载器。
//
// 用法: injector <目标 exe> <工作目录> <dll> <传给导出的参数>

#include <cstdio>
#include <string>

#include <Windows.h>
#include <tlhelp32.h>

namespace {

bool Report(const char* step) {
    std::printf("FAIL  %s  （GetLastError=%lu）\n", step, ::GetLastError());
    return false;
}

/// 从目标进程的模块表里读某个模块的基址。
///
/// **不能用 CreateRemoteThread(LoadLibraryA) 的退出码当基址** —— 它是 32 位的，
/// 而 x64 的模块基址是 64 位；基址一旦超过 4 GB，加偏移算出来的入口就指向垃圾。
uintptr_t RemoteModuleBase(DWORD processId, const char* moduleName) {
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    uintptr_t base = 0;
    if (::Module32First(snapshot, &entry)) {
        do {
            if (::_stricmp(entry.szModule, moduleName) == 0) {
                base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (::Module32Next(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
    return base;
}

/// 在目标进程里分配一块内存并写入内容。
void* WriteToProcess(HANDLE process, const std::string& text) {
    const SIZE_T size = text.size() + 1;
    void* remote = ::VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        return nullptr;
    }

    return ::WriteProcessMemory(process, remote, text.c_str(), size, nullptr) ? remote : nullptr;
}

/// 在目标进程里起一个线程跑 start，等它结束，取回它的返回值。
bool RunInProcess(HANDLE process, LPTHREAD_START_ROUTINE start, void* argument, DWORD* result) {
    HANDLE thread = ::CreateRemoteThread(process, nullptr, 0, start, argument, 0, nullptr);
    if (thread == nullptr) {
        return false;
    }

    ::WaitForSingleObject(thread, INFINITE);
    const BOOL ok = ::GetExitCodeThread(thread, result);
    ::CloseHandle(thread);
    return ok != 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("用法: injector <目标 exe> <工作目录> <dll> <传给导出的参数>\n");
        return 2;
    }

    const std::string target = argv[1];
    const std::string workingDirectory = argv[2];
    const std::string dllPath = argv[3];
    const std::string argument = argv[4];

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    std::string commandLine = target;
    if (!::CreateProcessA(
            nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, workingDirectory.c_str(),
            &startup, &process)) {
        return Report("挂起启动目标进程") ? 0 : 1;
    }

    std::printf("PASS  挂起启动 %s（pid %lu）\n", target.c_str(), process.dwProcessId);

    // ① 目标进程里加载我们的 DLL。
    const std::string dllName = dllPath.substr(dllPath.find_last_of("/\\") + 1);
    void* remoteDllPath = WriteToProcess(process.hProcess, dllPath);
    HMODULE kernel32 = ::GetModuleHandleA("kernel32.dll");
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(kernel32, "LoadLibraryA"));
    DWORD loaded = 0;
    if (remoteDllPath == nullptr || !RunInProcess(process.hProcess, loadLibrary, remoteDllPath, &loaded) || loaded == 0) {
        ::TerminateProcess(process.hProcess, 1);
        return Report("在目标进程里 LoadLibrary") ? 0 : 1;
    }

    const uintptr_t remoteBase = RemoteModuleBase(process.dwProcessId, dllName.c_str());
    if (remoteBase == 0) {
        ::TerminateProcess(process.hProcess, 1);
        return Report("读目标进程里的模块基址") ? 0 : 1;
    }

    std::printf("PASS  目标进程已加载 %s（基址 0x%p）\n", dllName.c_str(), reinterpret_cast<void*>(remoteBase));

    // ② 导出在模块里的偏移 —— 在本地加载同一份 DLL 量出来，再加到目标进程的基址上。
    HMODULE localModule = ::LoadLibraryA(dllPath.c_str());
    if (localModule == nullptr) {
        ::TerminateProcess(process.hProcess, 1);
        return Report("本地加载同一份 DLL") ? 0 : 1;
    }

    const auto localBase = reinterpret_cast<uintptr_t>(localModule);
    const auto localExport = reinterpret_cast<uintptr_t>(::GetProcAddress(localModule, "HosihikariShimStart"));
    if (localExport == 0) {
        ::TerminateProcess(process.hProcess, 1);
        return Report("找导出 HosihikariShimStart") ? 0 : 1;
    }

    // ③ 显式调它 —— 这就是 ADR-0016 说的"加载者负责调用我们的导出入口"。
    // 这一步分三段报告：写参数失败、起线程失败、导出返回非零是**三件不同的事**，混在一起就没法归因。
    void* remoteArgument = WriteToProcess(process.hProcess, argument);
    if (remoteArgument == nullptr) {
        ::TerminateProcess(process.hProcess, 1);
        return Report("把参数写进目标进程") ? 0 : 1;
    }

    auto remoteEntry = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + (localExport - localBase));
    HANDLE caller = ::CreateRemoteThread(process.hProcess, nullptr, 0, remoteEntry, remoteArgument, 0, nullptr);
    if (caller == nullptr) {
        ::TerminateProcess(process.hProcess, 1);
        return Report("在目标进程里起线程调导出") ? 0 : 1;
    }

    ::WaitForSingleObject(caller, INFINITE);
    DWORD managedResult = 0;
    ::GetExitCodeThread(caller, &managedResult);
    ::CloseHandle(caller);

    if (managedResult != 0) {
        std::printf("FAIL  Shim 的导出返回 %lu\n", managedResult);
        ::TerminateProcess(process.hProcess, 1);
        return 1;
    }

    std::printf("PASS  Shim 的导出返回 0\n");

    // ④ 放行，让它照常跑一会儿 —— 托管侧的输出会走同一个控制台。
    ::ResumeThread(process.hThread);
    std::printf("=== 已放行，跑 20 秒 ===\n");
    ::Sleep(20000);
    ::TerminateProcess(process.hProcess, 0);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return 0;
}
