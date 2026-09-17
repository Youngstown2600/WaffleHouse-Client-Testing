#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
ssh = (root / 'src/sshbackend.cpp').read_text()
net = (root / 'src/networktoolswindow.cpp').read_text()

checks = []

def check(cond, label):
    checks.append((bool(cond), label))
    print(('PASS' if cond else 'FAIL') + ' - ' + label)

check('void writeBestEffort(' in ssh, 'SSH warning-safe write helper exists')
check('::write(STDERR_FILENO, prefix' not in ssh, 'exec failure path no longer ignores write() result')
check('(void)::write(m_masterFd, exitCommand' not in ssh, 'SSH shutdown no longer casts away write() result')
check('writeBestEffort(m_masterFd, exitCommand' in ssh, 'SSH shutdown uses checked best-effort writer')
check('while (socket.waitForReadyRead(1500)) {' in net, 'Gopher receive loop is explicitly braced')
check('while (socket.waitForReadyRead(2000)) {' in net, 'Gemini receive loop is explicitly braced')
check('#ifdef Q_OS_MACOS\nQString shellQuote' in net, 'shellQuote is compiled only on macOS')
check(not re.search(r'while\s*\([^\n]+\)\s*[^\{\n][^\n]*;\s*data\s*\+=\s*socket\.readAll\(\);', net),
      'no misleading one-line socket receive loop remains')

failed = [label for ok, label in checks if not ok]
print()
if failed:
    print(f'{len(failed)} warning-cleanup validation check(s) failed.')
    sys.exit(1)
print(f'PASS - {len(checks)} WaffleHouse-Client 5.2r3 compiler-warning cleanup checks')
