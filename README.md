# Hosihikari.Shim

Hosihikari 框架的**原生层**：把 CLR 托管进 BDS 进程，然后把控制权交给核心
[`Hosihikari.PluginManager`](https://github.com/HosihikariFramework/PluginManagement)。

它只做四件事：定位 hostfxr、以核心的 `runtimeconfig.json` 初始化运行时、载入核心程序集、取托管入口。
其余一切（发现、依赖、隔离、生命周期）都在托管侧。

## 这个仓里有什么

| | |
|---|---|
| `common/` | 启动序列 —— **载体无关**，各载体的目标都编这一段 |
| `verify/nethost/` | 一个只为拉取 runtime pack 的小工程 —— nethost 的头文件与静态库**按版本从 NuGet 取**，不随仓带二进制 |
| `verify/` | 证明「原生 → 托管」这一跳的验证程序与脚手架 |

## 跑一遍

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

## CI 覆盖不到的部分

**这个仓不像另外两个那样全绿就等于全好。** 托管侧有十四条断言护着、跑在 CI 上；这里只有上面那一个验证程序，
而且它只覆盖**启动序列**（那一跳，与载体无关）。

四个载体的目标 —— LeviLamina mod、Endstone plugin（Windows 与 Linux）、自持态 Linux ——
都要在真实 BDS 与真实父级里才验得了，**CI 跑不到**。改动到那部分时，本地验证是唯一保障。

## 还没做的

四个载体目标本身。`common/` 已经就位，它们各自要做的是：接上自己的入口（父级 C++ 子类 / `EntryPoint` 符号 /
`__libc_start_main` 拦截），并满足父级对清单与构建的要求。

## 许可

LGPL-3.0-or-later，见 [LICENSE.md](LICENSE.md)。
