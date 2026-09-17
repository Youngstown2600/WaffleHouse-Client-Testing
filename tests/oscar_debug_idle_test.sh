#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="$ROOT/src/oscarprotocol.cpp"
H="$ROOT/src/oscarprotocol.h"
B="$ROOT/src/oscarbackend.cpp"
G="$ROOT/src/mainwindow.cpp"
T="$ROOT/src/terminalui.cpp"
S="$ROOT/src/backend.h"

grep -q 'QString oscarDebugMode = QStringLiteral("off")' "$S"
grep -q 'OSCAR debug / audit:' "$G"
grep -q 'Login audit — decoded auth/bootstrap, secrets redacted' "$G"
grep -q 'Full wire trace — all FLAP/SNAC + login audit, secrets redacted' "$G"
grep -q 'OSCAR audit (off/login/full)' "$T"
grep -q '\[oscar-login\]' "$B"
grep -q '\[oscar-wire\]' "$P"
grep -q 'TLV_PASSWORD_HASH' "$P"
grep -q '<redacted:' "$P"
grep -q 'signoffReason' "$P"
grep -q 'authErrorDescription' "$P"
grep -q 'BUCP_SECURID_REQUEST' "$B"

grep -q 'constexpr quint16 OS_IDLE_NOTIFICATION = 0x0011' "$H"
grep -q 'm_bos->sendSnac(FAM_OSERVICE, OS_IDLE_NOTIFICATION, body)' "$B"
grep -q 'appendU32(body, seconds)' "$B"
grep -q '\[oscar-idle\] native SNAC 0x0001/0x0011' "$B"
grep -q 'appendU32(idleBody, 0)' "$B"

echo 'OSCAR debug/audit + native idle regression: PASS'
