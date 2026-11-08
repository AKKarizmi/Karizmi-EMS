# Karizmi Teacher and Student — technical specification

## Current validation context

An earlier version of Karizmi EMS was successfully tested by the developer using three physical laptops connected over a local area network. This is a developer-reported functional testing milestone and does not constitute comprehensive validation of the latest build.

Latest physical discovery/network-recovery repairs remain unconfirmed; multi-device acceptance and independent security review are outstanding. Historical results below retain their original scope. Current behavior is consolidated in [TECHNICAL-SPECIFICATION.md](TECHNICAL-SPECIFICATION.md). A subsequent preparation rebuild of both applications and all four automated suites passed using existing dependencies; see [preparation audit](PORTFOLIO-AUDIT.md). The rebuilt candidate has no new physical-device acceptance and does not replace the historical baseline by implication.


Document date: 2026-11-08, Asia/Kabul. Baseline: latest built `out-recovery-status` packages and current source, protocol **3**. Status: **development preview**. This document describes implemented behavior, intended guarantees, and known gaps separately. It is not a security certification or a statement that physical classroom acceptance has passed.

## 1. Product scope

Karizmi provides offline classroom screen viewing and optional remote keyboard/mouse control over an IPv4 Ethernet or Wi-Fi LAN. Teacher manages up to ten independently reserved Students. Each Student permits one Teacher. Only one Student may be controlled by a Teacher at a time.

There are no required internet services, cloud accounts, or pairing servers. Computer names and addresses are discovery hints, not proof of identity. New pairing requires Student's monitoring notice and mutual phrase confirmation. Routine Student operation uses a tray icon, without a floating monitoring indicator, routine recovery balloons, or sounds.

Not implemented: organization activation/licensing, organization-only execution, permanent enrollment, emergency remote deactivation, or self-destruction. Copying an executable is not prevented by the pairing system. These are separate product requirements.

## 2. Platform and delivered components

| Component | Responsibility |
| --- | --- |
| `KarizmiTeacher.exe` | Discovery, classroom grid, independent session runtimes, detailed viewers, control routing |
| `KarizmiStudent.exe` | Pairing server, capture engine, input permission enforcement, tray UI, recovery |
| `vnchooks.dll` | Student capture dependency; distribute with Student |
| `KarizmiStudentService.exe` | Optional automatic LocalSystem supervisor for console-user Student startup |
| Elevated helper mode | Predefined firewall operations using the application executable; no separate general command service |
| Installation/removal scripts | Optional Student service installation and removal |

Target OS: Windows 10/11 x64. Native UI uses Win32, Segoe UI, Karizmi purple branding, generated Windows icons, and per-monitor DPI handling. The applications are separate portable packages; running Student alone does not install its service.

The latest package folders are `karizmi/out-recovery-status/Teacher` and `karizmi/out-recovery-status/Student`. Older `out/` and `out-network-repair/` folders may still exist and are not the latest baseline. Update both complete packages, including a separately installed Student service copy.

## 3. Internal architecture

The existing UltraVNC viewer and capture/server engines are retained. Shared Karizmi components implement security, framing, discovery, session state, approval storage, firewall setup, and UI coordination.

Each Teacher tile owns a `Runtime`, a synchronized `Session`, an encrypted transport, a viewer adapter, and its retained thumbnail. A shared `ClassroomResources` coordinator owns discovery, Teacher firewall setup, and a four-attempt concurrency budget. Ending one Student must not remove resources required by another.

Engine integration uses the C-compatible callbacks in `include/karizmi/integration.h`. Attach, detach, control, display, thumbnail, and window-to-peer routing use process-local authenticated stream markers. Commands carry generations so old callbacks cannot target replacement connections. Viewer mutations run on the UI thread; network work runs on workers. The viewer connection list is synchronized, with list locks released before destruction or worker joins.

RFB bytes are carried directly inside the authenticated encrypted transport. There is no plaintext localhost bridge. Legacy network/authentication and independent reconnect paths must not provide a bypass. The legacy untrusted-server prompt is skipped only for an exact stream registered as authenticated by Karizmi.

Important source modules:

