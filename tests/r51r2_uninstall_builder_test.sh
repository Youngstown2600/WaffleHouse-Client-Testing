#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOP="$ROOT/build.sh"
UNIX="$ROOT/scripts/build-unix.sh"
MAC="$ROOT/scripts/build-macos.sh"
TERMUX="$ROOT/scripts/build-termux.sh"
WIN="$ROOT/build-windows.ps1"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Top-level lifecycle choice must happen before compile-feature questions.
grep -Fq '2) Uninstall / Remove the installed WaffleHouse-Client' "$TOP" || fail 'top-level interactive uninstall choice missing'
grep -Fq 'LIFECYCLE_ACTION=uninstall' "$TOP" || fail 'top-level uninstall action missing'
grep -Fq 'exec "$PLATFORM_SCRIPT" "$@"' "$TOP" || fail 'top-level platform dispatch missing'
python3 - "$TOP" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
a=s.index('# Lifecycle selection happens before protocol questions')
b=s.index('# Compile-time feature selection')
assert a < b, 'uninstall selection occurs after protocol selection'
PY

# Linux/FreeBSD removal includes every installed app payload class but not user config.
grep -Fq 'INSTALL_SHELL="$INSTALL_BINDIR/wafflehouse-shell"' "$UNIX" || fail 'Unix shell helper cleanup target missing'
grep -Fq 'INSTALL_DATADIR="$INSTALL_PREFIX/share/wafflehouse-client"' "$UNIX" || fail 'Unix shared data cleanup target missing'
grep -Fq 'remove_path_if_present "$INSTALL_SHELL"' "$UNIX" || fail 'Unix shell helper not removed'
grep -Fq 'remove_tree_if_present "$INSTALL_DATADIR"' "$UNIX" || fail 'Unix shared data not removed'
grep -Fq 'Per-user WaffleHouse-Client/WaffleHouse configuration was preserved.' "$UNIX" || fail 'Unix preservation message missing'

# macOS uninstall must exit before Homebrew/dependency work.
grep -Fq -- '--uninstall|--remove-only) UNINSTALL_MODE=1' "$MAC" || fail 'macOS uninstall option missing'
grep -Fq 'run_admin rm -rf "$INSTALL_APP"' "$MAC" || fail 'macOS app removal missing'
grep -Fq 'run_admin rm -f "$INSTALL_BIN"' "$MAC" || fail 'macOS launcher removal missing'
python3 - "$MAC" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
assert s.index('if [ "$UNINSTALL_MODE" -eq 1 ]; then') < s.index('command -v brew'), 'macOS uninstall reaches Homebrew first'
PY

# Termux removal must happen before package update/dependency install.
grep -Fq -- '--uninstall|--remove-only) ACTION=uninstall' "$TERMUX" || fail 'Termux uninstall option missing'
grep -Fq 'rm -f "$PREFIX/bin/wafflehouse-client"' "$TERMUX" || fail 'Termux binary removal missing'
python3 - "$TERMUX" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
assert s.index('if [[ "$ACTION" == uninstall ]]') < s.index('pkg update -y'), 'Termux uninstall reaches package manager first'
PY

# Windows is portable: remove only the builder-managed package.
grep -Fq '[switch]$Uninstall' "$WIN" || fail 'Windows portable remove switch missing'
grep -Fq 'Remove-Item -Recurse -Force $PackageDir' "$WIN" || fail 'Windows portable package removal missing'
grep -Fq 'Per-user WaffleHouse-Client/WaffleHouse configuration was preserved.' "$WIN" || fail 'Windows preservation message missing'

echo 'WaffleHouse-Client 5.1r2 uninstall/remove builder regression: PASS'
