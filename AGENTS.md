# AGENTS.md

Native hosting layer for the Hosihikari Framework: locate hostfxr, start the CLR inside BDS, hand
control to the managed core, and provide the LeviLamina, Endstone, and self-hosted Linux carriers.

## Repository map

- `common/` — shared native hosting code.
- `carriers/levilamina/` — xmake carrier for LeviLamina.
- `carriers/endstone/` — CMake carrier for Endstone on Windows and Linux.
- `carriers/selfhosted-linux/` — CMake carrier and launcher for the self-hosted Linux server.
- `verify/` — host-glue, host DLL, and injector checks for the native to managed hop.
- `tooth.json` — lip package with four variants.

## Build and verify

```powershell
carriers\levilamina\build.bat
carriers\endstone\build.bat
cmake -S verify -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

```bash
bash carriers/endstone/build.sh
bash carriers/selfhosted-linux/build.sh
bash carriers/endstone/smoke.sh <server root>
```

CI also runs `verify-lip`, `verify-live`, and `verify-providers`. Live tests need a server root and
the released lip package; they are manual workflows.

## Invariants

- Release archives contain native files only. The managed core comes from the PluginManager lip
  dependency.
- All four variants keep their labels and dependency range in sync.
- The hosting contract version is 1. Apply carrier-specific changes to every affected carrier.
- Do not commit build trees, downloaded SDKs, or server roots.
- Public default branches are protected. Work on a branch, open a pull request, and squash merge.

## Context

- [Design record](https://github.com/HosihikariFramework/Design) — GLOSSARY and ADRs.
- [Documentation](https://hf.srcz.one/) — quick start and carrier setup.
