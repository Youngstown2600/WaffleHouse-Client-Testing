#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MEDIA="$ROOT/src/mediacontroller.cpp"
MEDIA_H="$ROOT/src/mediacontroller.h"
WINDOW="$ROOT/src/mediawindow.cpp"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -Fq 'loadPersistentLibrary();' "$MEDIA" || fail 'media library is not restored at controller startup'
grep -Fq 'QSaveFile library(persistentLibraryFile());' "$MEDIA" || fail 'atomic library.json persistence missing'
grep -Fq 'QStringLiteral("queue.m3u8")' "$MEDIA" || fail 'persistent M3U8 queue mirror missing'
grep -Fq 'QStringLiteral("playlists/%1-%2")' "$MEDIA" || fail 'internal imported-playlist cache missing'
grep -Fq 'QStringLiteral("pls"), QStringLiteral("m3u"), QStringLiteral("m3u8"), QStringLiteral("xspf")' "$MEDIA" || fail 'playlist cache type coverage missing'
grep -Fq 'sendLoadFile(savedSources.first(), QStringLiteral("replace"))' "$MEDIA" || fail 'direct lazy mpv queue rehydration missing'
grep -Fq 'm_backendPlaylistHydrated = false;' "$MEDIA" || fail 'fresh backend hydration state missing'
grep -Fq 'savePersistentLibrary();' "$MEDIA" || fail 'playlist mutation persistence missing'
grep -Fq 'QStringList playlistTitles() const' "$MEDIA_H" || fail 'persistent playlist title getter missing'
grep -Fq 'syncPlaylist(m_media->playlistSources(), m_media->playlistTitles(), m_media->playlistIndex());' "$WINDOW" || fail 'GUI does not seed restored library'
grep -Fq 'QStringLiteral("Library Folder")' "$WINDOW" || fail 'Media Library Folder shortcut missing'
grep -Fq 'QStringLiteral("Clear Library")' "$WINDOW" || fail 'persistent clear-library wording missing'
grep -Fq '*.xspf' "$WINDOW" || fail 'XSPF import filter missing'

# Shipping bundle should contain only the current validation report, not historical receipts.
find "$ROOT" -maxdepth 1 -type f -name 'VALIDATION-*.txt' | grep -q . && fail 'historical VALIDATION-*.txt files still shipped'
[ ! -e "$ROOT/R20-MERGE-AUDIT-5.1.txt" ] || fail 'one-off r20 merge audit still shipped'
[ ! -e "$ROOT/PLATFORM-COMPARISON-5.0r7-HISTORICAL.txt" ] || fail 'historical platform-comparison artifact still shipped'
[ -f "$ROOT/VALIDATION.md" ] || fail 'single current VALIDATION.md missing'

echo 'WaffleHouse-Client 5.2 inherited persistent-media + bundle-cleanup regression: PASS'
