# Karizmi EMS ? Development Preview

Draft template ? replace every placeholder with verified evidence before publication.

- Source commit: <exact commit>
- Source archive and build instructions: <links>
- Package names and calculated SHA-256: <actual final assets>
- Toolchain/dependency baseline: <actual versions>
- Wire compatibility: protocol 3; update Teacher, Student, and any installed service copy together.

## Included behavior

Native Teacher dashboard and Student screen sharing over offline IPv4 LAN; authenticated pairing, TLS 1.3 transport, optional permission-enforced keyboard/mouse control, and recovery of approved sessions during the current Windows sign-in. Up to ten reserved Students are supported by the architecture; ten live physical desktops are not established by local peer simulations.

## Requirements and installation

Windows 10/11 x64, peer-accessible Ethernet/Wi-Fi, and permission for application-scoped temporary Windows Firewall setup through UAC. Extract the entire role package and retain Student's vnchooks.dll. Launch both roles, accept Student's notice, set up exam connection, select Student in Teacher, enter its code, and compare/confirm the phrase on both PCs. Start View Only and request control separately. Portable use does not install the optional service. Closing the window hides to tray; Exit quits; Disconnect revokes pairing.

## Evidence for these exact assets

<Commands, actual results, package hashes, physical devices/topology, and failures. Separate current binary tests from historical evidence.>

An earlier version was successfully tested by the developer with three physical laptops on a LAN. This developer-reported milestone does not constitute comprehensive validation of the latest build.

## Known limitations

Development Preview; no production certification or independent security review. Latest reported physical discovery/network-outage recovery repair remains unconfirmed unless new evidence above closes it. Broader multi-device acceptance, cross-target input isolation, adapter changes, rotation failures, and lifecycle/firewall tests remain outstanding. Ports are fixed; IPv6, managed-network bypass, and pre-login/RDP capture are outside current scope. Include any additional failures observed for these assets.

## Source and licenses

Derived from UltraVNC (https://github.com/ultravnc/UltraVNC). Include original GPL copyright/license notices, Botan BSD-2-Clause notice, all linked dependency notices, exact corresponding source and dependency/build information. <Record completed licensing/provenance review; do not claim compliance from the root license alone.>
