#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
main = (root / "src/mainwindow.cpp").read_text(errors="replace")
header = (root / "src/mainwindow.h").read_text(errors="replace")

checks = {
    "protocol launcher card removed": "moduleCard" not in main and "addModuleButton" not in main,
    "Run command row removed": "m_commandInput" not in main and "m_commandRunButton" not in main,
    "Add button exists": 'm_mainAddAccountButton = new QPushButton(QStringLiteral("Add")' in main,
    "Remove button exists": 'm_mainRemoveAccountButton = new QPushButton(QStringLiteral("Remove")' in main,
    "Edit button exists": 'm_mainEditAccountButton = new QPushButton(QStringLiteral("Edit")' in main,
    "Connect/Disconnect button exists": 'm_mainConnectToggleButton = new QPushButton(QStringLiteral("Connect / Disconnect")' in main,
    "toggle connects disconnected selection": "else {\n        connectSelected();\n    }" in main,
    "toggle disconnects connected selection": "if (state->connected || state->connecting) {\n        disconnectSelected();" in main,
    "button label changes to Connect": 'QStringLiteral("Connect")' in main,
    "button label changes to Disconnect": 'QStringLiteral("Disconnect")' in main,
    "toolbar selection follows account tree": "stateFromBuddyItem(current)" in main,
    "header declares toggle": "void toggleSelectedConnection();" in header,
    "old Run members removed from header": "m_commandInput" not in header and "m_commandRunButton" not in header,
    "account-first tree header": 'QStringLiteral("Account/Buddy List")' in main,
    "protocol/status tree header": 'QStringLiteral("Protocol / Status")' in main,
}

failed = [label for label, ok in checks.items() if not ok]
for label, ok in checks.items():
    print(("PASS" if ok else "FAIL") + " - " + label)
if failed:
    print(f"\n{len(failed)} account-toolbar regression check(s) failed.", file=sys.stderr)
    sys.exit(1)
print(f"\nPASS - {len(checks)} account-toolbar regression checks")
