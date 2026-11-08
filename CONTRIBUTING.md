# Contributing

Start with the [README](README.md), [build guide](BUILD-KARIZMI.md), and [current specification](karizmi/TECHNICAL-SPECIFICATION.md). Keep Karizmi changes isolated and preserve upstream attribution. Do not rerun the one-time integration migration on already integrated source.

For a change, explain the observed problem, affected modules, toolchain, and tests actually run. Run the applicable CTest suites for code/build changes. Report physical-device acceptance separately from socket simulation and synthetic UI tests; never infer ten-device desktop performance from ten encrypted test peers.

Do not include pairing codes, recovery credentials, private profiles, registry approvals, dumps, or unredacted diagnostics in issues or commits. For suspected security defects, arrange private reporting with the maintainer before sharing exploit details publicly; this repository does not yet advertise a dedicated private reporting channel. Preview publication requires maintainer authorization and the [release gates](karizmi/RELEASE-PLAN.md).
