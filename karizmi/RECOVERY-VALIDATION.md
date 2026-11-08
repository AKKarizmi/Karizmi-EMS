# Viewer lifecycle and same-sign-in recovery — 2026-11-07

## Current validation context

An earlier version of Karizmi EMS was successfully tested by the developer using three physical laptops connected over a local area network. This is a developer-reported functional testing milestone and does not constitute comprehensive validation of the latest build.

Latest physical discovery/network-recovery repairs remain unconfirmed; multi-device acceptance and independent security review are outstanding. Historical results below retain their original scope. Current behavior is consolidated in [TECHNICAL-SPECIFICATION.md](TECHNICAL-SPECIFICATION.md). A subsequent preparation rebuild of both applications and all four automated suites passed using existing dependencies; see [preparation audit](PORTFOLIO-AUDIT.md). The rebuilt candidate has no new physical-device acceptance and does not replace the historical baseline by implication.


This release uses **protocol 3**. Update Teacher and Student together, including any separately installed service copy.

## Policy

| Action | Result |
| --- | --- |
| Close detailed viewer | Hide it, release control, return to grid; monitoring continues |
| Network/transport loss | Retain reservation, mark frame STALE, authenticate recovery, restore View Only |
| Application crash or termination | Approval survives in the current Windows sign-in session |
| Application Exit | Stop monitoring; preserve approval for relaunch during this sign-in |
| Disconnect | Revoke that pairing and require fresh pairing |
| End exam | Revoke all classroom pairings; Student generates a fresh code |
| Sign-out, restart, shutdown, or power loss | New sign-in has no usable approval; fresh identity, credentials, and Student code |
| Lock, sleep, or resume | Preserve the sign-in approval; recover in View Only |

Student's initial monitoring notice remains. Relaunch recovery still requires **Set up exam connection** / its normal UAC approval for the application run; it does not require an eight-digit code or mutual phrase confirmation. If only one computer restarts Windows, the other computer may retain its old unreachable reservation: Disconnect that stale pairing before pairing the new instance. Names/IP addresses never transfer authorization.

## Implementation

Approval blobs are DPAPI protected for the current user and held in ACL-restricted volatile registry keys under `HKCU\Software\Karizmi\SessionApprovals-<authentication-session-id>`. Teacher holds at most ten approvals, Student one. Records contain bounded identities/endpoints and current/pending rotation state; pairing codes are not stored. A new Windows sign-in uses a new scope; volatile data is not written as durable registry enrollment. Old/corrupt/incompatible records do not authorize connections.

Credentials are saved after mutual confirmation, and pending/current rotation state is saved before its acknowledgement/commit message. Explicit revocation cannot be undone by a late save. Partial recovery failures return to Recovering rather than leaving the session stuck as Connected. Disconnect/End exam remove approvals; ordinary application Exit preserves them.

Viewer operations run on the UI thread. Detach captures the intended marker once; commands carry connection generations and reject stale work. Connection-list lookup is synchronized and releases list synchronization before destroying/joining a viewer. Ordinary viewer Close does not destroy the transport. Legacy trust dialogs are bypassed only for a registered authenticated Karizmi stream; plaintext entry points remain rejected.

## Crash evidence and correction

Windows recorded repeated `KarizmiTeacher.exe` Application Error events with exception `0xc0000409`, including November 6 test cleanup and November 7. The original executable/PDB are retained in `build/forensics/`. DbgHelp resolves offset `0x2fc9f9` to `abort+0x35`; that address alone does not establish buffer corruption or identify the entire original call stack.

Inspection found an engine error path where encrypted stream closure returned a socket error instead of end-of-stream. The legacy reader could then throw a warning from its exception handler out of its worker routine. The adapter now reports end-of-stream, avoids that escaping-warning path for Karizmi, and disables independent legacy reconnect after read failure. Teardown and stale-command handling were corrected alongside it. These are substantive error-path/lifecycle corrections, not suppression of termination.

The archived WER report inspected contained `Report.wer` but no dump. No dump of the original fault has been obtained; subsequent clean runs must not be described as proving its precise historical stack. Termination/abort stack addresses now go to `%LOCALAPPDATA%\Karizmi\Diagnostics`. Setting `KARIZMI_CAPTURE_DUMP=1` explicitly enables a dump if that fault recurs. Dumps can contain session secrets: keep them local with restricted access, never publish them, and delete them after diagnosis.