| Path under `karizmi/` | Responsibility |
| --- | --- |
| `src/core.cpp`, `include/karizmi/core.h` | State machine, timers, rate limits, framing and bounds |
| `src/security.cpp` | SRP, HKDF, TLS policy, confirmation phrase |
| `src/pairing.cpp` | Bootstrap exchange, authenticated labels, confirmation |
| `src/network.cpp` | Sockets, discovery, encrypted queues, heartbeats |
| `src/runtime.cpp` | Pairing lifecycle, recovery, control acknowledgement, approval persistence |
| `src/approvals.cpp` | Windows-sign-in-scoped DPAPI approval store |
| `src/firewall.cpp` | Elevated helper, IPC, executable-scoped firewall rules |
| `src/classroom.cpp`, `src/classroom_ui.inc` | Discovery catalog, concurrency coordinator, grid and per-student commands |
| `src/app.cpp`, `src/teacher_keys.inc` | Shared native UI, stream registry, input routing and shortcut filtering |
| `src/teacher_engine.cpp`, `src/student_engine.cpp` | Native-engine adapters |
| `src/student_service.cpp` | Optional console-user startup supervisor |

## 4. Network topology and discovery

| Port | Protocol | Purpose |
| --- | --- | --- |
| 45900 | UDP | Application-specific discovery queries and replies |
| 45901 | TCP | Initial pairing and authenticated recovery bootstrap on Student |
| 45902 | TCP | Encrypted screen/control transport on Student |

Ports are fixed in the current UI; advanced port changes are not implemented. Teacher initiates TCP connections. Discovery sends interface-specific IPv4 broadcast queries and receives unicast replies. Eligible adapters are active Ethernet/Wi-Fi interfaces; arbitrary port scanning is not performed.

Teacher scans on startup, every five seconds, and on Scan now/manual retry requests. Replies carry protocol framing, query correlation, random 128-bit instance ID, endpoint, and a validated UTF-8 computer name of at most 128 bytes. Codes, code hashes, and reconnect secrets are not advertised. Catalog entries deduplicate by instance ID, update addresses, expire after 15 seconds, and are capped at 256. Advertised labels remain Unverified until pairing authenticates the device label.

Discovery is refreshed by polling current adapter information, not Windows adapter-change notifications. Discovery replies grant no authorization. A saved tile can exist while the discovery list is empty. Network segmentation, AP client isolation, unavailable adapters, incompatible builds, or firewall policy can prevent discovery and connection. IPv6 discovery/transport is not supported.

## 5. Pairing and cryptographic transport

1. Student accepts the monitoring notice before making a code available. It generates eight decimal digits using Botan's system RNG and rejection sampling. Leading zeros are preserved. An unused code expires after five monotonic minutes.
2. Teacher selects a discovered Student and supplies its code. The shared pairing implementation also supports bounded candidate probing and aborts if multiple candidates authenticate; the classroom UI normally targets a selected device.
3. SRP-6a uses fixed `modp/srp/3072` and SHA-256. Salt is 32 bytes; public values are 384 bytes and validated against the fixed group. Peer-selected alternatives are not accepted.
4. HKDF(SHA-256) derives a 32-byte external TLS PSK from the SRP secret. Context includes the protocol version, role context, both instance IDs, salt, and SRP public values. The numeric code is not used directly as an encryption key.
5. TLS 1.3 uses external PSK authentication, X25519 key exchange, and AES-128/GCM. TLS 1.2/DTLS are disabled. The implementation uses a no-op resumption manager, issues no persistent tickets, and does not use early application data or plaintext fallback. TLS Finished supplies mutual key confirmation for this application-specific composition.
6. Device labels/instance IDs are exchanged inside TLS. Both users compare and accept a six-word phrase derived from the authenticated TLS exporter. Device labels are not verified personal identities. The phrase uses six selections from a 16-word list; it is a human comparison aid, not a substitute for the cryptographic key.
7. Confirmation expires after at most 30 seconds, also bounded by the pairing deadline. After confirmation, Student supplies a random 256-bit recovery credential. A TLS exporter-derived ticket authenticates the separate encrypted screen stream. Successful connection invalidates the code.

Screen, input, authenticated identity, control, heartbeat, rotation, and disconnect messages use authenticated encryption. Discovery and the initial PAKE/recovery identifiers are bootstrap metadata, not secret application traffic. Reconnect bootstrap sends instance IDs and a derived credential identifier, never the credential itself. Independent review of the SRP/TLS composition remains required.

Rate limits: five failures per source per minute and twenty globally per minute, with a 60-second cooldown; code regeneration does not clear counters. The limiter supports a four-active-attempt ceiling, but Student currently processes bootstrap handshakes serially. Counters are in memory and reset when the process restarts.

## 6. Wire framing and resource bounds

Protocol version is 3. Multi-byte integers use explicit network byte ordering, not packed native structures. Unsupported versions and malformed lengths are rejected.

| Header field | Bytes |
| --- | --- |
| Magic `0x4B41525A` (`KARZ`) | 4 |
| Protocol version | 2 |
| Message type | 2 |
| Payload length | 4 |
| Sequence number | 8 |

