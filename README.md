# Hosihikari.Shim

[English](README.md) | [简体中文](README.zh_hans.md)

The **native layer** of the Hosihikari framework, named **HostingShim** (the CLR hosting shim): it
hosts the CLR inside the BDS process and then hands control to the core,
[`Hosihikari.PluginManager`](https://github.com/HosihikariFramework/PluginManager).

It does four things: locate hostfxr, initialise the runtime from the core's `runtimeconfig.json`,
load the core assembly, and fetch the managed entry point. Everything else — discovery,
dependencies, isolation, lifecycle — happens on the managed side. It does not project carrier
APIs; that side is **BindingShim**, produced outside this repository (ADR-0021).

## What is in this repository

| | |
|---|---|
| `common/` | The startup sequence — **carrier-independent**; every carrier target compiles this same code |
| `carriers/` | The carrier targets: `levilamina/` (xmake), `endstone/` (CMake, Windows and Linux), and `selfhosted-linux/` (CMake) |
| `verify/` | Programs and scaffolding that prove the native → managed hop |

nethost headers and libraries are **fetched by version from NuGet**, never committed — both the
carriers and `verify/` download their own copy at build time.

## Running it

### Carrier: LeviLamina

```
carriers\levilamina\build.bat
carriers\levilamina\deploy.bat <server root>
```

`build.bat` finds vcvars through `vswhere` and puts clang-cl on the path. On networks that need a
proxy, set `https_proxy` first — the script leaves that to the caller. The artifact and its manifest
are placed in `bin/hosihikari/` by the upstream `@levibuildscript/modpacker` rule; `deploy.bat` then
copies them, the managed core and `nethost.dll` into `plugins/hosihikari/`.

A full live pass — start the server, drive `show`/`disable`/`enable`/`unload`/`load`, then check the
log:

```
powershell -File carriers\levilamina\smoke.ps1 -ServerRoot <server root>
```

It first runs the managed assertion host (which also produces the plugin packages), so a sibling
`Hosihikari.PluginManager` checkout is required. It needs a server with LeviLamina already
installed, and it **will not kill somebody else's server** — if one is already running it stops
with an error.

The Endstone carrier has its own live pass. It starts the server through the Endstone CLI, which
injects the runtime into the vanilla `bedrock_server.exe`; the CLI and Node must be available:

```
powershell -File carriers\endstone\smoke.ps1 -ServerRoot <Endstone server root>
```

It stages the loader package and two Node plugins, drives `plugins` / `endstone:disable` /
`endstone:enable` / `stop`, and checks the plugin files, the crash fault and the carrier's removal
warning. The CLI is looked up at `%TEMP%\endstone-venv\Scripts\endstone.exe`, then `ENDSTONE_EXE`,
then PATH.

A shorter carrier-only pass runs against an already installed lip package, without staging
loaders or plugins:

```
powershell -File carriers\endstone\smoke-live.ps1 -ServerRoot <server root>
bash carriers/endstone/smoke.sh <server root>
```

The Linux script uses `ENDSTONE_EXE` or `endstone` on PATH. Both assert that the CLR comes up, the
carrier takes over, the parent enables it, and the server stops cleanly.

### Carrier: Endstone

Windows:

```
carriers\endstone\build.bat
carriers\endstone\deploy.bat <folder containing bedrock_server.exe>
endstone -s <folder containing bedrock_server.exe>
```

Linux:

```
bash carriers/endstone/build.sh
bash carriers/endstone/deploy.sh <folder containing bedrock_server>
endstone -s <folder containing bedrock_server>
```

`build.sh` needs CMake and a C++20 compiler; the SDK and nlohmann/json are shallow-cloned by CMake,
and nethost is fetched from nuget.org. `deploy.sh` copies the plugin, `libnethost.so` and the
managed core; the plugin is linked with `$ORIGIN`, so `libnethost.so` sits beside it.

Three structural differences from the LeviLamina carrier, all dictated by the parent:

- **Plugins are loaded by file**, so the carrier itself is a single `plugins/hosihikari.dll` (or
  `plugins/hosihikari.so`); the managed core still lives in `plugins/hosihikari/` (the data folder
  the parent assigns by plugin name). Our packages are directories, so the loader overrides
  `loadPlugins`.
- **The nethost library must be resolvable**: on Windows `nethost.dll` sits next to
  `bedrock_server.exe`, because Endstone loads plugins with a plain `LoadLibrary` and resolves
  dependencies from the application directory. On Linux `libnethost.so` sits next to the plugin and
  is found through `$ORIGIN`. The LeviLamina carrier has no such problem because that parent adds
  the mod directory to `PATH`.
- **Plugin names are normalised**: Endstone accepts `^[a-z0-9_]+$` and forbids a leading `endstone`,
  so `Bar` appears to the parent as `bar` while the managed side still tracks it by its manifest
  name.

### Carrier: self-hosted Linux

```
bash carriers/selfhosted-linux/build.sh
bash carriers/selfhosted-linux/deploy.sh <folder containing bedrock_server>
./hosihikari-server
```

`hosihikari-server` preloads `plugins/hosihikari/libhosihikari.so` with `LD_PRELOAD`. The shim
intercepts `__libc_start_main`, starts the CLR and the core before BDS' `main`, and the core scans
`plugins/` itself (ADR-0005 / 0013 / 0018). The launcher `cd`s to the server folder, so the core's
relative paths land on the right server root; it needs the .NET 10 runtime installed.

The self-hosted target is BDS 1.21.3: the container itself is BDS-version-agnostic, but the
capability surface (星光) stops there (ADR-0001).

Self-hosted plugins are precompiled assembly packages; there is no runtime enable/disable
(ADR-0020). A carrier-only live pass:

```
bash carriers/selfhosted-linux/smoke.sh <folder containing bedrock_server>
```

### Native seam (carrier-independent)

```
cmake -S verify -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
./build/Release/host-glue.exe <managed output directory>
```

It walks the startup sequence and calls the managed entry point, reporting each of the four steps.
Build the managed side first with `dotnet build src/Hosihikari.PluginManager.csproj -c Release`.

`verify/host-dll/` and `verify/injector/` are the same thing inside a **real BDS process**: the
first builds the startup sequence as a DLL with an exported entry point, the second injects it into a
suspended `bedrock_server.exe` and calls that export.

> `injector` is **verification scaffolding, not a deliverable**, and is not meant to become a
> general-purpose injector. It exists because ADR-0022 defers "who loads us in production" (PreLoader
> only offers `DllMain`, which conflicts with ADR-0016), not "whether we can be loaded".

## What CI covers

- **The three `verify/` targets** (host-glue, host-dll, injector) build on `windows-latest`.
- **`carriers/levilamina/`** builds on `windows-latest`. It pins the LLVM version and caches
  `~/AppData/Local/.xmake`: upstream's `levilamina` package ships no prebuilt binaries, so the first
  run compiles LeviLamina from source (about ten minutes) and later runs reuse the cache.
- **`carriers/endstone/`** builds on `windows-latest` and `ubuntu-latest` in about a minute — its
  SDK is a header-only interface library, so only our own code is compiled.
- **`carriers/selfhosted-linux/`** builds and deploys on `ubuntu-latest`; `verify-live` with the
  `selfhosted` input starts BDS through the preloaded shim.
- **`verify-lip`** (manual) installs the Linux variant with lip on `ubuntu-latest` and checks the
  placement.
- **`verify-live`** (manual) downloads BDS, installs Endstone and the released lip package, starts
  the server on Windows and Linux, and checks that the CLR and carrier come up and the server stops.

Live carrier behaviour is covered by the manual `verify-live` workflow. The fuller
delivery/enable/disable/unload/reload pass remains local via `smoke.ps1`.

## Distribution

`tooth.json` sits at the **repository root**: lip resolves the `tooth` field as a whole Git
repository, so anything below the root is invisible to it. One package, four variants:

| Variant | What it installs |
|---|---|
| default (no label) | The self-hosted Linux carrier: `hosihikari-server` at the server root, the native shim into `plugins/hosihikari/` |
| `#levilamina` | The LeviLamina carrier into `plugins/hosihikari/`; the Core package adds the managed files |
| `#endstone` | The Endstone carrier on Windows and Linux: Windows places the DLL and `nethost.dll` next to `bedrock_server.exe`; Linux places the `.so` and `libnethost.so` into `plugins/`. `platform` selects the archive |

```
lip install github.com/HosihikariFramework/HostingShim                # self-hosted Linux
lip install github.com/HosihikariFramework/HostingShim#levilamina     # LeviLamina
lip install github.com/HosihikariFramework/HostingShim#endstone
```

The suffix-less package is the self-hosted Linux carrier; LeviLamina is the labeled variant. Every
variant depends on the [Core package](https://github.com/HosihikariFramework/PluginManager),
which lip installs into `plugins/hosihikari/`; the native shim checks the hosting-contract version
before it starts the runtime. The release assets contain only native files; the
`deploy.bat` and `deploy.sh` scripts also copy a sibling core checkout for source deployments; the
version comes from a Git tag, so **a tag and a matching release are required before any of this can
be installed**. Tagging `v*` runs the release workflow that builds all four archives and attaches
them to one release. The self-hosted post-install script marks `hosihikari-server` executable; if
scripts are skipped, run `chmod +x hosihikari-server` yourself.

lip resolves versions through the **Go module proxy** (`proxy.golang.org`), not git tags directly,
so a freshly pushed tag takes a while to become installable: until the proxy indexes it,
`lip install` reports "Failed to find the latest version". The variants above are installed this way.

## Not done yet

Self-hosted Windows remains deferred (ADR-0022). LeviLamina, Endstone on Windows and Linux, and
self-hosted Linux are in place.

## Licence

LGPL-3.0-or-later, see [LICENSE.md](LICENSE.md).