A separate active-session Exit hang was reproduced and captured in `build/forensics/teacher-hang.dmp` with restricted file permissions. Before rebuilding, symbol inspection showed the UI thread waiting in viewer destruction and a viewer worker inside `NtUserSetWindowPos`/`ClientConnection::SizeWindow`. `hang-symbols.txt` records the stack-memory symbol scan (candidate return addresses, not a fully unwound call stack). Shutdown now dispatches synchronous sent messages while waiting for the worker, rejects update requests during teardown, and keeps the Karizmi worker loop bounded by its stop flag instead of re-enabling legacy reconnect. This removes the worker/UI wait cycle observed in that reproduction.

End exam also exposed premature transport shutdown after queueing Disconnect. Revocation now gets a bounded background flush before the viewer/channel is destroyed, and the receiving transport records an authenticated Disconnect even if EOF follows it before the runtime dequeues the message. Local revocation takes effect immediately; unreachable peers still require their own user to release a reservation.

## Validation record

| Check | Evidence / status |
| --- | --- |
| Core, Security, Network, UI tests | All four passed; log `build/release/Testing/Temporary/LastTest.log` |
| Approval storage | Protected round trip, reopened store, role isolation, invalid labels, removal, bounded ten-peer store |
| Core restoration | Restored reservation authenticates its saved peer, stays View Only, rejects takeover, revokes on Disconnect |
| Local paired viewer | Actual packaged engines: tile opening, full screen, retained viewer, close/reopen without trust dialog |
| Forced Teacher restart | Local test reconnects with saved credentials without code/confirmation |
| Forced Student restart | Local test reconnects with saved credentials without code/confirmation |
| Clean exits / crash-event checks | Passed: final application exits zero; no new Karizmi Application Error events; deliberate test kills tracked separately |
| Normal Exit/relaunch and End exam | Passed: Exit retains approval; relaunch resumes without code; End exam revokes it and produces a fresh Student code |
| Two live Students, Wi-Fi/Ethernet switching and IP changes | Not physically tested in this environment |
| Real shutdown/restart, Fast Startup, power loss, sign-out, sleep/lock | Not physically exercised; registry/session scoping is implemented, acceptance remains outstanding |
| Crash during every individual credential-rotation phase | Not fully failure-injection tested |

Run `tools/test-local-viewer.py` on an interactive desktop. It requests normal firewall UAC approvals, runs real same-PC applications, forces termination of its own test processes, checks recovery, and revokes its test approvals before final exits. It now fails on abnormal final exit codes and new Karizmi Application Error events. Prior scripts that reported visible success without checking exit codes missed shutdown crashes; those earlier lifecycle claims are superseded.

Build: `build/recovery-build.log`. Local acceptance: `build/restart-acceptance.log`. Same-PC screen capture can contain recursive views of the viewer itself and is not a substitute for a two-computer screen/control test.

## Reconnect, shortcut, and full-screen repair

Updated November 7, 2026. Both packages were rebuilt together. Protocol remains 3; update both applications because the local engine callback interface changed.

Reconnect now repairs a Connected transport while retaining approval and advances a Recovering attempt without overlapping it. It is unavailable before pairing, during confirmation, and after revocation. Recovery always restores View Only. Advanced settings includes a bounded, 32-entry recovery trace with stage codes for discovery, authentication, credential protection, firewall access, screen opening, and retry scheduling. It contains no codes or credentials.

One Teacher command router handles keyboard, session controls, and classroom commands. A focused viewer or its children identify the target independently of the selected tile; classroom/details focus uses that context. Ctrl+Shift+F12 is consumed locally, including buffered modifier events and releases, and repeats do not toggle again. Ordinary Ctrl/Shift combinations retain their event order.

Control survives focus transfers within the same viewer, its toolbar/title bar, and scoped full-screen transitions. Foreground focus is rechecked afterward. Moving to another application or student, closing the viewer, or losing transport revokes control and releases input. Control On still requires the Student's acknowledgement; stale generations cannot target replacement viewers.

