# winpthread (prebuilt, Windows only)

`3rdparty/winpthread` holds a prebuilt copy of the mingw-w64 POSIX threads library
(winpthreads). The clang-cl Windows build links `kyty_emulator` and the full-emulator test
binaries against it, and the DLL is copied next to those binaries and installed with every
Windows release.

## Files

| File | SHA-256 | Size |
| --- | --- | --- |
| `bin/libwinpthread-1.dll` | `78ebbc1480d62098aa66307db2ff139abc07c137bc188f5b73bf7b131492932c` | 90,084 bytes |
| `lib/libwinpthread.dll.a` (import library) | `c0302c72dbd07e551c29e968d385cf315bf42ec8334a60b3acf39346e8f972ef` | |
| `include/*.h` | headers from the same winpthreads release | |
| `COPYING` | mingw-w64 winpthreads license (MIT-style) | |

`3rdparty/CMakeLists.txt` checks both binaries against these hashes when configuring a
clang-cl build and stops with an error if either differs. Update the hashes there and in this
file only when replacing the binaries with a reviewed build.

## What is known about the DLL

The information below was read from the binary itself; no build record or download URL was
committed with it.

- Version resource: FileVersion/ProductVersion `1.0.0.0`, FileDescription
  "POSIX WinThreads for Windows", CompanyName "MingW-W64 Project. All rights reserved.",
  InternalName/OriginalFilename `WinPthreadGC`, LegalCopyright
  "Copyright (C) MingW-W64 Project Members 2010-2011". This is the generic version resource of
  every winpthreads build, so it does not identify the release.
- Compiler: embedded `.comment` strings read
  `GCC: (MinGW-W64 x86_64-posix-seh, built by Brecht Sanders) 10.2.0`, i.e. a WinLibs
  (winlibs.com) GCC 10.2.0 x86_64 posix-seh toolchain. The DLL most likely comes from the
  `bin` directory of one of the WinLibs GCC 10.2.0 / mingw-w64 releases. The exact WinLibs
  release (revision, mingw-w64 runtime version) has not been confirmed.
- PE header: x86-64, linker version 2.35 (binutils 2.35), PE timestamp 0 (reproducible build),
  imports only `KERNEL32.dll` and `msvcrt.dll`.
- Not Authenticode-signed.
- Repository history: first present in commit `67d7db685a3d2c64431c43ac360d205c191388f3`
  ("agc: new abi"), which is the earliest commit of the current history.

## Replacing it

To make the binary reproducible, rebuild winpthreads from a pinned mingw-w64 release
(`mingw-w64-libraries/winpthreads`) or take it from a pinned, hash-verified toolchain
archive, then record here the source URL, release tag, archive SHA-256 and the new file
hashes, and update the check in `3rdparty/CMakeLists.txt`.
