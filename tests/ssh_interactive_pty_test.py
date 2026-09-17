#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
checks=[]

def require(path, needle, label):
    text=(root/path).read_text(errors='replace')
    checks.append((needle in text,label))

def forbid(path, needle, label):
    text=(root/path).read_text(errors='replace')
    checks.append((needle not in text,label))

require('src/sshbackend.cpp', 'forkpty(&masterFd', 'Unix SSH uses a real local PTY')
require('src/sshbackend.cpp', 'QSocketNotifier', 'PTY output is integrated with the Qt event loop')
require('src/sshbackend.cpp', 'BatchMode=no', 'OpenSSH batch mode is disabled so interactive authentication can prompt')
require('src/sshbackend.cpp', 'NumberOfPasswordPrompts=3', 'OpenSSH password prompting remains enabled')
require('src/sshbackend.cpp', 'TIOCSWINSZ', 'SSH terminal resize reaches the PTY')
require('src/sshbackend.cpp', 'sendTerminalInput(const QByteArray &bytes)', 'SSH backend accepts raw terminal bytes')
require('src/backend.h', 'QString sshIdentityFile;', 'SSH profiles persist an optional identity file')
require('src/backend.h', 'QStringList sshOptions;', 'SSH profiles persist OpenSSH -o overrides')
require('src/backend.h', 'QString sshRemoteCommand;', 'SSH profiles persist an optional remote command')
require('src/sshbackend.cpp', 'args << QStringLiteral("-i") << identityFile;', 'SSH backend passes identity file directly to OpenSSH')
require('src/sshbackend.cpp', 'args << QStringLiteral("-o") << option;', 'SSH backend passes per-profile OpenSSH options')
require('src/mainwindow.cpp', 'Choose SSH Identity File', 'GUI provides SSH identity file chooser')
require('src/mainwindow.cpp', 'StrictHostKeyChecking=accept-new', 'GUI documents OpenSSH option overrides')
mainwindow=(root/'src/mainwindow.cpp').read_text(errors='replace')
start=mainwindow.find('connect(window, &ChatWindow::terminalBytesSubmitted')
end=mainwindow.find('connect(window, &ChatWindow::secureRequested', start)
dispatch=mainwindow[start:end] if start >= 0 and end > start else ''
checks.append(('ConnectionSettings::Protocol::Telnet' in dispatch
               and 'ConnectionSettings::Protocol::Ssh' in dispatch
               and 'sendTerminalInput(bytes)' in dispatch,
               'GUI terminal keystroke dispatcher forwards raw input to both Telnet and SSH'))
require('src/chatwindow.cpp', 'interactive PTY', 'SSH window identifies interactive PTY mode')
require('src/chatwindow.cpp', 'password typing is hidden', 'SSH window explains hidden password entry')
require('src/chatwindow.cpp', 'm_terminal->setFocus(Qt::OtherFocusReason)', 'Terminal window automatically takes keyboard focus')
require('CMakeLists.txt', 'find_library(WAFFLEHOUSE_UTIL_LIBRARY util)', 'Unix build links libutil when required for forkpty')
forbid('src/sshbackend.cpp', 'A saved password is not injected into OpenSSH', 'obsolete pipe-mode password warning removed from Unix path')

failed=[label for ok,label in checks if not ok]
for ok,label in checks:
    print(('PASS' if ok else 'FAIL')+' - '+label)
if failed:
    print(f'\n{len(failed)} SSH regression check(s) failed.', file=sys.stderr)
    sys.exit(1)
print(f'\nPASS - {len(checks)} SSH PTY regression checks')
