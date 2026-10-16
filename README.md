# Hosihikari.Shim

Hosihikari 框架的**原生层**：把 CLR 托管进 BDS 进程，然后把控制权交给核心
[`Hosihikari.PluginManager`](https://github.com/HosihikariFramework/PluginManagement)。

它只做四件事：定位 hostfxr、以核心的 `runtimeconfig.json` 初始化运行时、载入核心程序集、取托管入口。
其余一切（发现、依赖、隔离、生命周期）都在托管侧。

## 这个仓里有什么

| | |
|---|---|
| `common/` | 启动序列 —— **载体无关**，各载体的目标都编这一段 |
| `common/nethost/` | 随仓携带的 nethost 头文件与静态库（来自 .NET SDK 的 runtime pack，MIT） |
| `verify/` | 证明「原生 → 托管」这一跳的验证程序 |

## 跑一遍

```
cmake -S verify -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
./build/Release/host-glue.exe <托管侧输出目录>
```

它会走完启动序列并调用托管的入口，四步各自报告成败。托管侧先用
`dotnet build src/Hosihikari.PluginManager.csproj -c Release` 编出来。

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
