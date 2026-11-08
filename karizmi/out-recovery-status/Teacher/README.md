# Karizmi Teacher and Student

Karizmi is an offline Windows LAN screen sharing application built from the UltraVNC capture and viewer engines. The Student asks for consent before making a temporary pairing code available. Both users compare and confirm a phrase before a screen session starts. Teacher starts every connection and reconnection in view-only mode. Student input remains local and can only be controlled after Teacher requests control and Student acknowledges that control is available.

This checkout includes the implementation and reproducible build scripts. It is a development preview, not a security-reviewed release. The code has not been audited by an independent reviewer or tested between two Windows computers. Do not use it for a live exam until those checks and the outstanding firewall and input-control work below are complete.

## Build

Requirements: Windows 10/11 x64, Visual Studio C++ build tools, Windows SDK, CMake, Ninja, Python 3.12+, and the bundled vcpkg manifest. The current development machine has Visual Studio 2026/v145. Set `-PlatformToolset v143` on a machine with VS2022 if available.

Run from the repository root in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1
```

The script builds Botan 3.13.0 against the pinned archive checksum in `dependencies.json`, restores the root vcpkg manifest, builds the shared components, then builds and packages the x64 Teacher and Student executables. Outputs go to `karizmi/out/Teacher` and `karizmi/out/Student`. The Student package contains `vnchooks.dll` for screen capture. The upstream project files are not rewritten for this build; `Karizmi.Engine.props` applies Karizmi-only settings.

## Runtime

Both computers must be on an IPv4 Ethernet or Wi-Fi LAN that permits peer traffic. Student setup requests UAC approval to create temporary, executable-scoped Windows Firewall rules. Teacher initiates connections. Discovery uses UDP 45900, pairing/recovery TCP 45901, and encrypted screen/control TCP 45902. The current UI does not expose port configuration. Wi-Fi client isolation, explicit firewall blocks, disabled local-rule merging, or managed policy can prevent connections.

Student starts in the tray after its notice; the floating indicator has been removed. Its tray shows monitoring/control status and provides Open, Disconnect, and Exit. New pairing still requires visible confirmation. Teacher now has a discovery list and a grid of up to ten students. Interrupted sessions reconnect throughout the current exam until Disconnect or Exit; application relaunch can recover approved peers during the same Windows sign-in; Windows restart/sign-out requires fresh pairing. See [CLASSROOM.md](CLASSROOM.md) for protocol 3 compatibility, controls, architecture, and validation limits.

Teacher closing the main window keeps it in the notification area. Explicit Exit ends sessions and requests cleanup. Student includes an optional automatic Windows service; see [SERVICE.md](SERVICE.md) for installation, removal, and console-session limitations. Running the portable executable alone does not install the service.

## Validation record

Shared Teacher/Student font and DPI layout repairs, screenshots, and validation limits are documented in [UI-VALIDATION.md](UI-VALIDATION.md).

| Check | Result |
| --- | --- |
| Shared C++20 x64 components compile | Passed on the development machine |
| Core session and rate-limit tests | Passed |
| Botan SRP/TLS security tests | Passed |
| Encrypted RFB framing, wrong-key rejection, and disconnect tests over local test sockets | Passed |
| Teacher x64 executable build | Passed with VS 2026/v145 |
| Student x64 executable build | Passed with VS 2026/v145 |
| Windows Firewall behavior and UAC cleanup on a separate Windows installation | Not tested |
| Two-computer pairing, screen capture, input control, recovery, or packet capture | Not tested |
| Independent security review | Not performed |

## Known release gaps

- Ports are fixed at the planned defaults; advanced port configuration and transactional listener/rule changes are not implemented.
- Firewall rules are read back and scoped, but a protected ownership manifest and cleanup of rules from a relocated executable are not implemented. Firewall replacement attempts rollback; rollback itself can fail under policy/service errors.
- Discovery scans current IPv4 adapters on demand. Adapter-change notifications and a persistent adapter-specific discovery retry loop are not implemented.
- Student accepts bootstrap handshakes serially; the planned four concurrent-handshake limit is not implemented. Failed-attempt limits are enforced in memory, so restarting the application clears them.
- The Teacher shortcut is handled by its application message loop; filtering every Ctrl/Shift key event before it reaches the remote-view window needs acceptance testing and hardening.
- View-only/control transitions, key/button release, indicator behavior, shutdown, and the firewall helper need real Windows UI and failure-injection testing.
- The current packaging build proves compilation and linking only. It does not establish a secure deployment, successful firewall configuration, or working classroom connection.

See `tests/` for the automated test sources and the repository's `LICENSE` for UltraVNC attribution and GPL terms. Botan's license is shipped separately in each package.

See [RECOVERY-VALIDATION.md](RECOVERY-VALIDATION.md) for viewer-close behavior, same-sign-in approval recovery, crash evidence, and current test limits. Exit preserves approval during the current Windows sign-in; Disconnect and End exam revoke it.
