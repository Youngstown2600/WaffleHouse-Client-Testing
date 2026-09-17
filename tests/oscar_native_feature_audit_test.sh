#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
H="$ROOT/src/oscarprotocol.h"
B="$ROOT/src/oscarbackend.cpp"
BH="$ROOT/src/oscarbackend.h"
G="$ROOT/src/mainwindow.cpp"
T="$ROOT/src/terminalui.cpp"
S="$ROOT/src/backend.h"

# Core native presence/profile wire operations.
grep -q 'OS_IDLE_NOTIFICATION = 0x0011' "$H"
grep -q 'LOCATE_SET_INFO = 0x0004' "$H"
grep -q 'LOCATE_USER_INFO_QUERY2 = 0x0015' "$H"
grep -q 'LOCATE_TLV_PROFILE_DATA = 0x0002' "$H"
grep -q 'LOCATE_TLV_UNAVAILABLE_DATA = 0x0004' "$H"
grep -q 'USER_FLAG_UNAVAILABLE = 0x0020ULL' "$H"
grep -q 'm_bos->sendSnac(FAM_OSERVICE, OS_IDLE_NOTIFICATION, body)' "$B"
grep -q 'LOCATE_TLV_UNAVAILABLE_TYPE' "$B"
grep -q 'LOCATE_TLV_UNAVAILABLE_DATA' "$B"
grep -q 'LOCATE_TLV_PROFILE_TYPE' "$B"
grep -q 'LOCATE_TLV_PROFILE_DATA' "$B"
grep -q 'appendU32(body, 0x00000407)' "$B"

# Rich inbound buddy presence and native status interpretation.
grep -q 'buddyNativePresenceChanged' "$BH"
grep -q 'BUDDY_ONCOMING' "$B"
grep -q 'USER_STATUS_DND' "$B"
grep -q 'USER_STATUS_NA' "$B"
grep -q 'USER_STATUS_BUSY' "$B"
grep -q 'USER_STATUS_FREE_FOR_CHAT' "$B"
grep -q '\[oscar-presence\]' "$B"
grep -q 'oscarBuddyPresence' "$G"
grep -q 'oscarBuddyPresence' "$T"

# Initial presence rights, persistent profile replay, and truthful capabilities.
grep -q 'BUDDY_RIGHTS_FLAG_INITIAL_DEPARTS' "$B"
grep -q 'QString oscarProfile' "$S"
grep -q 'Replayed locally saved profile after BOS login' "$B"
grep -q '748f2420628711d18222444553540000.*native OSCAR chat' "$B"

# Broader native feature surfaces already present.
grep -q 'FEEDBAG_REQUEST_AUTHORIZE_TO_HOST' "$H"
grep -q 'PD_ADD_PERMIT' "$H"
grep -q 'ICBM_CLIENT_EVENT' "$H"
grep -q 'ICBM_SIN_RETRIEVE' "$H"
grep -q 'CHAT_MSG_TO_HOST' "$H"
grep -q 'ADMIN_INFO_CHANGE_REQUEST' "$H"
grep -q 'doRequestDirectoryInfo' "$B"
grep -q 'doFindByEmail' "$B"
grep -q 'doRequestWatcherList' "$B"
grep -q 'doRetrieveStoredMessages' "$B"
grep -q 'doTypingNotification' "$B"
grep -q 'doRequestAccountInfo' "$B"
grep -q 'supportsFamily' "$G"

# Native host-side state/notification coverage.
grep -q 'OS_USER_INFO_UPDATE = 0x000F' "$H"
grep -q 'OS_EVIL_NOTIFICATION = 0x0010' "$H"
grep -q 'snac.subtype == OS_USER_INFO_UPDATE' "$B"
grep -q 'snac.subtype == OS_EVIL_NOTIFICATION' "$B"
grep -q 'snac.subtype == BUDDY_REJECT_NOTIFICATION' "$B"
grep -q 'while (offset < snac.body.size())' "$B"

echo 'OSCAR native feature audit (presence/profile + feature surface): PASS'
