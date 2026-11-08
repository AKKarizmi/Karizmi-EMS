# Karizmi Student service

Student supports an optional automatic Windows service and a tray icon. Teacher behavior is unchanged. The process descriptions are **Karizmi Student** and **Karizmi Student Service**, and the service name is `KarizmiStudent` (display name **Karizmi Student**). UltraVNC copyright and license attribution remain in the package.

From the built `out/Student` package, run PowerShell as administrator:

```powershell
.\Install-StudentService.ps1
```

The installer copies the package into `%ProgramFiles%\Karizmi Student`, protects it against writes by standard users, and creates/starts an automatic LocalSystem service. It refuses an existing service or installation directory rather than overwriting another installation. Never register the service executable directly from a user-writable download or build directory.

The service runs at boot. It launches the Student application under the signed-in **console user's** account, allowing up to 30 seconds for a new launch attempt. The application still asks for the monitoring notice to be accepted and requires secure pairing and confirmation. After the notice it starts in the tray. Closing its window leaves the tray icon available, with monitoring/control status and Disconnect; no floating indicator is shown. Tray actions are Open, status, Disconnect, and Exit. Exit or Cancel suppresses automatic relaunch for that logon until the service is restarted. Student can also be started manually.

Stopping the service signals its own Student process to disconnect and exit, with a five-second fallback termination of its job. Console-session changes stop that process. The service does not adopt manually started instances. It does not open VNC ports, bypass pairing, configure firewall rules, or expose a privileged command channel. Firewall setup still uses its existing UAC helper. Following installation at the new executable path, run **Set up exam connection** again; old-path firewall rules do not apply.

This is service-backed startup for the classroom application, not unattended control of the Windows sign-in/UAC secure desktop. There is no capture before a user signs in and accepts the notice. Remote Desktop sessions and multi-user capture are not supported by this console supervisor. Separating the service from the user interface follows [Microsoft's interactive-service guidance](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services).

To uninstall, run the package's script as administrator:

```powershell
.\Uninstall-StudentService.ps1
```

It stops and removes only the named service at the expected installation path and removes the known package files. It does not recursively delete directories or remove other VNC installations. Use Student's **Remove access** before uninstalling if firewall cleanup is needed.

## Validation

Validated on 2026-11-06:

| Check | Result |
| --- | --- |
| x64 Student, service, and Teacher rebuild | Passed with installed VS 2026/v145 |
| Core, Security, Network, 60-case UI layout tests | All passed |
| Student and service file descriptions/product/original filename | Verified as Karizmi names |
| Install automatic LocalSystem service into protected Program Files directory | Passed |
| Launch console-session Student under signed-in user rather than SYSTEM | Passed |
| Stop with pending monitoring notice, restart, uninstall | Passed; test service and installation removed |
| Student tray registration, close/reopen, tray-only startup, Advanced, Disconnect/Exit | Passed |
| Student service-stop event after accepted notice | Passed: graceful process exit |
| Teacher tray and button regression | Passed |
| Reboot/logon, fast user switching, service failure injection | Not tested |
| Two-computer capture/control/recovery | Not tested |

Evidence: `build/service-build.log`, `build/service-test.log`, `build/release/Testing/Temporary/LastTest.log`, and window captures in `build/service-ui/`. `tools/Test-StudentService.ps1` requires administrator rights, refuses existing services/running Student processes, and removes its temporary installation in a finally block. No service is left installed by that test. Boot/logon and two-computer monitoring still require Windows acceptance testing.

The protocol 2 classroom changes and their newer validation results are documented in CLASSROOM.md. The service lifecycle results above predate those UI changes.