Header size is 20 bytes. Maximum general payload is 65,536 bytes; bootstrap packet payload is bounded to 4,096 bytes. Encrypted receive sequences must match exactly; reordered/replayed application messages are rejected. Text uses bounded, length-prefixed fields and rejects embedded NUL; device names additionally require valid UTF-8 without control characters.

Message families include discovery query/reply; pairing hello/challenge/response; authenticated identity/confirm/reject; stream setup; heartbeat/disconnect; control request/acknowledgement/release; reconnect hello; credential rotate/ack/commit; and framed RFB. Enum declarations also retain setup message identifiers that should not be interpreted as separate public entry points.

Transport bounds include a 16 MiB RFB receive queue, 16 MiB queued outgoing payload, 1,024 outgoing messages, and 64 incoming non-RFB control messages. Exceeding bounds fails the transport. These are safety bounds, not guarantees of memory usage or frame rate under every engine condition.

## 7. Ownership and lifecycle

Normal state transitions are `Ready -> Pairing -> Confirming -> Connected -> Recovering`, with return to Connected after authenticated recovery and terminal Ended after revocation. Restored approvals start in Recovering. One Student reservation prevents takeover by another Teacher.

| Event | Behavior |
| --- | --- |
| Close detailed viewer | Hide viewer, release control, return to grid; monitoring/thumbnail updates continue |
| Close Teacher main window | Hide to tray |
| Close Student window | Keep Student in tray |
| Transport or engine failure | Suspend input, retain last frame as STALE, retain reservation, attempt recovery |
| Disconnect | Revoke the selected pairing; fresh pairing required |
| End exam | Revoke all Teacher pairings |
| Explicit application Exit | Stop monitoring, clean temporary firewall access, preserve sign-in approval |
| Crash/application relaunch in same Windows sign-in | Restore protected approval and attempt authenticated recovery after setup |
| Sleep/lock/network change | Intended to preserve approval; recovery resumes in View Only |
| Sign-out/restart/shutdown/power loss | New authentication-session scope cannot reuse old approval; fresh pairing required |

If a peer is unreachable during explicit revocation, it cannot receive that command. Its own user must release its retained reservation. Showing a saved tile does not establish live monitoring or current reachability.

## 8. Approval storage and rotation

Approval records are bounded and versioned, user-DPAPI encrypted, and stored in ACL-restricted volatile registry keys beneath `HKCU\Software\Karizmi\SessionApprovals-<AuthenticationId>\Teacher` or `\Student`. The authentication-session identifier comes from Windows token statistics. The serialized record also checks this scope, protocol, role, and schema version. Teacher stores at most ten peers; Student stores one.

Records contain local/peer instance IDs, bounded label/address/ports, current/pending/previous recovery credentials, previous-credential expiry, and input capability. Codes are not stored. Corrupt, incompatible, foreign-scope, or missing records do not authorize access. The volatile registry is not permanent enrollment; DPAPI is user-scoped, and current-user/admin compromise is outside its protection boundary.

Recovery authenticates with current or pending credentials as needed. Rotation persists pending state before acknowledgement and committed state before final acknowledgement. A bounded previous-key window supports a lost acknowledgement. Generation and exclusive session state prevent a second simultaneous owner. Every rotation transaction boundary has not yet been exhaustively failure-injection tested.

## 9. Recovery behavior and current repair

Authenticated heartbeats run every two seconds; six seconds without authenticated received traffic or an immediate transport error triggers loss handling. Recovery retries immediately, then after 1, 2, 4, 8, and at most 10 seconds. It has no fixed 60-second expiry. Teacher permits at most four concurrent pairing/recovery attempts, with no overlap for an individual Student.

Teacher tries the previous endpoint plus currently discovered endpoints for the reserved instance. Names and IPs alone never transfer approval. A changed peer address must authenticate before Student updates the exact-address screen rule. Each successful recovery creates a replacement viewer generation and returns to View Only.

Reconnect now repairs a Connected-but-stuck transport while retaining approval, or advances a Recovering attempt. It does not authorize a revoked/unpaired device. A restored approval waits for per-run firewall setup. Missing local setup now reports R011 rather than misleadingly claiming an attempt is running.

The latest Student repair treats recovery-access failure, including no eligible network adapter, as retryable: it retains approval and refreshes scoped firewall access every second while recovering. It does not widen the screen rule. Active engine/send exceptions preceding transport failure detection also enter recovery rather than terminal pairing cleanup.

