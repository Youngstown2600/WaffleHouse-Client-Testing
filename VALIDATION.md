# WaffleHouse-Client 5.5 — Validation

## Automated source/regression tests

All **30/30** test files under `tests/` pass in this source tree.

The suite includes the inherited Media Center Stop→Play and Pause→Play regressions, persistent media library protection, single-instance behavior, WaffleCast/album-art behavior, and the new 5.5 manual/public-listening checks.

5.5 specifically validates:

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


## WaffleHouse-Client 5.5 remote WaffleCast update

- 32/32 packaged source/regression test files pass after adding saved Internet broadcast settings, independent local/public ports, LAN hairpin avoidance, and remote bare-host resolution.
- Added `tests/r55_wafflecast_remote_broadcast_test.py` with 11 targeted checks.
- CMake configuration in the packaging container reaches dependency discovery but cannot complete because Qt6 development package/configuration files are not installed in the container. Native Linux Mint/FreeBSD/macOS/Windows builds remain the runtime compile validation.
