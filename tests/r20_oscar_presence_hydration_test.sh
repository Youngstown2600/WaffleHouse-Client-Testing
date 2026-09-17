#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPP="$ROOT/src/oscarbackend.cpp"
HDR="$ROOT/src/oscarbackend.h"
MAIN="$ROOT/src/mainwindow.cpp"

fail() { echo "FAIL: $*" >&2; exit 1; }

grep -Fq 'requestBuddyPresenceHydration' "$HDR" || fail 'presence hydration declaration missing'
grep -Fq 'refreshOnlineBuddyPresence' "$HDR" || fail 'periodic presence refresh declaration missing'
grep -Fq 'm_pendingPresenceLookups' "$HDR" || fail 'async LOCATE request tracking missing'
grep -Fq 'm_onlineBuddyNames' "$HDR" || fail 'online buddy tracking missing'
grep -Fq 'appendU32(body, 0x00000002);' "$CPP" || fail 'Query2 UNAVAILABLE lookup missing'
grep -Fq 'appendU16(body, 0x0003);' "$CPP" || fail 'classic away-query fallback missing'
grep -Fq 'm_presenceUseLegacyLocate = true;' "$CPP" || fail 'session fallback preference missing'
grep -Fq 'LOCATE_TLV_UNAVAILABLE_DATA' "$CPP" || fail 'away-message TLV not consumed'
grep -Fq 'USERINFO_TLV_IDLE_TIME' "$CPP" || fail 'idle-time TLV not consumed'
grep -Fq 'emit buddyNativePresenceChanged(displayName, native);' "$CPP" || fail 'hydrated native presence not emitted'
grep -Fq 'if (online) requestBuddyPresenceHydration(user.name, false);' "$CPP" || fail 'buddy updates do not trigger hydration'
grep -Fq 'buddyPresenceRefresh.elapsed() >= 60000' "$CPP" || fail 'periodic online-buddy refresh missing'
grep -Fq 'QVariantMap merged = state->oscarBuddyPresence.value(key);' "$MAIN" || fail 'hydrated presence does not merge with native buddy metadata'
grep -Fq 'state->oscarBuddyPresence.insert(key, presence);' "$MAIN" || fail 'offline presence does not replace stale cached state'
grep -Fq 'connect(oscar, &OscarBackend::userInfoReceived, this' "$MAIN" || fail 'manual User Info does not refresh buddy cache'
grep -Fq 'info.value(QStringLiteral("awayMessage"))' "$MAIN" || fail 'GUI cache does not retain away text'
grep -Fq 'info.value(QStringLiteral("idleSeconds"), 0)' "$MAIN" || fail 'GUI cache does not retain idle seconds'

# Verify the hydration implementation is shared; it must not be hidden behind a
# non-macOS preprocessor condition.
python3 - "$CPP" <<'PY'
import pathlib, sys
s=pathlib.Path(sys.argv[1]).read_text()
a=s.index('void OscarBackend::requestBuddyPresenceHydration')
b=s.index('void OscarBackend::discoverBosCapabilities', a)
chunk=s[a:b]
assert '#ifdef' not in chunk and '#ifndef' not in chunk, 'presence hydration is platform-gated'
PY

echo 'PASS: r20 OSCAR LOCATE away/idle presence hydration'
