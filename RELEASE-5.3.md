# WaffleHouse-Client 5.3

## Single-instance hotfix r4

- Added a cross-platform single-instance guard shared by GUI and CLI. Only one WaffleHouse-Client session may run for the current user at a time.
- A second GUI launch displays an already-running message and exits; a second CLI launch reports the same condition on stderr and exits.
- `--help` and `--version` remain available even while the main WaffleHouse-Client process is running.
- Includes the r3 Pause -> Play fix and r2 Stop -> Play fix unchanged.

## Release focus: macOS bootstrap and standalone packaging

WaffleHouse-Client 5.3 promotes the 5.2r3 Update Merger source into a new release and overhauls the macOS builder around the failures observed on macOS 13 Ventura.

### macOS builder

- Detects and configures a full Xcode installation instead of silently accepting Command Line Tools.
- On Ventura, opens the compatible archived Apple Xcode download (15.2 for 13.5+, 14.3.1 for 13.0-13.4) and can extract/move/configure the downloaded Xcode application after the user completes Apple's authenticated download.
- Installs Homebrew with Homebrew's official installer when missing.
- Replaces the broad `qt` meta-package requirement with the smaller `qtbase`, `qtmultimedia`, and `qttools` set used by WaffleHouse.
- Preserves managed PJSIP 2.17 and the existing compile-time module selection.

### macOS deployment repair

- Seeds and verifies the Cocoa Qt platform plugin (`libqcocoa.dylib`).
- Writes an application-local `qt.conf` with `Plugins = PlugIns`.
- Passes installed Homebrew dependency library paths to `macdeployqt`, including transitive libraries needed by split Qt modules.
- Runs `macdeployqt` without intermediate signing, then ad-hoc signs only after the bundle is complete.
- Treats unresolved deployment errors as fatal instead of printing a misleading success message.
- Audits every bundled Mach-O payload for missing `@rpath` targets or machine-local Homebrew/source paths.
- Verifies the final code signature and runs a packaged `--version` smoke test.
- Creates a self-contained drag-to-Applications DMG by default.

## Media transport hotfix r3

- Fixed Pause -> Play so Play resumes the paused mpv transport directly and does not touch the playlist queue/index.
- Pause/resume state is synchronized immediately after successful IPC writes, preventing rapid Pause -> Play clicks from racing mpv property notifications.
- Selecting a different playlist entry while paused now unpauses before starting that entry.
- Retains the r2 Stop -> Play rebuild behavior unchanged.
- Added dedicated Pause -> Play regression coverage.

## Media transport hotfix r2

- Media Center Stop now clears mpv's transport queue while preserving WaffleHouse's application-owned playlist and current index.
- Play rebuilds mpv directly from the saved media paths with `loadfile replace` + `append`; it no longer relies on restoring `queue.m3u8`, `keep-playlist`, or `playlist-play-index` to wake a stopped backend.
- Empty and partial backend playlist notifications are ignored during Stop/rebuild so a populated library cannot collapse while mpv is transitioning.
- The GUI current row now follows mpv playlist-position changes and is used as the restart target when available.
- Dedicated Stop -> Play regression coverage now checks the direct queue reconstruction path.

### Release identity

- Application version: **5.3**
- CMake project version: **5.3.0**
- Retains all 5.2r3 protocol, GUI, SSH PTY, warning-cleanup, and Update Merger behavior.
- Settings and wire compatibility remain continuous with the existing WaffleHouse-Client line.

### Build

```sh
./build.sh
```

For macOS-only environment setup:

```sh
./build.sh --os macos --bootstrap-only
```

For a clean macOS build and default DMG:

```sh
./build.sh --os macos --clean
```
