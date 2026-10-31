# AGENTS.md

Native hosting layer for the Hosihikari Framework: locate hostfxr, start the CLR inside BDS, hand
control to the managed core, and provide the LeviLamina, Endstone, and self-hosted Linux carriers.

## Repository map

- `common/` — shared native hosting code.
- `carriers/levilamina/` — xmake carrier for LeviLamina.
- `carriers/endstone/` — CMake carrier for Endstone on Windows and Linux.
- `carriers/selfhosted-linux/` — CMake carrier and launcher for the self-hosted Linux server.
- `carriers/selfhosted-windows/` — CMake carrier and injecting launcher for the self-hosted Windows server.
- `verify/` — host-glue, host DLL, and injector checks for the native to managed hop.

## Build and verify

```powershell
carriers\levilamina\build.bat
carriers\endstone\build.bat
carriers\selfhosted-windows\build.bat
pwsh carriers\selfhosted-windows\smoke.ps1 -ServerRoot <server root>
cmake -S verify -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

```bash
bash carriers/endstone/build.sh
bash carriers/selfhosted-linux/build.sh
bash carriers/endstone/smoke.sh <server root>
```

CI also runs `verify-live` and `verify-providers`. Live tests need a server root and the released
archives; they are manual workflows.

## Invariants

- All carriers build as C++23. LeviLamina headers already use `std::to_underlying`; keep the CMake
  and xmake standards aligned.
- Release archives contain native files only. The managed core comes from the PluginManager release
  archive.
- Every release archive extracts at the server root: LeviLamina writes `plugins/hosihikari/`,
  Endstone Windows writes `plugins/hosihikari.dll` and `nethost.dll`, Endstone Linux writes
  `plugins/hosihikari.so` and `plugins/libnethost.so`, self-hosted Linux writes
  `hosihikari-server` and `plugins/hosihikari/`, and self-hosted Windows writes
  `hosihikari-server.exe` and `plugins/hosihikari/`.
- The self-hosted Windows shim links nethost statically: the archive carries no `nethost.dll`.
- The hosting contract version is 1. Apply carrier-specific changes to every affected carrier.
- Do not commit build trees, downloaded SDKs, or server roots.
- Public default branches are protected. Work on a branch, open a pull request, and squash merge.

## Context

- Design decisions and terminology are kept by maintainers outside the public repositories; ask for
  the relevant decision when changing lifecycle or public API.
- [Documentation](https://hf.srcz.one/) — quick start and carrier setup.
