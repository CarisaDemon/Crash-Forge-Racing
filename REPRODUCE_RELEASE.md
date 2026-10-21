# Crash Forge: Racing v2.1.3 — corresponding source and reproduction

This archive contains the recovered program source corresponding to the approved Windows x86 v2.1.3 RC3 release, the exact 14 non-executable release files, and a local reproduction recipe. Source files are at the archive root so the tree can be reviewed directly in Git. The game executable is built from source by the recipe.

## Verified correspondence

An isolated build of the 2,667 program/dependency source files (57,976,628 bytes) was compared against the approved original build. The machine code and program data are byte-for-byte identical: .text, .data, .rdata, .bss, .eh_frame, .idata, .reloc, .rsrc and .tls match, including section addresses, sizes and flags. Runtime header fields and non-debug data directories also match.

The only differing bytes in the remaining non-debug section belong to IMAGE_EXPORT_DIRECTORY.TimeDateStamp in .edata. The original export timestamp is 1792552616; the reconstructed timestamp is 1792541673, the recorded compilation SOURCE_DATE_EPOCH. All other export bytes match. The comparison masks only that identified four-byte field, after verifying the expected structure and both observed values.

Complete PE files differ in build/debug metadata: debug paths and sections, the COFF timestamp, PE checksum and image size occupied by debug sections. A byte-identical complete EXE is not claimed. The ZIP recipe verifies its exact 15 member names and contents, but original ZIP encoding/timestamps and the complete original ZIP checksum are not reproduced.

Reference SHA256 values:

| Object | SHA256 |
| --- | --- |
| Approved original build, before release metadata | 2082df57f1ed304e9351c092851a9948262665ea61dda8681218c749555e03ac |
| Isolated reconstruction, before release metadata | 1db406572f5ca46f05bab7bfc9e917dbf9101c198cec4b06d96820f4138cb94f |
| Approved RC3 executable | b912d79ce22b6034d1ed68aef4483566f39a32267e9ea7a5a0b7b52c0465c45a |
| Approved RC3 Windows ZIP | 1fe4f5fa5e782d63da99ced56ec3507e759276fcca8797c10a0cd9f897c14e76 |

The machine-readable evidence is in REPRODUCTION_EVIDENCE.json. release_source_manifest.json records the exact source/resource hashes, toolchain identity and PE reference sections used by the verification scripts. SHA256SUMS.txt covers every archived file other than itself. Preserve file bytes and line endings when importing this tree into a repository; a Git checkout that converts line endings can fail the exact-byte source manifest check.

## Build environment

The verified build used Windows, Python 3.12, and the existing MSYS2 MinGW32 toolchain. Python is a development/reproduction dependency. It is not a runtime requirement of the packaged game.

| Component | Verified installed version |
| --- | --- |
| GCC, i686-w64-mingw32 | 16.2.0, MSYS2 Rev4 |
| GNU Binutils | 2.47-3 |
| CMake | 4.4.3-3 |
| GNU Make | 4.4.1-5 |
| MinGW CRT and headers | 14.0.0.r420.g61d40c4c0-1 |
| libvorbis | 1.3.7-3 |
| libogg | 1.3.6-1 |
| SDL source | 3.4.10, included under externals/SDL |

The compiler, Windows SDK/CRT and installed static Vorbis/Ogg libraries are development prerequisites; their installed files are identified by package version and key file hashes in the manifest. SDL, cgltf and the project-owned platform/header sources used by the build are included. Existing copyright and license notices are retained in LICENSE, THIRD_PARTY_NOTICES.md and the component directories.

This recipe does not install a toolchain or alter system PATH. It uses --toolchain for the existing MinGW32 bin directory. The verified compiler binaries and linked static libraries must be available there. A newer or different compiler can generate a different program; the strict verification stops on that difference instead of relabeling it as the verified release.

## Reproduce

Extract this source archive preserving its paths and bytes. In PowerShell, from its root:

~~~powershell
python tools/release213/reproduce_release.py --toolchain C:\msys64\mingw32\bin --output E:\CFR_reproduce_v213 --preflight-only
python tools/release213/reproduce_release.py --toolchain C:\msys64\mingw32\bin --output E:\CFR_reproduce_v213
~~~

