# Attribution and distribution review

Karizmi EMS extends UltraVNC; it does not replace upstream authorship. Original project: https://github.com/ultravnc/UltraVNC. The repository root LICENSE contains GNU GPL version 3. Preserve individual source copyright/license headers and all applicable terms. Some older upstream files contain their own license notices; the exact combined distribution terms need maintainer review rather than a blanket new SPDX assertion.

Botan 3.13.0 is BSD-2-Clause; its source archive URL and SHA-256 are pinned in dependencies.json. Build-Apps.ps1 copies the actual Botan license into each package as Botan-LICENSE.txt.

Static engine dependencies are restored through the root vcpkg manifest: zlib, zstd, libjpeg-turbo, liblzma, libsodium, minizip-ng, and transitive components such as bzip2. Their actual installed copyright files are copied under third-party-licenses in newly built packages. Review that directory against the final link inputs, including statically linked components; copying notices does not by itself prove completeness. Retained upstream modules, embedded resources, and noVNC/submodule contents also require review for any material actually distributed.

## Corresponding-source release gate

Ship the exact modified source, engine adapters, headers/resources, build scripts, dependency lockfiles, and applicable dependency source/build information corresponding to each binary. Retain upstream notices. Publish the exact commit and a usable source archive alongside the preview, with build instructions. Check redistribution obligations for every linked or embedded component, including static dependencies and any compiler runtime material.

The current checkout contains custom modules, modified engines, and build instructions, but historical archives have no demonstrated exact source-commit mapping. A clean source rebuild and artifact comparison have not established complete corresponding-source coverage for those archives. Do not represent this audit as legal certification. Maintainer review of source provenance, license compatibility, third-party source availability, and any additional GPL conveyance requirements remains a release gate. No upstream license or copyright header was replaced during preparation.
