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

# Product identity / fork cleanup.
for path in root.rglob('*'):
    if not path.is_file() or any(part in {'build','.git'} for part in path.parts):
        continue
    if path.suffix.lower() in {'.png','.ico','.icns','.wav','.zip','.gz','.tar'}:
        continue
    try:
        text=path.read_text(errors='replace')
    except Exception:
        continue
    checks.append((('retro' + 'client') not in text.casefold(), f'no legacy fork branding in {path.relative_to(root)}'))

checks.append(((root/'src/networktoolswindow.cpp').exists(), 'NetworkToolsWindow source exists'))
checks.append(((root/'src/networktoolswindow.h').exists(), 'NetworkToolsWindow header exists'))
checks.append((not (root/'src/retroserviceswindow.cpp').exists(), 'legacy retroserviceswindow.cpp removed'))
checks.append((not (root/'src/retroserviceswindow.h').exists(), 'legacy retroserviceswindow.h removed'))
require('src/mainwindow.h', 'class NetworkToolsWindow;', 'main window uses WaffleHouse NetworkToolsWindow identity')
require('src/xmppbackend.cpp', 'wafflehouse-bind', 'XMPP transaction IDs use WaffleHouse namespace')
require('src/xmppbackend.cpp', 'WaffleHouseUser', 'XMPP fallback nick uses WaffleHouse naming')

# Header/splash are deliberately clean: name + version, no giant protocol subtitle.
require('src/mainwindow.cpp', 'QStringLiteral("%1 %2").arg(appDisplayName(), appVersionString())', 'main header contains application name and version')
forbid('src/mainwindow.cpp', 'BuildFeatures::enabledProtocolNames(true)', 'main header no longer prints every protocol')
forbid('src/main.cpp', 'auto *protocols = new QLabel', 'GUI splash no longer prints every protocol')
forbid('src/main.cpp', 'AUTO FRONTEND — GUI MODE', 'GUI splash reduced to product branding')
forbid('src/mainwindow.cpp', 'moduleCard', 'main-window module launcher removed')
forbid('src/mainwindow.cpp', 'm_commandInput', 'main-window Run input removed')
require('src/mainwindow.cpp', 'm_mainAddAccountButton = new QPushButton(QStringLiteral("Add")', 'main window has Add account button')
require('src/mainwindow.cpp', 'm_mainRemoveAccountButton = new QPushButton(QStringLiteral("Remove")', 'main window has Remove account button')
require('src/mainwindow.cpp', 'm_mainEditAccountButton = new QPushButton(QStringLiteral("Edit")', 'main window has Edit account button')
require('src/mainwindow.cpp', 'm_mainConnectToggleButton = new QPushButton(QStringLiteral("Connect / Disconnect")', 'main window has state-aware connect toggle')
require('src/mainwindow.cpp', 'void MainWindow::toggleSelectedConnection()', 'connect toggle implementation exists')
require('src/mainwindow.cpp', 'QStringLiteral("Account/Buddy List")', 'account/buddy header uses account-first wording')
require('src/mainwindow.cpp', 'QStringLiteral("Protocol / Status")', 'status header identifies protocol first')
require('src/mainwindow.cpp', 'resize(720, 540);', 'main window default size updated')
require('src/mainwindow.cpp', 'setMinimumSize(580, 430);', 'main window minimum size updated')

# SSH companion import stays additive to the PTY GUI backend.
checks.append(((root/'scripts/wafflehouse-ssh').exists(), 'SSH companion launcher included'))
require('scripts/wafflehouse-ssh', 'COMPANION_VERSION = "5.4-alpha1"', 'SSH companion version updated for 5.4 alpha1')
require('scripts/wafflehouse-ssh', 'ExitOnForwardFailure=yes', 'SSH companion preserves safe forwarding checks')
require('scripts/wafflehouse-ssh', 'StreamLocalBindUnlink=yes', 'SSH companion uses reverse Unix-socket forwarding')
require('CMakeLists.txt', 'install(PROGRAMS scripts/wafflehouse-ssh', 'Unix install includes SSH companion when SSH is enabled')
require('src/sshbackend.cpp', 'forkpty(&masterFd', 'embedded GUI SSH still uses PTY backend')

failed=[label for ok,label in checks if not ok]
for ok,label in checks:
    print(('PASS' if ok else 'FAIL')+' - '+label)
if failed:
    print(f'\n{len(failed)} r2 branding/GUI check(s) failed.', file=sys.stderr)
    for label in failed:
        print('  - '+label, file=sys.stderr)
    sys.exit(1)
print(f'\nPASS - {len(checks)} WaffleHouse-Client 5.3 branding/GUI checks')