Choose a new output directory. Existing directories are refused. Preflight checks the source hashes, toolchain file hashes, and compiler date/time macro behavior without compiling. The full command copies the source to that isolated directory, configures, builds, verifies, and packages. It runs no game GUI and publishes nothing.

The recipe uses Release, MinGW Makefiles, the ctr_native target, LTO enabled, CPU-native tuning disabled, PGO disabled, CSEQ liberation enabled, x64 disabled and all experimental QA switches disabled. The exact settings and generated flags are recorded in the manifest. CMake compilation is limited to two jobs. The linker launcher changes only the LTO worker limit from -flto=6 to -flto=2; the isolated verified build used this same limit.

Two environment timestamps reproduce metadata from the approved original build:

- CMake configuration: SOURCE_DATE_EPOCH=1792552161 generates unknown-dirty-20261021030921.
- C compilation: SOURCE_DATE_EPOCH=1792541673 makes __DATE__/__TIME__ generate Oct 21 2026 00:14:33. This value reproduces the recorded literal; it is not presented as the original build's UTC wall-clock time.

The copied build tree has no Git metadata, and Git parent-directory discovery is bounded to the isolated output directory so that CMake retains the original build ID. No prefix map is needed for runtime data: moving the source changed debug paths but left the verified machine code and data identical.

Outputs are under the chosen directory:

- source/: the copied input tree.
- build/ctr_native.exe: the unmodified reconstructed original build.
- configure.log, build.log and BUILD_RESULT.json: local build evidence.
- release/CrashForgeRacing.exe: the generated release executable.
- release/Crash-Forge-Racing-Windows.zip and its .sha256 file.
- release/REPRODUCTION_RESULT.json: checks performed on the generated package.

## Release metadata and exact package members

The approved release was prepared without recompiling the original program. The reproduction tool follows its transformations:

1. Verify the rebuilt PE32/i386 against the original code/data references.
2. Run the recorded strip.exe with --strip-debug, writing a new file.
3. In the verified .rdata section, replace exactly six five-byte ASCII version literals from 2.1.2 to 2.1.3.
4. Replace exactly one fixed-width menu timestamp from Oct 21 2026 00:14:33 ART UTC-3 to Oct 21 2026 15:52:49 ART UTC-3.
5. Verify the resulting code/data against approved RC3 references, then package the 14 resource templates with the generated executable.

CMakeLists.txt intentionally retains the original 2.1.2 build version; the documented release metadata transformation produces the approved 2.1.3 labels. It does not alter gameplay code or allow arbitrary byte differences.

If a matching reconstructed executable already exists, packaging alone is available:

~~~powershell
python tools/release213/package_release.py --input E:\CFR_reproduce_v213\build\ctr_native.exe --output E:\CFR_package_v213 --strip C:\msys64\mingw32\bin\strip.exe
~~~

The exact ZIP member list is:

~~~text
CrashForgeRacing.exe
LICENSE.txt
README_FIRST.txt
THIRD_PARTY_NOTICES.txt
assets/UI_NATIVE/blankbuttons.png
assets/UI_NATIVE/cnk3d_body_v2.png
assets/UI_NATIVE/cnk3d_needle_v2.png
assets/UI_NATIVE/cnk_turbo_gauge.png
assets/UI_NATIVE/control_glyphs.png
assets/UI_NATIVE/finalbuttons.png
assets/localization/strings_de.tsv
assets/localization/strings_es.tsv
assets/localization/strings_fr.tsv
assets/localization/strings_it.tsv
assets/localization/strings_nl.tsv
~~~

The templates under release_resources come byte-for-byte from the approved RC3 ZIP. Its PNGs and localization tables are included, along with its license, notices and user README. Original disc data, installed mods, personal settings, saves and runtime logs are outside the source and release archive. A disc image is not needed to compile.

## Existing repository workflow

.github/workflows/windows-game-release.yml is preserved byte-for-byte from the public repository commit b3fa43bd8d82cfaa985f5419db9867a405583a3c, Git blob 13eab665e96d5b29ac653385b11995b66d952c6d. It still contains its original v2.1.1 version check and packaging/publication conditions. The local v2.1.3 reproduction tools do not edit or weaken those checks.

This source archive is the verified release input, not a replacement for development work that happened after the approved build. Public release resources and reproduction documentation are recorded separately from the recovered compilation source in the manifest.
