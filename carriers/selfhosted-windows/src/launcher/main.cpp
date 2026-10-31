// 自持态 Windows 的启动器：挂起启动 bedrock_server.exe，把 plugins/hosihikari/hosihikari.dll
// 注入进去，调用它的导出入口，再放行。
//
// 这就是 ADR-0016 说的"加载者负责调用我们的导出入口"：CLR 不在 DllMain 里初始化。
// 用法: hosihikari-server [服务端根目录] [-- <传给 bedrock_server 的参数>...]

#include <Windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

void Report(const wchar_t* step) {
    std::fwprintf(stderr, L"[hosihikari] 失败：%ls（GetLastError=%lu）\n", step, ::GetLastError());
}

std::wstring CurrentDirectory() {
    DWORD length = ::GetCurrentDirectoryW(0, nullptr);
    if (length == 0) {
        return {};
    }

    std::wstring buffer(length, L'\0');
    length = ::GetCurrentDirectoryW(length, &buffer[0]);
    buffer.resize(length);
    return buffer;
}

bool Exists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring FileName(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], length, nullptr, nullptr);
    result.resize(static_cast<size_t>(length) - 1);
    return result;
}

/// 从目标进程的模块表里读模块基址 —— 不能用远程 LoadLibrary 的退出码当基址，
/// 它是 32 位的而 x64 模块基址是 64 位。
uintptr_t RemoteModuleBase(DWORD processId, const std::wstring& moduleName) {
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uintptr_t base = 0;
    if (::Module32FirstW(snapshot, &entry)) {
        do {
            if (::_wcsicmp(entry.szModule, moduleName.c_str()) == 0) {
                base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (::Module32NextW(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
    return base;
}

void* WriteToProcess(HANDLE process, const void* data, SIZE_T size) {
    void* remote = ::VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        return nullptr;
    }

    return ::WriteProcessMemory(process, remote, data, size, nullptr) ? remote : nullptr;
}

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

/// 子进程与启动器共用控制台；Ctrl+C 交给 BDS 处理，启动器等着收尾并回报退出码。
BOOL WINAPI OnConsoleSignal(DWORD type) {
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ? TRUE : FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring root = CurrentDirectory();
    bool rootGiven = false;
    bool forward = false;
    std::vector<std::wstring> extra;

    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (forward) {
            extra.push_back(argument);
        } else if (argument == L"--") {
            forward = true;
        } else if (!rootGiven && !argument.empty() && argument[0] != L'-') {
            root = argument;
            rootGiven = true;
        } else {
            extra.push_back(argument);
        }
    }

    const std::wstring target = root + L"\\bedrock_server.exe";
    const std::wstring shim = root + L"\\plugins\\hosihikari\\hosihikari.dll";
    if (!Exists(target)) {
        std::fprintf(stderr, "[hosihikari] 找不到 %ls\n", target.c_str());
        return 2;
    }
    if (!Exists(shim)) {
        std::fprintf(stderr, "[hosihikari] 找不到 %ls\n", shim.c_str());
        return 2;
    }

    std::wstring commandLine = L"\"" + target + L"\"";
    for (const auto& argument : extra) {
        commandLine += L" \"" + argument + L"\"";
    }

    // CreateProcessW 要可写的命令行缓冲区。
    std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    ::SetConsoleCtrlHandler(OnConsoleSignal, TRUE);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(
            target.c_str(),
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED,
            nullptr,
            root.c_str(),
            &startup,
            &process)) {
        Report(L"挂起启动 bedrock_server.exe");
        return 1;
    }

    auto fail = [&](const wchar_t* step) {
        ::TerminateProcess(process.hProcess, 1);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        Report(step);
        return 1;
    };

    // ① 目标进程里加载垫片。
    const SIZE_T shimBytes = (shim.size() + 1) * sizeof(wchar_t);
    void* remoteShimPath = WriteToProcess(process.hProcess, shim.c_str(), shimBytes);
    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(kernel32, "LoadLibraryW"));
    DWORD loaded = 0;
    if (remoteShimPath == nullptr || loadLibrary == nullptr ||
        !RunInProcess(process.hProcess, loadLibrary, remoteShimPath, &loaded) || loaded == 0) {
        return fail(L"在目标进程里加载垫片");
    }

    const uintptr_t remoteBase = RemoteModuleBase(process.dwProcessId, FileName(shim));
    if (remoteBase == 0) {
        return fail(L"读目标进程里的垫片基址");
    }

    // ② 导出在模块里的偏移在本地量，再加到目标进程的基址上。
    HMODULE localModule = ::LoadLibraryW(shim.c_str());
    if (localModule == nullptr) {
        return fail(L"本地加载同一份垫片");
    }
    const auto localBase = reinterpret_cast<uintptr_t>(localModule);
    const auto localExport = reinterpret_cast<uintptr_t>(::GetProcAddress(localModule, "HosihikariShimStart"));
    if (localExport == 0) {
        return fail(L"找导出 HosihikariShimStart");
    }

    // ③ 显式调用导出 —— ADR-0016 要求的那一步。
    const std::string managedDirectory = Utf8(root + L"\\plugins\\hosihikari");
    void* remoteArgument = WriteToProcess(
        process.hProcess,
        managedDirectory.c_str(),
        managedDirectory.size() + 1
    );
    if (remoteArgument == nullptr) {
        return fail(L"把核心目录写进目标进程");
    }

    auto remoteEntry = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + (localExport - localBase));
    DWORD managedResult = 0;
    if (!RunInProcess(process.hProcess, remoteEntry, remoteArgument, &managedResult)) {
        return fail(L"在目标进程里调导出入口");
    }
    if (managedResult != 0) {
        std::fprintf(stderr, "[hosihikari] 垫片的导出返回 %lu\n", managedResult);
        ::TerminateProcess(process.hProcess, 1);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return 1;
    }

    // ④ 放行，等服务端自己退出。
    if (::ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        return fail(L"恢复服务端主线程");
    }

    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    ::GetExitCodeProcess(process.hProcess, &exitCode);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
