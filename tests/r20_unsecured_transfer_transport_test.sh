#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FT="$ROOT/src/filetransport.h"
MAIN="$ROOT/src/mainwindow.cpp"
CLI="$ROOT/src/terminalui.cpp"

fail() { echo "FAIL: $*" >&2; exit 1; }

grep -Fq 'QStringLiteral("[[WHFILE2:")' "$FT" || fail 'printable WHFILE2 prefix missing'
grep -Fq 'QStringLiteral("]]"' "$FT" || fail 'WHFILE2 suffix missing'
grep -Fq 'return unsecuredPrefixV2() + QString::fromLatin1(encoded) + unsecuredSuffixV2();' "$FT" || fail 'outgoing unsecured frames are not using v2'
grep -Fq 'legacyUnsecuredPrefixV1()' "$FT" || fail 'exact WHFILE1 compatibility missing'
grep -Fq 'legacyUnsecuredPrefixV1WithoutSeparator()' "$FT" || fail 'stripped WHFILE1 compatibility missing'
grep -Fq 'QStringLiteral("WHFILE1|")' "$FT" || fail 'bare legacy prefix not accepted'
grep -Fq 'CpxFileTransferManager::looksLikeMessage(candidate)' "$FT" || fail 'decoded payload validation missing'
grep -Fq 'WaffleFileTransport::unwrapUnsecured(payload, filePayload)' "$MAIN" || fail 'GUI does not consume unsecured transfer frames'
grep -Fq 'WaffleFileTransport::unwrapUnsecured(payload, filePayload)' "$CLI" || fail 'CLI does not consume unsecured transfer frames'

# The legacy control byte may remain only in compatibility framing, not the v2 sender.
python3 - "$FT" <<'PY'
import pathlib, sys
s=pathlib.Path(sys.argv[1]).read_text()
start=s.index('inline QString wrapUnsecured')
end=s.index('inline bool decodeCandidate', start)
body=s[start:end]
assert 'QChar(0x1e)' not in body, 'v2 sender still depends on Record Separator'
assert 'unsecuredPrefixV2()' in body
PY

echo 'PASS: r20 printable unsecured transfer envelope + legacy receive compatibility'
