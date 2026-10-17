# Hosihikari.Shim

星光框架的**原生层**：把 CLR 托管进 BDS 进程，然后把控制权交给核心
[`Hosihikari.PluginManager`](https://github.com/HosihikariFramework/PluginManagement)。

它只做四件事：定位 hostfxr、以核心的 `runtimeconfig.json` 初始化运行时、载入核心程序集、取托管入口。
其余一切（发现、依赖、隔离、生命周期）都在托管侧。

## 这个仓里有什么

| | |
|---|---|
| `common/` | 启动序列 —— **载体无关**，各载体的目标都编这一段 |
| `carriers/` | 各载体的目标：`levilamina/`（xmake）与 `endstone/`（CMake）。其余见「还没做的」 |
| `verify/` | 证明「原生 → 托管」这一跳的验证程序与脚手架 |

nethost 的头文件与静态库**按版本从 NuGet 取**，不随仓带二进制 —— 载体与 `verify/` 两处构建各自下载。

## 跑一遍

### 载体（LeviLamina）

```
carriers\levilamina\build.bat
carriers\levilamina\deploy.bat <服务端根目录>
```

`build.bat` 自己用 `vswhere` 找 vcvars、接上 clang-cl；出网要走代理的网络请先设好 `https_proxy`
（脚本不替你设）。产物与清单由上游的 `@levibuildscript/modpacker` 落到 `bin/hosihikari/`，
`deploy.bat` 再把它与托管核心、nethost.dll 一起铺进 `plugins/hosihikari/`。

真机的一条龙 —— 起服、依次打 `show`/`disable`/`enable`/`unload`/`load`、再断言日志：

```
powershell -File carriers\levilamina\smoke.ps1 -ServerRoot <服务端根目录>
```

它会先跑一遍托管侧的断言宿主（顺带产出插件包），所以要有并列的 `Hosihikari.PluginManager` checkout。
需要一台已装好 LeviLamina 的服务端；**它不会去杀别人的服务端**，发现已经在跑就直接报错退出。

`tooth.json` 是 lip 的包元数据（`lip install github.com/HosihikariFramework/Shim`）：它声明依赖
LeviLamina，并把发布附件里的 `hosihikari/` 整个铺到 `plugins/hosihikari/`。附件的内容就是
`deploy.bat` 铺出来的那一份（Shim、清单、`nethost.dll`、托管核心），打成 zip 且顶层目录名为
`hosihikari/`。

### 载体（Endstone）

```
carriers\endstone\build.bat
carriers\endstone\deploy.bat <bedrock_server 所在目录>
endstone -s <bedrock_server 所在目录>
```

与 LeviLamina 那套的三处结构差异，都是父级的形状决定的：

- **插件是按文件加载的**，所以载体本身是 `plugins/endstone_hosihikari.dll` 一个文件；托管核心仍在
  `plugins/hosihikari/`（父级按插件名给的数据目录）。我们的包是目录，因此覆盖了 `loadPlugins`。
- **`nethost.dll` 必须放在服务端根目录**：Endstone 用普通 `LoadLibrary` 加载插件，依赖按"应用目录 +
  当前目录"找；LeviLamina 那边是父级把 mod 目录加进了 PATH，这里没有这待遇。
- **插件名会被规范化**：Endstone 只接受 `^[a-z0-9_]+$` 且不以 `endstone` 开头，所以 `Bar` 在父级
  那边叫 `bar`，托管侧仍按清单里的名字记账。

### 原生缝（与载体无关）

```
cmake -S verify -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
./build/Release/host-glue.exe <托管侧输出目录>
```

它会走完启动序列并调用托管的入口，四步各自报告成败。托管侧先用
`dotnet build src/Hosihikari.PluginManager.csproj -c Release` 编出来。

`verify/host-dll/` 与 `verify/injector/` 是同一件事在**真实 BDS 进程里**的版本：前者把启动序列编成一个
导出入口的 DLL，后者把它注入一个挂起启动的 `bedrock_server.exe`，再显式调那个导出。

> `injector` 是**验证脚手架，不是交付物**，也不打算做成通用注入器 —— 它存在的唯一理由是
> ADR-0022 暂缓的是"生产里谁加载我们"（PreLoader 只给 `DllMain`，与 ADR-0016 冲突），而不是
> "能不能被加载"。要把它用在别处，先想清楚这一点。

## CI 覆盖到哪

- **`verify/` 的三个目标**（host-glue / host-dll / injector）在 `windows-latest` 上编出来。
- **`carriers/levilamina/`** 在 `windows-latest` 上编出来。它钉住 LLVM 版本，并缓存 `~/AppData/Local/.xmake`：
  上游的 `levilamina` 包没有预编译体，第一次会把 LeviLamina 从源码编一遍（约十分钟），之后走缓存。

覆盖不到的是**真机行为**：起服、交付、启停、卸载、重载。那些由 `smoke.ps1` 在本地跑，需要一台真服务端。

## 还没做的

另外两个载体目标：Endstone（Linux）、自持态 Linux。`common/` 已经就位，它们各自要做的是
接上自己的入口（父级 C++ 子类 / `EntryPoint` 符号 / `__libc_start_main` 拦截），并满足父级对清单与构建的要求。

## 许可

LGPL-3.0-or-later，见 [LICENSE.md](LICENSE.md)。
