#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
G="$ROOT/src/mainwindow.cpp"
B="$ROOT/src/oscarbackend.cpp"
H="$ROOT/src/oscarprotocol.h"

# The account context menu must not gate OSCAR chat on the BOS family list.
grep -q 'Join AIM Chat…' "$G"
grep -q 'ChatNav (0x000D) and Chat (0x000E) are redirect services' "$G"
if sed -n '/QAction \*joinChat =/,/connect(joinChat/p' "$G" | grep -q 'supportsFamily(Oscar::FAM_CHAT'; then
    echo 'FAIL: Join AIM Chat is still gated on BOS-advertised CHAT/CHATNAV families' >&2
    exit 1
fi
grep -q 'joinChat->setEnabled(state->connected' "$G"

# Joining must request the two separate OSCAR services on demand.
grep -q 'requestService(FAM_CHATNAV)' "$B"
grep -q 'requestService(' "$B"
grep -q 'FAM_CHAT,' "$B"
grep -q 'OS_SERVICE_REQUEST = 0x0004' "$H"

# The Feature Center must explain the BOS-vs-redirect distinction.
grep -q 'redirect services requested on demand' "$G"
grep -q 'QStringLiteral("TRY")' "$G"

echo '5.0r18 OSCAR ChatNav/Chat redirect menu regression: PASS'
