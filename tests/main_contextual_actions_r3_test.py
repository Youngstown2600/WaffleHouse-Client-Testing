#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
main = (root / "src/mainwindow.cpp").read_text(errors="replace")
header = (root / "src/mainwindow.h").read_text(errors="replace")
caps = (root / "src/core/capabilityregistry.cpp").read_text(errors="replace")
softphone = (root / "src/softphonewindow.cpp").read_text(errors="replace")
main_entry = (root / "src/main.cpp").read_text(errors="replace")
irc = (root / "src/ircbackend.cpp").read_text(errors="replace")

checks = {
    "Tools Command Palette removed": 'Command &Palette' not in main and 'Ctrl+Shift+P' not in main,
    "palette implementation removed": 'showCommandPalette' not in main and 'showCommandPalette' not in header,
    "hidden GUI command dispatcher removed": 'executeGuiCommand' not in main and 'executeGuiCommand' not in header,
    "GUI command argument parser removed": 'takeGuiArgument' not in main and 'resolveGuiAccount' not in main and 'resolveGuiAccount' not in header,
    "client capabilities mapped to Tools": 'Client &Capabilities…' in main,
    "unified contacts mapped to Tools": 'Unified &Contacts…' in main,
    "history mapped to Tools": 'Search &History…' in main,
    "softphone mapped to Tools": 'Open &Softphone…' in main,
    "transfers mapped to Tools": 'File Transfer &Log / Activity…' in main,
    "runtime environment mapped to Help": '&Runtime Environment…' in main and 'Runtime Environment — %1' in main,
    "help action remains": '%1 &Help…' in main,
    "new IM is account contextual": 'Start IM…' in main,
    "join room is account contextual": ('Join AIM Chat…' in main or 'Join IRC Channel…' in main),
    "OSCAR advanced actions are contextual": 'OSCAR Presence / Profile' in main and 'OSCAR Account Administration' in main and 'Advanced Raw OSCAR SNAC…' in main,
    "SIP diagnostics live in Softphone": 'Diagnostics' in softphone and 'Active Calls & Controls' in softphone,
    "Media has dedicated menu": 'addMenu(QStringLiteral("&Media"))' in main_entry,
    "notification configuration lives in Options": 'NotificationManager::configurableEvents()' in main and 'Options — %1' in main,
    "contextual GUI capability advertised": 'contextual-gui-actions' in caps and 'Contextual GUI Actions' in caps,
    "command-palette capability removed": 'command-palette' not in caps and 'Command Palette' not in caps,
    "help documents contextual model": 'CONTEXTUAL GUI ACTIONS' in main and 'no command bar and Tools has no Command Palette' in main,
    "protocol-native IRC slash commands retained": 'handleSlashCommand' in irc,
    "r2 account toolbar retained": all(x in main for x in ['QStringLiteral("Add")', 'QStringLiteral("Remove")', 'QStringLiteral("Edit")', 'QStringLiteral("Connect / Disconnect")']),
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(("PASS" if ok else "FAIL") + " - " + name)
if failed:
    print(f"\n{len(failed)} contextual-action regression check(s) failed.", file=sys.stderr)
    sys.exit(1)
print(f"\nPASS - {len(checks)} contextual-action regression checks")