| Current repair check | Result |
| --- | --- |
| Core, Security, Network, UI regressions | All four passed; UI includes 68 layout combinations, shortcut filtering, and two synthetic viewer focus targets |
| Real paired control acknowledgements | Passed with packaged Teacher and Student |
| Repeated full-screen entry/exit while controlling | Passed; Student remained Control On |
| Physical Ctrl+Shift+F12 press/release | Passed; toggled acknowledged control; Student's received-key counter did not increase |
| Unrelated foreground window | Passed; Student control revoked |
| Reconnect now on Connected transport | Passed; new viewer generation, no pairing code, View Only restored |
| Automatic engine/transport-loss recovery | Passed without manual retry or code; View Only restored |
| Close/reopen, forced application restarts, normal Exit/relaunch, End exam | Passed in the same local acceptance run |
| Final exits and Windows Application Error events | Zero final exit codes; no new Karizmi crash events |
| Two physical computers, adapter/IP switching, two live Student viewers | Outstanding; synthetic focus routing and loopback transport tests do not establish these behaviors |
| Disconnect/rotation failure at every transaction boundary | Not exhaustively injected; remains outstanding |

Build log: `build/control-reconnect-build.log`. Acceptance log: `build/control-reconnect-acceptance.log`. Regression log: `build/release/Testing/Temporary/LastTest.log`. The interactive acceptance script is `tools/test-local-viewer.py`. Same-sign-in approval, shutdown/sign-out invalidation, and explicit revocation policy above are unchanged. These results supersede earlier control/reconnect acceptance claims, not the outstanding physical-device checks.

## Follow-up: Student issues a fresh code after network loss

The user reported a fresh Student code after unplugging/reconnecting its network. Teacher traces showed repeated endpoint failures; a screenshot also showed ended tiles. These observations establish that Student lost its reservation, but no Student-side trace is available to identify its exact exception.

Source inspection found that recovery firewall setup requires eligible local IPv4 adapters. With no adapters, scope calculation returns ERROR_NETWORK_UNREACHABLE. Both initial recovery and approval restoration previously escalated a failed firewall operation into terminal session cleanup and credential removal. Student now keeps its reservation, waits, and retries recovery access every second. Successful refresh uses the current local addresses/subnets; the screen rule remains restricted to the authenticated peer. Firewall or managed-policy failure still prevents connection, but does not silently revoke approval. Explicit Disconnect, End exam, and revocation retain their existing behavior.

An active engine/send exception that precedes the transport pump's failure detection now stops that transport and enters authenticated recovery rather than escaping to pairing cleanup. Recovery trace adds R120 for this case, R031 for successful bootstrap TCP connection, R032 for credential authentication, and R101 with numeric Winsock errors for socket failures. No credentials or peer-provided exception strings are included in these trace entries.

| Follow-up validation | Result |
| --- | --- |
| Both x64 applications rebuilt | Passed; `build/network-outage-build.log` |
| Core, Security, Network, UI suites | All four passed; `build/network-outage-tests.log` |
| Failed recovery-access setup | Regression confirms waiting/retry, retained authenticated reservation, no fresh code, and explicit Disconnect still revokes |
| Engine exception before pump failure | Real encrypted test stream plus failing engine: enters Recovering with R120 and preserves reservation; explicit Disconnect ends it |
| Real Student network unplug/replug | Not rerun: the reported Student PC is unavailable |
| Current interactive paired acceptance | Not rerun for this follow-up; existing live applications were left running. Earlier acceptance above belongs to the preceding build |

Updated packages are in `out-network-repair/Teacher` and `out-network-repair/Student`, separate from the running `out/` copies. Update both complete folders, including any installed Student service copy. If the old build has already issued a fresh code and erased approval, pair once again; the repair cannot recover erased credentials and never authorizes by name/IP alone.

Follow-up setup-status correction: Reconnect now on a restored reservation previously overwrote the setup requirement with a misleading queued message even while the worker was waiting for firewall approval. It now shows R011 and explicitly requests Set up exam connection / UAC when local setup is missing. With setup complete it distinguishes a running attempt from a queued next attempt. A regression checks R011 before setup; all four suites passed. The previous repair package was running and locked, so this revision is packaged in `out-recovery-status/Teacher` and `out-recovery-status/Student`; see `build/recovery-status-build.log`. This message correction does not bypass per-run UAC or repair credentials already erased by an older Student.
