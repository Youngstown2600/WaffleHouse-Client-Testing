# WaffleHouse-Client 5.4 alpha2 — Validation

## Automated source/regression tests

All **30/30** test files under `tests/` pass in this source tree.

The suite includes the inherited Media Center Stop→Play and Pause→Play regressions, persistent media library protection, single-instance behavior, WaffleCast/album-art behavior, and the new 5.4 alpha2 manual/public-listening checks.

5.4 alpha2 specifically validates:

- Media Center **Connect / Listen** receiver control.
- WaffleCast URL normalization from the public page, direct MP3, M3U, and PLS URLs.
- Listener playback through `MediaController::playTransient()` so the persistent local playlist is not replaced.
- **Copy Public Link** host control.
- Browser listen page with HTML5 audio, live metadata polling, cover-art refresh, and listener count.
- Standard `audio/mpeg` live stream endpoint.
- M3U and PLS Internet-radio playlist endpoints.
- Random session-token scoping retained for every public endpoint.

## Native compile note

The packaging container does not contain the Qt 6 development package (`Qt6Config.cmake`), so a complete native Qt build cannot be performed here. CMake reaches `find_package(Qt6 ...)` and stops because Qt 6 headers/configuration are unavailable. The first Linux Mint/FreeBSD/macOS/Windows build remains the native compile/runtime validation.
