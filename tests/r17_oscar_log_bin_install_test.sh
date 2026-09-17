#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
B="$ROOT/src/oscarbackend.cpp"
H="$ROOT/src/oscarbackend.h"
G="$ROOT/src/mainwindow.cpp"
U="$ROOT/scripts/build-unix.sh"

# Persistent, per-account OSCAR audit file.
grep -q 'QString auditLogPath() const' "$H"
grep -q 'QStandardPaths::AppDataLocation' "$B"
grep -q 'logs/oscar-%1.log' "$B"
grep -q '5 \* 1024 \* 1024' "$B"
grep -q 'QStringLiteral(".old")' "$B"
grep -q 'QIODevice::Append' "$B"
grep -q 'Qt::ISODateWithMs' "$B"

# In-app viewer from Tools and the OSCAR account context menu.
grep -q 'View OSCAR &Audit Log…' "$G"
grep -q 'View OSCAR Audit Log…' "$G"
grep -q 'void MainWindow::showOscarAuditLog' "$G"
grep -q 'Showing the last 512 KiB' "$G"
grep -q 'Log file: %1' "$G"

# Successful Unix builds explicitly offer a /bin launcher; default stays No.
grep -q 'SYSTEM_BIN=/bin/wafflehouse-client' "$U"
grep -q 'Install WaffleHouse-Client and add it to /bin' "$U"
grep -q 'INSTALL_BIN_ALIAS_MODE=1' "$U"
grep -q 'ln -s "$INSTALL_BIN" "$SYSTEM_BIN"' "$U"
grep -q 'Installed /bin launcher:' "$U"

echo '5.0r17 OSCAR log viewer + /bin install prompt regression: PASS'
