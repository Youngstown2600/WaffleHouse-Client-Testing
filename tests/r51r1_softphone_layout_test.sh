#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/src/softphonewindow.cpp"
fail() { echo "FAIL: $*" >&2; exit 1; }
grep -q 'setFixedHeight(58)' "$SRC" || fail 'status banner is not height-limited'
grep -q 'Destination:"), dialBox), 4, 0' "$SRC" || fail 'Destination does not have its dedicated row'
grep -q 'addWidget(m_destination, 4, 1, 1, 3)' "$SRC" || fail 'Destination is not full-width'
grep -q 'setFixedSize(76, 58)' "$SRC" || fail 'desktop keypad sizing missing'
# r1 required transfer actions to have enough room. r3 intentionally compacts
# them into the second four-button row using shorter visible labels/tooltips.
if grep -q 'addWidget(blindTransfer, 2, 0, 1, 2)' "$SRC"; then
  grep -q 'addWidget(attendedTransfer, 2, 2, 1, 2)' "$SRC" || fail 'Attended Transfer is not half-row width'
else
  grep -q 'addWidget(blindTransfer, 1, 2)' "$SRC" || fail 'Blind Transfer compact placement missing'
  grep -q 'addWidget(attendedTransfer, 1, 3)' "$SRC" || fail 'Attended Transfer compact placement missing'
  grep -q 'Blind Xfer' "$SRC" || fail 'compact Blind Transfer label missing'
  grep -q 'Attended Xfer' "$SRC" || fail 'compact Attended Transfer label missing'
fi
grep -q 'setSizes({455, 585})' "$SRC" || fail 'softphone splitter balance missing'
if grep -q 'addWidget(m_destination, 3, 3)' "$SRC"; then fail 'legacy squeezed destination layout still present'; fi
echo 'WaffleHouse-Client 5.1r1 softphone GUI layout regression: PASS'
