# Hosihikari.Shim

[English](README.md) | [简体中文](README.zh_hans.md)

星光框架的**原生层**，职责名是 **HostingShim**（CLR 寄宿垫片）：把 CLR 托管进 BDS 进程，然后把控制权交给核心
[`Hosihikari.PluginManager`](https://github.com/HosihikariFramework/PluginManagement)。

它只做四件事：定位 hostfxr、以核心的 `runtimeconfig.json` 初始化运行时、载入核心程序集、取托管入口。
其余一切（发现、依赖、隔离、生命周期）都在托管侧。它不投影载体 API —— 那一侧是 **BindingShim**，
由星光产出（ADR-0021）。

## 这个仓里有什么

| | |
|---|---|
| `common/` | 启动序列 —— **载体无关**，各载体的目标都编这一段 |
| `carriers/` | 各载体的目标：`levilamina/`（xmake）、`endstone/`（CMake，Windows 与 Linux）与 `selfhosted-linux/`（CMake） |
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

Endstone 载体另有一条真机路径。它通过 Endstone CLI 启动服务端（CLI 负责把运行时注入原版
`bedrock_server.exe`），需要 CLI 与 Node：

```
powershell -File carriers\endstone\smoke.ps1 -ServerRoot <Endstone 服务端根目录>
```

它铺 Provider 包与两个 Node 插件，依次打 `plugins` / `endstone:disable` / `endstone:enable` /
`stop`，断言插件写出的文件、崩溃故障与载体的移除告警。CLI 默认找
`%TEMP%\endstone-venv\Scripts\endstone.exe`，也可以用 `ENDSTONE_EXE` 指。

只验载体本身的一条短路径，对着已经用 lip 装好的包跑，不铺 Provider 与插件：

```
powershell -File carriers\endstone\smoke-live.ps1 -ServerRoot <服务端根目录>
bash carriers/endstone/smoke.sh <服务端根目录>
```

Linux 脚本用 `ENDSTONE_EXE` 或 PATH 上的 `endstone`。两边都断言 CLR 起来、载体接管、父级启用载体、
服务端正常停服。

### 载体（Endstone）

Windows：

```
carriers\endstone\build.bat
carriers\endstone\deploy.bat <bedrock_server 所在目录>
endstone -s <bedrock_server 所在目录>
```

Linux：

```
bash carriers/endstone/build.sh
bash carriers/endstone/deploy.sh <bedrock_server 所在目录>
endstone -s <bedrock_server 所在目录>
```

`build.sh` 需要 CMake 与 C++20 编译器；SDK 与 nlohmann/json 由 CMake 浅克隆，nethost 从 nuget.org 取。
`deploy.sh` 铺插件、`libnethost.so` 与托管核心；插件带 `$ORIGIN`，所以 `libnethost.so` 放在它旁边。

与 LeviLamina 那套的三处结构差异，都是父级的形状决定的：

- **插件是按文件加载的**，所以载体本身是 `plugins/hosihikari.dll`（Linux 上是
  `plugins/hosihikari.so`）一个文件；托管核心仍在 `plugins/hosihikari/`（父级按插件名给的数据目录）。
  我们的包是目录，因此覆盖了 `loadPlugins`。
- **nethost 库必须能被解析到**：Windows 上 `nethost.dll` 放在 `bedrock_server.exe` 旁边，因为
  Endstone 用普通 `LoadLibrary` 加载插件，依赖按"应用目录 + 当前目录"找；Linux 上
  `libnethost.so` 放在插件旁边，由 `$ORIGIN` 找到。LeviLamina 那边是父级把 mod 目录加进了 PATH，
  这里没有这待遇。
- **插件名会被规范化**：Endstone 只接受 `^[a-z0-9_]+$` 且不以 `endstone` 开头，所以 `Bar` 在父级
  那边叫 `bar`，托管侧仍按清单里的名字记账。

### 载体（自持态 Linux）

```
bash carriers/selfhosted-linux/build.sh
bash carriers/selfhosted-linux/deploy.sh <bedrock_server 所在目录>
./hosihikari-server
```

`hosihikari-server` 用 `LD_PRELOAD` 预加载 `plugins/hosihikari/libhosihikari.so`。垫片拦截
`__libc_start_main`，在 BDS 的 `main` 之前启动 CLR 与核心，核心自己枚举 `plugins/`
（ADR-0005 / 0013 / 0018）。启动器会先切到服务端目录，核心的相对路径因此落在正确的服务端根上；
需要系统已装 .NET 10 运行时。

自持态的目标是 BDS 1.21.3：容器本身与 BDS 版本无关，但能力面（星光）止于那里（ADR-0001）。

自持态插件是预编译程序集包；没有运行期启停（ADR-0020）。只验载体本身的真机路径：

```
bash carriers/selfhosted-linux/smoke.sh <bedrock_server 所在目录>
```

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
- **`carriers/endstone/`** 在 `windows-latest` 与 `ubuntu-latest` 上编出来，约一分钟 —— 它的 SDK 是
  纯头文件的 INTERFACE 库，只编我们自己的代码。
- **`carriers/selfhosted-linux/`** 在 `ubuntu-latest` 上构建与装配；`verify-live` 的 `selfhosted`
  输入用预加载垫片起 BDS。
- **`verify-lip`**（手动）在 `ubuntu-latest` 上用 lip 装 Linux 变体并检查落盘。
- **`verify-live`**（手动）下载 BDS，装 Endstone 与 lip 发布包，在 Windows 与 Linux 上起服，断言 CLR
  与载体起来、服务端正常停服。

更完整的交付/启停/卸载/重载仍由本地的 `smoke.ps1` 跑，需要一台真服务端。

## 分发

`tooth.json` 在**仓根**：lip 把 `tooth` 字段整个当成一个 Git 仓库来解析，仓根以下的东西它看不见。
一份包，四个变体：

| 变体 | 装什么 |
|---|---|
| 默认（不带 label） | 自持态 Linux 载体：`hosihikari-server` 放服务端根，原生垫片进 `plugins/hosihikari/` |
| `#levilamina` | LeviLamina 载体，落进 `plugins/hosihikari/`；托管文件由 Core 包补齐 |
| `#endstone` | Endstone Windows 载体：插件 dll 进 `plugins/`，`nethost.dll` 放到 `bedrock_server.exe` 旁边 |
| `#endstone_linux` | Endstone Linux 载体：插件 `.so` 与 `libnethost.so` 进 `plugins/` |

```
lip install github.com/HosihikariFramework/Shim                # 自持态 Linux
lip install github.com/HosihikariFramework/Shim#levilamina     # LeviLamina
lip install github.com/HosihikariFramework/Shim#endstone
lip install github.com/HosihikariFramework/Shim#endstone_linux
```

无后缀的包是自持态 Linux 载体；LeviLamina 改用带 label 的变体。每个变体都依赖
[Core 包](https://github.com/HosihikariFramework/PluginManagement)，lip 会把它装进
`plugins/hosihikari/`；原生垫片启动前会检查入口契约版本。发布附件只含原生文件；
`deploy.bat` 与 `deploy.sh` 在源码部署时还会从同级核心检出复制一份；版本来自 Git tag，
所以**要先有标签与对应的 release 才装得了**。
打 `v*` 会跑发布工作流，编出四个归档并挂到同一个 release。自持态的 post_install 脚本会给
`hosihikari-server` 加可执行位；跳过脚本时请自己 `chmod +x hosihikari-server`。

lip 解析版本走的是 **Go module proxy**（`proxy.golang.org`），不是直接看 git tag —— 所以刚推上去的
标签要等代理索引到才能装，在那之前 `lip install` 会报 "Failed to find the latest version"。
上面的变体就是这样装出来的。

## 还没做的

自持态 Windows 仍按 ADR-0022 暂缓。LeviLamina、Endstone（Windows 与 Linux）与自持态 Linux
都已就位。

## 许可

LGPL-3.0-or-later，见 [LICENSE.md](LICENSE.md)。
