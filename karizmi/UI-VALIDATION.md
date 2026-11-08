# Shared UI repair — 2026-11-05

## Current validation context

An earlier version of Karizmi EMS was successfully tested by the developer using three physical laptops connected over a local area network. This is a developer-reported functional testing milestone and does not constitute comprehensive validation of the latest build.

Latest physical discovery/network-recovery repairs remain unconfirmed; multi-device acceptance and independent security review are outstanding. Historical results below retain their original scope. Current behavior is consolidated in [TECHNICAL-SPECIFICATION.md](TECHNICAL-SPECIFICATION.md). A subsequent preparation rebuild of both applications and all four automated suites passed using existing dependencies; see [preparation audit](PORTFOLIO-AUDIT.md). The rebuilt candidate has no new physical-device acceptance and does not replace the historical baseline by implication.


The shared Win32 interface in `src/app.cpp` scaled fonts and child controls without consistently sizing the main window. Previously packaged executables also linked an older shared UI library because `Build-Apps.ps1 -SkipCore` skipped its rebuild. At the development display's 240 DPI (250%), controls extended beyond the old 620-by-610-pixel window.

Both roles now use explicit Segoe UI LOGFONT sizes, a DPI-scaled client area, shared responsive layout, minimum window dimensions, WM_SIZE and WM_DPICHANGED handling, readable colors, and role-specific visible controls. Confirmation adds room for its phrase and buttons. The build script now rebuilds shared components even when dependency bootstrapping is skipped.

## Results

| Check | Result |
| --- | --- |
| Teacher and Student x64 build | Passed with installed VS 2026/v145 |
| Core, Security, Network tests | Passed |
| Native UI layout regression | Passed: 60 role/DPI/state/width combinations |
| Simulated DPI | 96, 120, 144, 192, 240 (100%, 125%, 150%, 200%, 250%) |
| Layout assertions | Visible control containment, overlap, font size, text fit; ready and confirmation layouts at 520, 560, 720 logical-pixel widths |
| Real packaged Teacher and Student windows | Launched separately and screenshots visually inspected at actual 250% scaling; complete titles, branding, labels and buttons |
| Advanced | Real dialog opened in both applications |
| Teacher Connect | Eight-digit input reached runtime firewall-setup validation |
| Disconnect / Exit | Invoked on both unpaired applications; processes exited |
| Paired control/reconnect and firewall setup/removal | Not exercised in this UI smoke test |
| Actual Windows display changes / multiple monitors | Not tested; synthetic WM_DPICHANGED exercises application layout without changing system scaling |
| Two-computer acceptance / security review | Still outstanding |

## Artifacts and reproduction

- Executables: `out/Teacher/KarizmiTeacher.exe`, `out/Student/KarizmiStudent.exe`. Keep Student with its packaged `vnchooks.dll`.
- Build log: `build/ui-build.log`.
- Test log: `build/release/Testing/Temporary/LastTest.log`.
- Actual window screenshots: `build/ui-after/Teacher/window.png`, `build/ui-after/Student/window.png`.
- Synthetic layout captures: `build/release/ui-screenshots/`. Their window decorations still use the real monitor DPI; these captures do not establish native title-bar behavior at other system settings.
- Build: `powershell -NoProfile -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1 -SkipCore -SkipDependencies` from the repository root, then `ctest --test-dir karizmi/build/release --output-on-failure` with CMake on PATH.
- GUI check: `python karizmi/tools/inspect-ui.py <executable> <output-directory>` (requires Pillow and an interactive Windows desktop). This launches only the specified app, accepts Student's notice for the local unpaired check, captures its window, checks actions, and exits it.

Changes and outputs are in `D:\project\VNC\Karizmi-Exam-Source\UltraVNC-main`, not the separate `D:\project\VNC\UVNC` checkout. Existing release gaps in README remain applicable.
