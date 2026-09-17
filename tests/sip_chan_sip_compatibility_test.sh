#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="$ROOT/src/sipcore/Profile.cpp"
E="$ROOT/src/sipcore/SipEngine.cpp"
B="$ROOT/src/sipbackend.cpp"
grep -q 'asterisk-chan_sip' "$P"
grep -q 'SipCompatibility::AsteriskChanSip' "$E"
grep -q 'sipOutboundUse=false' "$E"
grep -q 'PJSUA_CONTACT_REWRITE_UNREGISTER' "$E"
grep -q 'PJSIP_CRED_DATA_PLAIN_PASSWD' "$E"
grep -q 'identityDomain=p.callerIdDomain.empty()?p.sipDomain:p.callerIdDomain' "$E"
grep -q 'sipCompatibilityFromString' "$B"
echo 'SIP legacy Asterisk chan_sip compatibility regression: PASS'
