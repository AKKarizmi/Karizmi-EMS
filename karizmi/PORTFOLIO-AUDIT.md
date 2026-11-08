# Public portfolio preparation audit

Prepared 2026-11-08 (Asia/Kabul). No application source, protocol, authentication, cryptography, or security control was changed. No commit, tag, GitHub metadata update, release, or upload was performed.

## Findings and changes

- Root README now presents Karizmi EMS, its actual architecture, use, evidence boundaries, roadmap and screenshot plans.
- The supposedly missing Build-Apps.ps1 and Build-Core.ps1 existed locally but were excluded by the broad `*build-*` ignore rule. Explicit exceptions now expose both scripts and the build guide to Git. The existing legacy root Build-Karizmi.ps1 is also exposed and documented as a separate upstream-style workflow.
- Build-Apps now checks tools/environment, requires restored engine libraries/notices, runs tests when reusing Botan, and collects actual vcpkg copyright notices. A package-specific README avoids broken repository-relative navigation in shipped packages. Build-Core adds tool/Python checks. Existing architecture and output locations remain.
- Current README/specification/recovery/UI summaries record the earlier developer-reported three-laptop milestone without altering historical test tables. Classroom protocol heading and shortcut routing now agree with current source/specification. Historical packaged documents remain untouched.
- Added release plan/template, actual archive inventory/checksums, third-party review, portfolio/LinkedIn draft and contribution guide. No release version was invented.
- Added ignores for future generated dependency/package trees and sensitive diagnostics. Existing tracked packages remain preserved; ignore rules do not untrack them.

## Build and test evidence from this preparation

The first candidate build encountered a CMake cache copied from a different absolute source path. The entire old `karizmi/build/release` directory was preserved as ignored `karizmi/build/release-before-portfolio`, then a new cache was configured in this checkout. No source, historical log, or binary package was deleted.

The shared components and all four test executables compiled with installed MSVC 19.51/v145. Initial sandbox CTest: core, security and UI passed; network failed at `store.save(record)`. Standalone network tests outside the sandbox passed. Repeating the candidate build outside the sandbox passed all four CTest suites (core, security, network, ui_layout). This supports a sandbox access limitation, not a change to application approval security. The existing VS developer environment emitted a `vswhere.exe` lookup warning but initialized the compiler successfully.

Command: `powershell -NoProfile -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1 -SkipCore -SkipDependencies -PackageFolder out-portfolio-check`.

Dependencies were reused from local Botan/vcpkg builds. A fresh network dependency restoration, full Botan recompilation, VS2022/v143 build, clean-machine installation and byte-for-byte reproducibility were not tested. Automated tests are not physical classroom acceptance.

## Archive and licensing findings

The combined out-recovery-status ZIP and Student ZIP contain binaries matching the unpacked baseline and specification hashes. SHA-256 values and ZIP entry names were calculated from actual bytes; see ARCHIVE-INVENTORY.json and RELEASE-PLAN.md. The RAR was hashed only, not extracted. The older out-network-repair tree was retained. Teacher options.vnc has an empty index only.

Historical packages include GPL/Botan notices but lack newly collected dependency notices and demonstrated exact source-commit provenance. Current modified engine/application sources and build inputs are present; complete corresponding source for historical binaries is not established. Independent license/provenance review, transitive/embedded component review, and a source archive tied to final assets remain required. Upstream copyright notices and the root GPL text were preserved.

## Remaining human work

- Reproduce/close latest physical discovery and network-recovery failures; test latest packages on independent devices.
- Validate two live Students and input isolation, then actual ten-desktop capacity; test adapter changes, firewall/UAC cleanup, service and sign-in/sleep lifecycle, and rotation failure boundaries.
- Obtain independent security review and confirm final source/licensing/runtime dependency completeness.
- Capture real demonstration images using test data. Historical local captures/logs are not newly published evidence.
- Decide whether to relocate/remove existing tracked package archives; no such removal is authorized here. Review full Git history separately for sensitive content; a narrow working-tree scan cannot certify history.
- Choose an exact source identifier, prepare final ZIP/source assets and hashes, then explicitly authorize publishing a Development Preview.

## Changed-file map

| Files | Purpose |
| --- | --- |
| README.md, BUILD-KARIZMI.md, CONTRIBUTING.md | Project presentation, current/legacy build distinction, contribution expectations |
| .gitignore | Expose required scripts/guides; ignore generated and sensitive local artifacts |
| Build-Karizmi.ps1 | Existing local legacy script newly visible to Git; no application changes |
| karizmi/tools/Build-Apps.ps1, Build-Core.ps1 | Existing local build scripts exposed, checked and repaired |
| karizmi/README.md, TECHNICAL-SPECIFICATION.md, RECOVERY-VALIDATION.md, UI-VALIDATION.md | Current validation context; historical records preserved |
| karizmi/CLASSROOM.md | Current protocol and shortcut guidance |
| karizmi/PACKAGE-README.md, THIRD-PARTY-NOTICES.md | Package instructions and licensing review |
| karizmi/RELEASE-PLAN.md, RELEASE-NOTES-TEMPLATE.md, ARCHIVE-INVENTORY.json | Preview preparation and actual archive evidence |
| karizmi/PORTFOLIO.md, PORTFOLIO-AUDIT.md | Sharing copy, capture plan and audit record |

## Final verification results

The candidate rebuild completed successfully: Teacher, Student, capture hooks, and Student service were packaged under ignored `karizmi/out-portfolio-check/{Teacher,Student}`. Both roles include seven actual installed copyright files. Existing compiler warnings (mostly narrowing conversions and macro redefinitions) remain; the build was successful, not warning-free. No GUI or physical-device acceptance was conducted on these newly rebuilt packages.

PowerShell AST parsing passed for all Karizmi tool scripts. Repository local Markdown paths and candidate package Markdown paths were checked. Build command targets exist. `git diff --check` passed. A narrow scan of 784 tracked text/source files found no private-key headers, AWS access-key identifiers, or GitHub-token patterns; this is not a comprehensive secret scan and excludes full Git history and opaque archive contents. No credentials or diagnostic dumps were added.

Candidate binary identities (calculated from rebuilt files; no archive release created):

| Candidate file | SHA-256 |
| --- | --- |
| `Teacher/KarizmiTeacher.exe` | `6b8b2f54c0ec9d39da31edcf62ded4491c1736839419ec337e05dd52cac4ef31` |
| `Student/KarizmiStudent.exe` | `f2278ba5b815bfed217b5f171408f2503da69cb0a658daed1aba9fc98d2a745e` |
| `Student/KarizmiStudentService.exe` | `61f0d7bc5cf4b64bd40052d14d471246100f58a270a9e231eaa5128fb8a1b867` |
| `Student/vnchooks.dll` | `a09426f9a15046b552284a9338ca5bbed65b7901f691aaa5dc09fd220008b6e5` |
