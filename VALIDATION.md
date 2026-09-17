# WaffleHouse-Client 5.4 alpha1 — Validation

Date: 2026-09-15

## Scope

WaffleHouse-Client 5.3 promotes the 5.2r3 Update Merger tree and adds the redesigned macOS bootstrap/deployment path. Validation in this packaging environment covers release identity, existing protocol/UI regressions, macOS framework-linkage source rules, and the new macOS bootstrap/package regression gates.

## Regression results

All **29/29** regression files under `tests/` pass from the 5.4 alpha1 source tree, including:

- BuildFeatures GUI declaration scope
- IM composer initial focus
- macOS PJSIP framework-link normalization
- macOS 5.3 Xcode/Homebrew bootstrap and Qt deployment gates
- account-centric Add/Remove/Edit/Connect main-window workflow
- contextual menu migration / Command Palette removal
- OSCAR debug, idle, ChatNav, presence hydration, file-transfer and NINA compatibility gates
- SIP `chan_sip` compatibility and server-type UI
- Linux/FreeBSD/macOS interactive SSH PTY behavior
- 5.1r4 persistent media regression surface
- 5.2 modular protocol feature gates
- 5.2r3 warning-cleanup regression surface
- 5.3 application/CMake version propagation and WaffleHouse branding checks
- 5.3 Media Center Stop -> Play direct queue-rebuild and playlist-preservation regression
- 5.3 Media Center Pause -> Play state-aware resume regression
- 5.3 single-instance GUI/CLI shared-lock regression
- 5.4 alpha1 WaffleCast/album-art/transient-library regression

Additional validation:

- `build.sh` and POSIX-shell scripts pass shell syntax checks.
- Bash-specific scripts pass `bash -n`.
- Python regression/helper files pass source execution/validation.
- `scripts/build-macos.sh` passes `sh -n` and the dedicated 5.3 deployment regression.
- No `__pycache__`, `.pyc`, CMake build cache, object files, or editor backup artifacts are intended to ship in the final ZIP.

## macOS 5.3 deployment regression requirements

The release test confirms the macOS builder contains all of the following gates:

- full Xcode detection/configuration and Ventura archived-Xcode flow;
- official Homebrew bootstrap;
- narrowed `qtbase` + `qtmultimedia` + `qttools` dependency set;
- explicit `libqcocoa.dylib` bundling;
- `qt.conf` with `Plugins = PlugIns`;
- transitive Homebrew `-libpath` discovery for `macdeployqt`;
- fatal handling of unresolved `macdeployqt` deployment errors;
- post-deployment ad-hoc signing plus strict signature verification;
- packaged `--version` smoke test;
- standalone DMG creation after the verified app is complete.

## Native-build limitation in this execution environment

This packaging environment is Linux and does not contain Apple's Xcode/macOS SDK or a macOS Homebrew Qt installation. Therefore it cannot truthfully produce or execute the final macOS `.app`/`.dmg` here. The 5.3 ZIP contains the validated source and bootstrap/package builder; the actual standalone `.app` and DMG are produced on the target Mac by `./build.sh --os macos --clean`.

## 5.4 alpha1 packaging-environment note

The complete source/regression suite passes **29/29** test files in this packaging environment, including the new WaffleCast/album-art checks and the inherited 5.3 Stop/Play, Pause/Play, and single-instance regressions. A native Qt build was not executed here because this container does not provide the Qt6 development CMake package (`Qt6Config.cmake`). The bundle is intended to be compiled with the existing target-platform builders; the first target build remains the native compile/runtime validation for WaffleCast audio and UI behavior.