Reported field failures remain relevant: a Student previously generated a fresh code after a network outage, while Teacher kept retrying the old approval; discovery subsequently failed in user testing. Source-level causes have been repaired and regressions pass, but the latest physical two-computer unplug/replug and discovery scenario has not been verified as resolved. If an older build erased credentials, update both PCs and pair once again; erased credentials cannot be reconstructed.

## 10. Remote control and viewer behavior

Every initial connection and recovery starts in View Only. Teacher requests control; Student enforces capability/permission and sends an acknowledgement. Control On is shown only after acknowledgement. Physical Student keyboard/mouse input is not intentionally blocked.

One Teacher command router serves Ctrl+Shift+F12, the session toggle, and classroom commands. A focused viewer/window hierarchy identifies the target independently of the selected tile. Grid/session-details focus uses that context. Shortcut modifier presses are buffered; the shortcut and releases are consumed locally, auto-repeat is ignored, and ordinary Ctrl/Shift combinations retain their order.

Control transfer revokes the previous target and releases input before enabling another. Disabling control, external focus, switching students, viewer close, and transport failure release tracked injected input and suspend forwarding. Generation checks reject obsolete viewer commands. Student independently handles release on loss.

Full-screen entry/exit and focus transfers within the same viewer, its child windows, toolbar, and title bar preserve acknowledged control. A scoped transition guard ends after the transition and the foreground window is rechecked. An unrelated application revokes control. Retained native viewer full-screen shortcut is Ctrl+Alt+F12; Teacher control shortcut is Ctrl+Shift+F12.

Background framebuffer requests are capped at one per second; the detailed view allows up to ten per second. Requests coalesce instead of growing without bound. Tiles retain bounded 320 x 180 thumbnails and frame age; stale images must not be presented as live observation. These are request limits, not promised delivered frame rates.

## 11. Firewall and privilege model

Set up exam connection requests UAC for a helper retained for the application run. The helper uses authenticated, access-controlled IPC tied to the initiating process/session and predefined firewall operations. Executable identity is derived locally; requests are not arbitrary shell commands/program paths. Requests are serialized. Rules apply to Private, Public, and Domain profiles, with edge traversal disabled.

| Phase | Student-owned access |
| --- | --- |
| Ready/pairing | Discovery UDP and bootstrap TCP scoped to eligible local addresses/subnets |
| Connected | Screen TCP restricted to the authenticated Teacher's exact address; initial bootstrap access removed |
| Recovering | Local-subnet discovery/bootstrap restored; application accepts only the reserved peer's credential |
| Cleanup | Remove owned temporary rules; preserve unrelated policy |

Teacher setup permits discovery replies; it does not add a general inbound screen-sharing rule. Rule configuration is read back before success is reported. Rule configured is distinct from Connection tested, which requires an authenticated connection. Existing broader allows can weaken effective restrictions; explicit blocks and managed policy can still prevent access.

The helper watches its parent and attempts cleanup on exit/crash. Rule replacement attempts rollback on failure; rollback can also fail. Remove access is independent of listener activity but revokes the session. Defender antivirus settings, network profile classifications, unrelated rules, and managed policy are not disabled or bypassed. A protected ownership manifest and relocation/stale-rule cleanup are not implemented.

## 12. Student service and disclosure

The optional `KarizmiStudent` service runs as LocalSystem from a protected Program Files installation and launches Student under the signed-in console user's account. It does not capture pre-login, adopt manually started instances, configure firewall rules, bypass notice/pairing, or provide privileged remote commands. RDP and multi-user capture are outside scope.

Service stop signals its child to exit, with a five-second fallback job termination. Exit/notice cancellation suppresses automatic relaunch for that logon until service restart. Console-session changes stop its child. Installation refuses an existing service/directory; removal targets known owned files rather than recursively deleting arbitrary directories.

Student retains tray status, Open, Disconnect, and Exit. Explorer restart notification triggers icon registration; failed registration opens the window so status/disconnect remain accessible. Monitoring notice and new-pair confirmation remain visible even though ordinary operation is quiet.

## 13. Build, dependencies, and package identity

Current validated compiler: Visual Studio 2026/v145 x64. Build scripts support selecting v143 on a machine where VS2022/v143 is installed; that toolset was not validated here. Shared Karizmi components use C++20; the C-compatible engine boundary allows legacy engine language settings. Tooling includes Windows SDK, CMake, Ninja, Python, and vcpkg.

Botan is pinned to 3.13.0. Archive SHA-256: `12f5a8358890bbee82edfe9d2e7769b0a610b6dd0e0698aea13d20a675d84620`. The root vcpkg baseline is `2f8d9f7d3f041e70643c18954cee799459e7eb75`, with zlib, zstd, libjpeg-turbo, liblzma, libsodium, and minizip-ng dependencies. See `dependencies.json`, the root `vcpkg.json`, and `Karizmi.Engine.props`.

