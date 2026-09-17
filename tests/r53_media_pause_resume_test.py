#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
cpp = (root / 'src/mediacontroller.cpp').read_text()
hdr = (root / 'src/mediacontroller.h').read_text()
window = (root / 'src/mediawindow.cpp').read_text()

checks=[]
def check(cond,label):
    checks.append((bool(cond),label))
    print(('PASS' if cond else 'FAIL')+' - '+label)

check('bool setProperty(const QString &name, const QJsonValue &value);' in hdr,
      'setProperty reports whether the IPC command was queued')
check('bool MediaController::setProperty' in cpp and 'return sendJsonCommand(command' in cpp,
      'setProperty returns IPC write success')

pause_m=re.search(r'void MediaController::pause\(\)\n\{(?P<body>.*?)\n\}\n\nvoid MediaController::resume',cpp,re.S)
pause=pause_m.group('body') if pause_m else ''
check('if (m_idle || m_paused) return;' in pause,
      'pause ignores stopped/idle transport and duplicate pause commands')
check('if (setProperty(QStringLiteral("pause"), true))' in pause and 'm_paused = true;' in pause,
      'pause marks local state immediately after successful IPC write')

resume_m=re.search(r'void MediaController::resume\(\)\n\{(?P<body>.*?)\n\}\n\nvoid MediaController::togglePause',cpp,re.S)
resume=resume_m.group('body') if resume_m else ''
check('if (m_idle && !m_playlistSources.isEmpty())' in resume,
      'resume retains Stop -> Play rebuild handling')
check('if (!m_paused) return;' in resume,
      'normal resume only operates on paused transport')
check('if (setProperty(QStringLiteral("pause"), false))' in resume and 'm_paused = false;' in resume,
      'resume clears mpv pause and local pause state directly')
check('playlist-play-index' not in resume.split('// Resume must only clear')[1] if '// Resume must only clear' in resume else False,
      'paused resume path does not touch playlist index')

play_m=re.search(r'connect\(play, &QPushButton::clicked, this, \[this\] \{(?P<body>.*?)\n    \}\);',window,re.S)
play=play_m.group('body') if play_m else ''
paused_pos=play.find('if (m_media->paused())')
idle_pos=play.find('if (m_media->idle())')
check(paused_pos >= 0 and idle_pos >= 0 and paused_pos < idle_pos,
      'GUI Play checks paused state before stopped/idle state')
check('m_media->paused()) {\n            m_media->resume();' in play,
      'GUI Play resumes paused playback instead of selecting playlist index')
check('if (m_media->idle())' in play and 'playSelected();' in play,
      'GUI Play still rebuilds selected row after Stop')

toggle_m=re.search(r'void MediaController::togglePause\(\)\n\{(?P<body>.*?)\n\}',cpp,re.S)
toggle=toggle_m.group('body') if toggle_m else ''
check('m_paused ? resume() : pause();' in toggle and 'cycle' not in toggle,
      'toggle pause uses state-aware pause/resume path')

index_m=re.search(r'void MediaController::playPlaylistIndex\(int index\)\n\{(?P<body>.*?)\n\}\n\nvoid MediaController::removePlaylistIndex',cpp,re.S)
index=index_m.group('body') if index_m else ''
check('m_paused && setProperty(QStringLiteral("pause"), false)' in index,
      'explicit track selection unpauses transport before selecting track')

failed=[label for ok,label in checks if not ok]
print()
if failed:
    print(f'{len(failed)} pause/resume regression check(s) failed.')
    sys.exit(1)
print(f'PASS - {len(checks)} WaffleHouse-Client 5.3 pause/resume regression checks')
