#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
need() { pattern=$1; file=$2; grep -q -- "$pattern" "$ROOT/$file" || { echo "FAIL: $file missing $pattern" >&2; exit 1; }; }
need 'WAFFLEHOUSE_ENABLE_OSCAR' CMakeLists.txt
need 'WAFFLEHOUSE_ENABLE_IRC' CMakeLists.txt
need 'WAFFLEHOUSE_ENABLE_TELNET' CMakeLists.txt
need 'WAFFLEHOUSE_ENABLE_SIP' CMakeLists.txt
need 'WAFFLEHOUSE_ENABLE_MEDIA' CMakeLists.txt
need '\-\-protocols' build.sh
need 'PJSUA_MAX_ACC >= 32' src/sipcore/SipEngine.cpp
need 'AsteriskChanSip' src/sipcore/SipEngine.cpp
need 'telnetColumns' src/backend.h
need 'telnetRows' src/backend.h
need 'setTerminalGeometry' src/ansiterminalwidget.cpp
need 'setIdleSeconds' src/mainwindow.cpp
need '0x0001/0x0011' src/oscarbackend.cpp
need '\[oscar-idle\]' src/oscarbackend.cpp
need 'oscarDebugMode' src/mainwindow.cpp
if grep -q 'Auto-away — inactive' "$ROOT/src/mainwindow.cpp" "$ROOT/src/terminalui.cpp"; then
  echo 'FAIL: client-generated AIM auto-away remains' >&2; exit 1
fi
echo '5.0r13 selectable protocols + multi-SIP + BBS geometry + native OSCAR idle/debug: PASS'
