# Build Karizmi Teacher and Student

Use the custom build entry point below. The root `Build-Karizmi.ps1` is an older upstream-style engine build producing `vncviewer.exe`/`winvnc.exe`; it does not package the current classroom applications.

## Prerequisites

- Windows 10/11 x64; short writable checkout path.
- Visual Studio C++ Build Tools and Windows SDK. Historical validated toolset: VS 2026/v145. VS 2022/v143 can be selected for the engine projects but has not been acceptance-validated. Shared CMake/Botan builds use the latest installed VS environment; do not claim that selecting the engine toolset also selects their compiler.
- CMake 3.24+, Ninja, Python 3.12+ on PATH (or supplied by the VS developer environment). Python needs secure tar extraction support.
- Git for checkout/submodules and vcpkg restoration; NASM on PATH for applicable dependency builds. vcpkg is taken from the detected VS installation, otherwise PATH.
- Network access initially for the pinned Botan archive and vcpkg dependencies. See [dependency lock](karizmi/dependencies.json), [manifest](vcpkg.json), and [engine props](karizmi/Karizmi.Engine.props).

```powershell
git clone --recurse-submodules https://github.com/AKKarizmi/Karizmi-EMS.git
cd Karizmi-EMS
powershell -NoProfile -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1
```

The script imports the latest VS x64 environment, checks tools, builds pinned static Botan, builds/tests C++20 components with CMake/Ninja, restores static x64 vcpkg dependencies, builds both engines plus capture hooks, and copies packages. Existing source already contains guarded engine hooks; do not rerun the one-time `integrate_engine.py` migration.

## Outputs and rebuild options

- `karizmi/build/release`: shared static libraries, service executable, tests.
- `karizmi/build/engine/{vncviewer,winvnc,vnchooks}`: engine products.
- `karizmi/out/Teacher`: `KarizmiTeacher.exe`, documentation and notices.
- `karizmi/out/Student`: `KarizmiStudent.exe`, `vnchooks.dll`, service executable/install scripts, documentation and notices.

Use a **new** package directory for release candidates so old unowned files cannot enter an archive:

```powershell
# Existing Botan and vcpkg dependencies required; shared code is still rebuilt and tested.
powershell -NoProfile -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1 -SkipCore -SkipDependencies -PackageFolder out-preview-candidate
# Optional engine toolset selection only, if installed:
powershell -NoProfile -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1 -PlatformToolset v143
ctest --test-dir karizmi/build/release --output-on-failure
```

`-SkipCore` skips Botan bootstrapping, not the shared-code rebuild. `-SkipDependencies` skips vcpkg restoration and requires existing libraries/notices. `Build-Core.ps1 -CoreOnly` is a reduced development test build without security/network/UI components, not an application package configuration. Do not reuse its CMake cache as evidence for full coverage.

If tools are absent, install prerequisites; if libraries or notices are absent, rebuild without skip switches. Compiler/toolset mismatch or a cache from a different absolute checkout can require a fresh build directory after preserving local diagnostics. Build failures must not be reported as successful releases. These instructions support rebuilding; deterministic binary reproduction and pristine-machine acceptance remain outstanding. See [audit results](karizmi/PORTFOLIO-AUDIT.md).
