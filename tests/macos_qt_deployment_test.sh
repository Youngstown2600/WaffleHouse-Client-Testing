#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
B="$ROOT/scripts/build-macos.sh"
TOP="$ROOT/build.sh"

# 5.3 must bootstrap the development environment instead of assuming it exists.
grep -q 'BOOTSTRAP_ONLY=0' "$TOP"
grep -q '\-\-bootstrap-only' "$TOP"
grep -q 'ensure_xcode' "$B"
grep -q 'ensure_homebrew' "$B"
grep -q 'Xcode_15.2.xip' "$B"
grep -q 'raw.githubusercontent.com/Homebrew/install/HEAD/install.sh' "$B"
grep -q 'for formula in cmake pkg-config qtbase qtmultimedia qttools libsodium ncurses portaudio opus' "$B"

# Deployment must be self-contained and fail closed on macdeployqt errors.
grep -q 'libqcocoa.dylib' "$B"
grep -q 'Plugins = PlugIns' "$B"
grep -q 'brew deps --installed --formula' "$B"
grep -q -- '-no-codesign' "$B"
grep -q 'Refusing to mark this build successful while deployment errors remain' "$B"
grep -q 'bundle-dependency-audit.log' "$B"
grep -q 'MISSING_RPATH' "$B"
grep -q 'EXTERNAL_BUILD_PATH' "$B"
grep -q 'codesign --force --deep --sign -' "$B"
grep -q 'codesign --verify --deep --strict' "$B"
grep -q 'hdiutil create' "$B"
grep -q 'WaffleHouse-Client-$RELEASE_VERSION-macOS.dmg' "$B"

# The old giant Qt meta-package must no longer be a required formula.
! grep -q 'for formula in cmake pkg-config qt libsodium' "$B"

echo "macOS 5.3 bootstrap/deployment regression: PASS"
