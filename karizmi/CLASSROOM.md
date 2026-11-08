# Classroom preview — protocol 3

Teacher discovers Karizmi Students on the local IPv4 LAN, refreshes discovery every five seconds, and keeps up to ten reserved student sessions. Student starts in the tray after the initial monitoring notice. Approvals survive application restarts during the current Windows sign-in session; see RECOVERY-VALIDATION.md. Install both newly built packages together; protocol 1/2 builds are incompatible.

## Use

1. Start Student and accept its notice. Open **Karizmi Student** from its tray icon to see the eight-digit code and run **Set up exam connection**.
2. Start Teacher and run **Set up exam connection**. Eligible devices appear in the discovery list. Computer names in this list are **Unverified**; they are hints, not authenticated identities.
3. Select a device, enter its code, and choose **Pair selected**. Compare the authenticated phrase on both computers and confirm on each.
4. A tile represents each reserved Student. Double-click to open its screen. Right-click for **Open screen**, **Open full screen**, session details/confirmation, control, Reconnect now, or Disconnect. Session details also provide the visible view-only/control toggle. Ctrl+Shift+F12 targets the focused viewer; grid/session-details focus uses that context. See TECHNICAL-SPECIFICATION.md for current routing rules. The retained viewer offers Ctrl+Alt+F12 to toggle full-screen mode.
5. **End exam** disconnects all students. Closing Teacher hides it in the tray; **Exit** ends the application and its sessions.

Student's tray shows Ready, Monitored — View only, Monitored — Control on, or Interrupted. Disconnect and Exit remain available there. New pairing displays confirmation; ordinary reconnects are quiet. An Explorer restart notification causes tray re-registration; registration failure displays the main window. The tray icon may be in Windows' notification-area overflow.

## Session architecture

Each tile owns a Runtime, session state, encrypted transport, and viewer connection. Engine commands use the authenticated socket marker as a process-local session identifier. The marker is never a plaintext network bridge. A shared Teacher coordinator owns discovery, the firewall helper, and a maximum of four concurrent bootstrap/recovery attempts. Student retains exclusive ownership by one Teacher.

Discovery replies include protocol version, random instance ID, endpoint, and a bounded UTF-8 computer name. Unpaired entries expire after 15 seconds; the catalog is capped at 256 entries. Spoofed discovery cannot bypass PAKE, TLS, or mutual confirmation. Recovery uses the shared catalog plus the previous endpoint.

Background framebuffer requests are limited to one per second; the visible detailed view allows up to ten per second. Deferred requests coalesce into a timer rather than an unbounded queue. Tile captures are bounded 320×180 images refreshed at most once per second. Actual frame throughput depends on the capture engine, network, and changed pixels.

Control handoff disables local forwarding immediately, requests revocation from the previous Student, and waits for its acknowledgement (or connection loss) before enabling another Student. Late acknowledgements cannot re-enable a previous local target. Every initial connection and recovery starts in View Only.

An interrupted session keeps its memory-only reservation until Disconnect, End exam, Exit, or process termination. Heartbeats detect loss; retries back off through 1, 2, 4, 8, and ten seconds. Reconnect now does not create overlapping attempts. Reconnect authentication and credential rotation remain encrypted; a final acknowledgement erases the previous runtime credential, with a bounded fallback window if the acknowledgement is lost. A disconnected peer cannot be silently paired again.

Tiles retain their last thumbnail and label, with STALE and frame age during interruption/disconnection. If one peer explicitly disconnects while the network is down, the unreachable peer cannot receive that command: its own user must Disconnect/Exit to release its reservation. Restarting Windows/signing out requires fresh pairing; application relaunch during the same sign-in can resume an approved peer.

## Validation matrix

| Check | Evidence / limit |
| --- | --- |
| Teacher, Student, service x64 builds | `build/classroom-build.log`; VS 2026/v145 |
| Core | Code expiry, exclusivity, recovery retained after 24 simulated hours, rotation, rate limits, version/bounds checks |
| Security | Existing Botan SRP/TLS tests |
| Network | Ten simultaneous encrypted loopback transports with distinct keys, per-peer frames/control routing, one-peer disconnect isolation; wrong-key rejection |
| Discovery/coordinator | Name validation, changed-address deduplication, stale expiry, catalog cap, four-attempt limit and cancellation |
| UI layout | 60 existing role/state/width/DPI combinations plus eight classroom width/DPI combinations at 100%, 125%, 150%, 200% |
| Packaged UI | Local Teacher/Student smoke scripts and captures in `build/classroom-ui/` |
| Local paired viewer | Real packaged Teacher/Student: secure pairing, tile-open action, full-screen action, and retention beyond 30 seconds tested; see viewer-opening repair below |
| Explorer behavior | Synthetic TaskbarCreated notification; a real Explorer termination/restart is not part of the smoke test |
| Physical two-computer screen/control/recovery | Not run; a second Windows computer is unavailable in this environment |
| Ten live VNC students, capture performance, Wi-Fi switching, dropped rotation messages | Not acceptance-tested; ten encrypted transports do not establish ten complete working viewer/capture sessions |
| Independent security review | Not performed |

Run `ctest --test-dir karizmi/build/release --output-on-failure` with the installed CMake tools on PATH. `tools/inspect-ui.py` launches a packaged executable, accepts the initial Student notice for the unpaired smoke check, exercises tray/UI actions, captures the window, and exits. Add `--test-service-stop` for Student to test graceful shutdown through the service event.

This preview requires two-computer acceptance before classroom deployment. Existing firewall ownership/relocation, advanced-port, adapter-notification, Student serial-bootstrap, and input-hardening limitations documented in README remain applicable.

## Viewer-opening repair

Local acceptance exposed a quiet-screen read timeout that could close the legacy viewer while the authenticated session continued. Engine reads now wait while the encrypted channel remains alive; authenticated heartbeat failure and cancellation wake them. Hidden viewers retain their throttled updates, and viewer shutdown stops its encrypted channel so the session manager can recover instead of retaining an unusable connected tile.

The full-screen context-menu action uses the same selected-peer opening path as ordinary screen viewing. A separate confirmation race was fixed: once both users' decisions are known, the confirmation routine returns without consuming the next session message. Automated tests cover a delayed confirmation followed immediately by credential delivery and an idle reader that subsequently receives pixels.

`tools/test-local-viewer.py` runs an interactive, same-PC Teacher/Student acceptance test using the real packaged engines and normal firewall-helper UAC approvals. It pairs the applications, invokes tile double-click opening, verifies the context-menu full-screen action removes the viewer caption, and checks that the viewer remains after 35 seconds. Captures and logs are in `build/local-viewer/` and `build/local-viewer-test.log`; build output is in `build/viewer-fix-build.log`. This establishes local viewer behavior, not two-computer network switching or ten live desktop sessions.

The lifecycle, crash findings, revised Exit/Disconnect policy, and corrected acceptance results in [RECOVERY-VALIDATION.md](RECOVERY-VALIDATION.md) supersede earlier memory-only/restart and lifecycle claims above. Closing a detailed viewer now returns to the grid without ending monitoring.
