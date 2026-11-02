// Self-hosted Windows launcher.
//
// Start bedrock_server.exe suspended, inject plugins/hosihikari/hosihikari.dll,
// call its exported entry, resume (ADR-0046). Published with NativeAOT so the
// server does not need a .NET runtime beyond the one the framework already
// requires, and with CsWin32 so the Win32 surface is generated rather than
// hand-marshalled.

using System.CommandLine;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.Console;
using Windows.Win32.System.Diagnostics.ToolHelp;
using Windows.Win32.System.Memory;
using Windows.Win32.System.Threading;

namespace Hosihikari.SelfHostedLauncher;

internal static unsafe class Program
{
    private const uint Infinite = 0xFFFFFFFF;

    // Rooted for the process lifetime: the OS keeps the function pointer after this call.
    private static readonly PHANDLER_ROUTINE CtrlHandler = type => type is 0 or 1;

    private static int Main(string[] args)
    {
        var rootOption = new Option<DirectoryInfo>("--root")
        {
            Description = "Server root; defaults to the current directory.",
            DefaultValueFactory = _ => new DirectoryInfo(Environment.CurrentDirectory),
        };

        var command = new RootCommand(
            "Start bedrock_server.exe with the Hosihikari carrier. Arguments after -- are passed to the server.")
        {
            rootOption,
        };
        command.TreatUnmatchedTokensAsErrors = false;

        command.SetAction(parse =>
        {
            var root = parse.GetValue(rootOption) ?? new DirectoryInfo(Environment.CurrentDirectory);
            return Run(root.FullName, [.. parse.UnmatchedTokens]);
        });

        return command.Parse(args).Invoke();
    }

