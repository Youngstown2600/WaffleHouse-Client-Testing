#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
cpp = (root / 'src/mediacontroller.cpp').read_text()
hdr = (root / 'src/mediacontroller.h').read_text()
window = (root / 'src/mediawindow.cpp').read_text()

checks = []
def check(cond, label):
    checks.append((bool(cond), label))
    print(('PASS' if cond else 'FAIL') + ' - ' + label)

check('bool m_preserveLibraryWhileStopped = false;' in hdr,
      'controller tracks stop-time library preservation')

stop_match = re.search(r'void MediaController::stop\(\)\n\{(?P<body>.*?)\n\}\n\nvoid MediaController::next', cpp, re.S)
stop = stop_match.group('body') if stop_match else ''
check(bool(stop), 'stop() implementation found')
check('savePersistentLibrary();' in stop,
      'stop persists the application-owned queue before touching mpv')
check('m_preserveLibraryWhileStopped = !m_playlistSources.isEmpty();' in stop,
      'stop protects a non-empty library from backend snapshots')
check('m_backendPlaylistHydrated = false;' in stop,
      'stop forces next Play to rebuild mpv transport queue')
check('m_idle = true;' in stop and 'emit idleChanged(true);' in stop,
      'stop enters idle synchronously')
check('sendCommand({QStringLiteral("stop")}' in stop,
      'stop clears mpv transport queue instead of preserving stale backend state')
check('keep-playlist' not in stop,
      'stop no longer relies on mpv keep-playlist state')

hydrate_match = re.search(r'bool MediaController::hydrateBackendPlaylist\(int startIndex\)\n\{(?P<body>.*?)\n\}\n\nbool MediaController::backendAvailable', cpp, re.S)
hydrate = hydrate_match.group('body') if hydrate_match else ''
check(bool(hydrate), 'hydrateBackendPlaylist() implementation found')
check('const QStringList savedSources = m_playlistSources;' in hydrate,
      'rehydration snapshots the application-owned queue')
check('sendLoadFile(savedSources.first(), QStringLiteral("replace"))' in hydrate,
      'rehydration restarts playback directly with loadfile replace')
check('sendLoadFile(savedSources.at(i), QStringLiteral("append"))' in hydrate,
      'rehydration appends remaining tracks directly')
check('QStringLiteral("loadlist")' not in hydrate,
      'Stop -> Play rehydration no longer depends on queue.m3u8/loadlist')
check('QStringLiteral("playlist-play-index")' not in hydrate,
      'Stop -> Play no longer depends on playlist-play-index to wake playback')
check('select.append(QStringLiteral("playlist-pos"));' in hydrate and 'select.append(requestedIndex);' in hydrate,
      'nonzero saved index is selected using native numeric playlist-pos')

snapshot_match = re.search(r'void MediaController::refreshPlaylistSnapshot\(const QJsonValue &data\)\n\{(?P<body>.*?)\n\}\n\nvoid MediaController::parseIpcLine', cpp, re.S)
snapshot = snapshot_match.group('body') if snapshot_match else ''
check('m_preserveLibraryWhileStopped && !m_playlistSources.isEmpty()' in snapshot,
      'playlist snapshots are guarded while stopped/rebuilding')
check('m_idle || array.size() != m_playlistSources.size()' in snapshot,
      'transient empty/partial backend playlists cannot shrink persistent library')

play_match = re.search(r'connect\(play, &QPushButton::clicked, this, \[this\] \{(?P<body>.*?)\n    \}\);', window, re.S)
play = play_match.group('body') if play_match else ''
check('m_playlist->currentRow() >= 0' in play and 'playSelected();' in play,
      'GUI Play uses visible selected/current row as authoritative restart target')
check('else m_media->resume();' in play,
      'GUI Play still resumes when no playlist row is selected')
check('emit playlistEntriesChanged(m_playlistSources, m_playlistTitles, m_playlistIndex);' in cpp,
      'mpv playlist position changes keep the GUI current row synchronized')

failed = [label for ok, label in checks if not ok]
print()
if failed:
    print(f'{len(failed)} media stop/resume regression check(s) failed.')
    sys.exit(1)
print(f'PASS - {len(checks)} WaffleHouse-Client 5.3 media stop/resume regression checks')
