#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
grep -q 'Asterisk PJSIP (chan_pjsip)' "$ROOT/src/mainwindow.cpp"
grep -q 'Asterisk legacy SIP (chan_sip)' "$ROOT/src/mainwindow.cpp"
grep -q 'pjsip.conf / chan_pjsip' "$ROOT/src/mainwindow.cpp"
grep -q 'sip.conf / chan_sip' "$ROOT/src/mainwindow.cpp"
grep -q 'SIP server type (auto/pjsip/chan_sip)' "$ROOT/src/terminalui.cpp"
grep -q 'v=="pjsip"' "$ROOT/src/sipcore/Profile.cpp"
echo "SIP server type selector regression: PASS"