    private static int Run(string root, string[] extra)
    {
        var target = Path.Combine(root, "bedrock_server.exe");
        var shim = Path.Combine(root, "plugins", "hosihikari", "hosihikari.dll");
        if (!File.Exists(target))
        {
            Console.Error.WriteLine($"[hosihikari] 找不到 {target}");
            return 2;
        }
        if (!File.Exists(shim))
        {
            Console.Error.WriteLine($"[hosihikari] 找不到 {shim}");
            return 2;
        }

        var commandLine = new StringBuilder($"\"{target}\"");
        foreach (var argument in extra)
        {
            commandLine.Append($" \"{argument}\"");
        }

        // CTRL_C_EVENT and CTRL_BREAK_EVENT belong to BDS; the launcher waits for it to exit.
        PInvoke.SetConsoleCtrlHandler(CtrlHandler, true);

        var line = (commandLine.ToString() + "\0").ToCharArray();
        var lineSpan = line.AsSpan();
        var startup = new STARTUPINFOW { cb = (uint)sizeof(STARTUPINFOW) };
        if (!PInvoke.CreateProcess(
                target,
                ref lineSpan,
                null,
                null,
                false,
                PROCESS_CREATION_FLAGS.CREATE_SUSPENDED,
                null,
                root,
                in startup,
                out var process))
        {
            return Fail("create bedrock_server.exe suspended");
        }

        using var processHandle = new SafeFileHandle((IntPtr)process.hProcess.Value, ownsHandle: true);
        using var threadHandle = new SafeFileHandle((IntPtr)process.hThread.Value, ownsHandle: true);

        // ① Load the shim in the target.
        var shimPath = Encoding.Unicode.GetBytes(shim + "\0");
        var remoteShim = WriteToProcess(processHandle, shimPath);
        if (remoteShim is null)
        {
            return FailProcess(processHandle, "write the shim path into the target");
        }

        var loadLibrary = ModuleExport(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
        if (loadLibrary == IntPtr.Zero)
        {
            return FailProcess(processHandle, "find LoadLibraryW");
        }
        if (!RunInProcess(processHandle, loadLibrary, remoteShim, out var loaded) || loaded == 0)
        {
            return FailProcess(processHandle, "load the shim in the target");
        }

        var remoteBase = RemoteModuleBase(process.dwProcessId, Path.GetFileName(shim));
        if (remoteBase == 0)
        {
            return FailProcess(processHandle, "read the shim base address");
        }

        // ② Measure the export offset locally, add it to the remote base.
        var localModule = LoadLibrary(shim);
        if (localModule.IsNull)
        {
            return FailProcess(processHandle, "load the same shim locally");
        }

        var localExport = ModuleExport(localModule, "HosihikariShimStart");
        if (localExport == IntPtr.Zero)
        {
            return FailProcess(processHandle, "find HosihikariShimStart");
        }

        var remoteEntry = remoteBase + localExport.ToInt64() - (long)localModule.Value;

        // ③ Call the export; ADR-0016's exported entry.
        var managedDirectory = Encoding.UTF8.GetBytes(Path.Combine(root, "plugins", "hosihikari") + "\0");
        var remoteArgument = WriteToProcess(processHandle, managedDirectory);
        if (remoteArgument is null)
        {
            return FailProcess(processHandle, "write the core directory into the target");
        }

        if (!RunInProcess(processHandle, (IntPtr)remoteEntry, remoteArgument, out var managed) || managed != 0)
        {
            Console.Error.WriteLine($"[hosihikari] 垫片的导出返回 {managed}");
            return FailProcess(processHandle, "call the exported entry");
        }

        // ④ Resume and wait.
        if (PInvoke.ResumeThread(threadHandle) == uint.MaxValue)
        {
            return FailProcess(processHandle, "resume the server thread");
        }

        PInvoke.WaitForSingleObject(processHandle, Infinite);
        PInvoke.GetExitCodeProcess(processHandle, out var exitCode);
        return (int)exitCode;
    }

    private static int Fail(string step)
    {
        Console.Error.WriteLine($"[hosihikari] 失败：{step}（GetLastError={Marshal.GetLastPInvokeError()}）");
        return 1;
    }

    private static int FailProcess(SafeHandle process, string step)
    {
        PInvoke.TerminateProcess(process, 1);
        return Fail(step);
    }

    private static void* WriteToProcess(SafeHandle process, byte[] data)
    {
        var remote = PInvoke.VirtualAllocEx(
            process,
            null,
            (nuint)data.Length,
            VIRTUAL_ALLOCATION_TYPE.MEM_COMMIT | VIRTUAL_ALLOCATION_TYPE.MEM_RESERVE,
            PAGE_PROTECTION_FLAGS.PAGE_READWRITE);
        if (remote is null)
        {
            return null;
        }

        return PInvoke.WriteProcessMemory(process, remote, data) ? remote : null;
    }

    private static bool RunInProcess(SafeHandle process, IntPtr start, void* argument, out uint result)
    {
        result = 0;
        var routine = Marshal.GetDelegateForFunctionPointer<LPTHREAD_START_ROUTINE>(start);
        using var thread = PInvoke.CreateRemoteThread(process, null, 0, routine, argument, 0, out _);
        if (thread.IsInvalid)
        {
            return false;
        }

        PInvoke.WaitForSingleObject(thread, Infinite);
        var ok = PInvoke.GetExitCodeThread(thread, out result);
        GC.KeepAlive(routine);
        return ok;
    }

    private static HMODULE GetModuleHandle(string name)
    {
        fixed (char* pName = name + "\0")
        {
            return PInvoke.GetModuleHandle((PCWSTR)pName);
        }
    }

    private static HMODULE LoadLibrary(string path)
    {
        fixed (char* pPath = path + "\0")
        {
            return PInvoke.LoadLibrary((PCWSTR)pPath);
        }
    }

    private static IntPtr ModuleExport(HMODULE module, string name)
    {
        fixed (byte* pName = Encoding.ASCII.GetBytes(name + "\0"))
        {
            return PInvoke.GetProcAddress(module, (PCSTR)pName).Value;
        }
    }

    private static long RemoteModuleBase(uint processId, string moduleName)
    {
        var raw = PInvoke.CreateToolhelp32Snapshot(
            CREATE_TOOLHELP_SNAPSHOT_FLAGS.TH32CS_SNAPMODULE | CREATE_TOOLHELP_SNAPSHOT_FLAGS.TH32CS_SNAPMODULE32,
            processId);
        using var snapshot = new SafeFileHandle((IntPtr)raw.Value, ownsHandle: true);
        if (snapshot.IsInvalid)
        {
            return 0;
        }

        var entry = new MODULEENTRY32 { dwSize = (uint)sizeof(MODULEENTRY32) };
        if (!PInvoke.Module32First(snapshot, ref entry))
        {
            return 0;
        }

        do
        {
            var nameBytes = MemoryMarshal.AsBytes(entry.szModule.AsReadOnlySpan());
            var zero = nameBytes.IndexOf((byte)0);
            var name = Encoding.ASCII.GetString(nameBytes[..(zero < 0 ? nameBytes.Length : zero)]);
            if (string.Equals(name, moduleName, StringComparison.OrdinalIgnoreCase))
            {
                return (long)entry.modBaseAddr;
            }
        }
        while (PInvoke.Module32Next(snapshot, ref entry));

        return 0;
    }
}
