#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S="$ROOT/scripts/build-macos.sh"
grep -q 'Bootstrapping Homebrew for Intel macOS' "$S"
grep -q 'brew install --build-from-source' "$S"
grep -q 'ensure_qt_component_config qtmultimedia Multimedia' "$S"
grep -q '\$QTBASE_PREFIX/bin/macdeployqt' "$S"
grep -q 'Repairing Qt deployment tooling' "$S"
grep -q 'Required macOS build tool' "$S"
echo 'macOS 5.6 builder hardening regression: PASS'
