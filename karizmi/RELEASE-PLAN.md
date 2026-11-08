# Development Preview release plan

No tag, release, upload, or new semantic version is authorized by this preparation. Publication requires explicit maintainer authorization after review.

## Baseline and archive review

The latest technical specification identifies `out-recovery-status` (protocol 3) as the latest inspected baseline. Its combined ZIP and Student ZIP contain executable hashes matching the unpacked Teacher/Student baseline and the specification. `out-network-repair` is an older candidate. No deletion or relocation of historical packages was performed. The RAR was hashed but its contents were not extracted or validated; prefer newly built ZIPs.

The ZIPs include GPL/Botan notices, Student capture hooks and optional service components, but lack the newly collected full static-dependency notices and exact source-commit provenance. Their bundled documentation is historical. The Teacher `options.vnc` contains only an empty index setting. These archives are retained as historical artifacts and must not be silently promoted to an official release.

The [machine-readable inventory](ARCHIVE-INVENTORY.json) records actual ZIP entries and executable hashes. Existing executable hashes match the specification; that establishes artifact identity, not equivalence to a particular source commit.

## Candidate preparation

1. Start from a clean, identified source commit; build both applications into a fresh `-PackageFolder out-preview-candidate`. Do not mix older binaries or installed service copies. See [build instructions](../BUILD-KARIZMI.md).
2. Confirm Teacher executable, Student executable, capture DLL, optional service/install scripts, package README, technical specification, GPL/Botan notices, and actual linked dependency notices. Inspect PE dependencies on a clean Windows 10/11 x64 machine; static-library configuration is not proof of runtime completeness.
3. Resolve [licensing/source gaps](THIRD-PARTY-NOTICES.md). Supply exact corresponding source plus usable dependency sources/build information and commit identifier with the binaries.
4. Run all CTest suites and record latest-package physical pairing, viewing/control isolation, discovery, firewall setup/cleanup, unplug/replug recovery, service and sign-in lifecycle acceptance. Preserve unresolved failures in release notes. Independent security review remains outstanding.
5. Review all staged files for credentials, profiles, diagnostic traces/dumps and private information. Generate fresh ZIPs containing only the intended role directories, not whole working trees.
6. Use `Karizmi-EMS-Development-Preview-<source-id>-windows-x64-Teacher.zip`, the analogous `Student.zip`, and `Karizmi-EMS-Development-Preview-<source-id>-source.zip`. `<source-id>` is the actual commit identifier chosen by the maintainer, not an invented semantic version. Generate SHA256SUMS from final bytes with `Get-FileHash -Algorithm SHA256`.
7. Complete the [release-notes template](RELEASE-NOTES-TEMPLATE.md), obtain maintainer publication approval, then publish explicitly as a GitHub prerelease titled **Development Preview**. Protocol 3 is a wire compatibility identifier, not a release version.

## Existing archive checksums (not new release assets)

Calculated from actual local files during this preparation:

| Archive | SHA-256 |
| --- | --- |
| `out-recovery-status.zip` | `b7d0cee867e2def47274f20396d181c472a32d71fd975ce64fde1a4c3557c5ef` |
| `Student.rar` | `d2bc5d773edc24df5b1a78330018561031f08bb982d297cc289690a8d693d1ec` |
| `Student.zip` | `d0072fe660c6b0cacac6e01da28f920cbf7bb889cd2eb7b628bbd6c845b88e12` |

## Historical artifact policy

Generated `out*`, vendor trees, diagnostics, caches, and future staging directories are now ignored. Already tracked package artifacts remain tracked; ignore rules do not remove them from Git or its history. Relocating/removing those files requires a documented maintainer decision and explicit approval. Check the full Git history separately before a public release; this preparation does not certify history free of secrets.