```powershell
# Full build from repository root; dependencies/toolchains must be available.
powershell -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1

# Rebuild with existing dependencies into a separate package folder.
powershell -ExecutionPolicy Bypass -File karizmi/tools/Build-Apps.ps1 -SkipCore -SkipDependencies -PackageFolder out-recovery-status

ctest --test-dir karizmi/build/release --output-on-failure
```

Buildability has been demonstrated; byte-for-byte reproducible output across machines has not been established. There is no independently assigned Karizmi semantic release identifier in this specification; protocol 3 is a compatibility version, not a product release number. Package hashes identify the inspected binaries:

| Latest inspected executable | SHA-256 |
| --- | --- |
| `out-recovery-status/Teacher/KarizmiTeacher.exe` | `F4E5DA32999E5CE8FBE7D730B0CB3F92B66B5FE7C04702C981C22CCFDF197357` |
| `out-recovery-status/Student/KarizmiStudent.exe` | `0F9815111AE95C539EC6749E63878A57C105D7BB6F406C088564D258C29C8800` |

Upstream UltraVNC GPL attribution/license and Botan BSD-2-Clause notices remain in the packages. Branding changes do not remove upstream license obligations.

## 14. Diagnostics and validation status

Advanced displays a bounded 32-entry monotonic recovery trace. Important stages: R010 queued; R011 setup required; R020 endpoints/discovery; R030 bootstrap connect; R031 TCP connected; R032 credential authenticated; R040 pending rotation; R050 access update; R060 screen stream; R070 transaction; R080 protected commit; R090 authenticated recovery; R100/R103 exchange failures; R101 numeric Winsock error; R105 firewall access unavailable; R110 next retry; R120 active transport failure.

Trace text excludes pairing codes, credentials, and peer-provided exception strings. Local UI diagnostics expose session generation, acknowledged control status, and a received-key-event count without key contents. Termination diagnostics are under `%LOCALAPPDATA%\Karizmi\Diagnostics`. Optional `KARIZMI_CAPTURE_DUMP=1` enables a dump; dumps may contain secrets and must remain protected diagnostic artifacts, not distribution contents.

| Evidence | Status / scope |
| --- | --- |
| Latest Teacher/Student builds | Passed; `build/recovery-status-build.log` |
| Core, Security, Network, UI suites | All four passed; `build/network-outage-tests.log` |
| DPI layouts | 68 role/state/size cases including 100%, 125%, 150%, 200%; automated layout evidence, not every physical monitor |
| Ten encrypted concurrent peers | Local transport simulation passed; not ten full desktop capture sessions |
| Focused viewer A vs selected B | Synthetic two-viewer routing test passed |
| Latest outage regressions | Failed recovery-access setup retains reservation; engine exception on a real encrypted test stream enters recovery; missing setup reports R011 |
| Earlier real same-PC paired acceptance | Passed control acknowledgements, repeated full-screen transitions, physical shortcut consumption, external-focus revocation, connected repair, engine-loss recovery, viewer close/reopen, same-sign-in restarts, End exam, clean exits, and crash-event checks |
| Latest follow-up paired GUI acceptance | Not rerun; earlier acceptance is not evidence for every latest binary change |
| User physical-network reports | Discovery/recovery failures reported; latest repair not physically confirmed resolved |
| Physical two-computer adapter/IP switching and two live Students | Outstanding |
| Every rotation failure boundary, actual Explorer restart, boot/Fast Startup/sign-out/power-loss policy | Not comprehensively acceptance-tested |
| Independent cryptographic/security review | Not performed |

Earlier acceptance log: `build/control-reconnect-acceptance.log`. Further evidence and historical crash findings: `RECOVERY-VALIDATION.md`, `UI-VALIDATION.md`, and `SERVICE.md`. Older `CLASSROOM.md` protocol-2 heading, selected-tile shortcut wording, and purely memory-only approval descriptions are superseded by current source and this baseline.

## 15. Release gates and remaining work

Before classroom release: reproduce and close the reported physical discovery/network-recovery failures; verify two independent live Students and cross-target input isolation; run physical adapter/IP-switch and packet-capture acceptance; exhaustively inject rotation/teardown failures; verify shutdown, sign-out, Fast Startup, lock/sleep and service lifecycle; and obtain independent security review.

Other planned gaps include configurable ports with transactional listener/rule replacement, protected stale-rule ownership/relocation cleanup, adapter-change notifications, concurrent Student bootstrap processing, and organization licensing if required. Current code and passing local tests must not be represented as completion of those requirements.
