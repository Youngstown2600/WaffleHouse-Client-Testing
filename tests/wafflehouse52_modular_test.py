#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
checks = []

def require(path, needle, label):
    text = (root / path).read_text(errors="replace")
    ok = needle in text
    checks.append((ok, label))

def forbid(path, needle, label):
    text = (root / path).read_text(errors="replace")
    ok = needle not in text
    checks.append((ok, label))

for f in ["src/xmppbackend.cpp", "src/sshbackend.cpp", "src/nntpbackend.cpp", "src/networktoolswindow.cpp"]:
    checks.append(((root / f).is_file(), f"new module source exists: {f}"))

require("CMakeLists.txt", 'project(WaffleHouseClient VERSION 5.5.0', "CMake project/version is WaffleHouse-Client 5.5")
for flag in ["WAFFLEHOUSE_ENABLE_XMPP", "WAFFLEHOUSE_ENABLE_SSH", "WAFFLEHOUSE_ENABLE_NNTP", "WAFFLEHOUSE_ENABLE_MOSH", "WAFFLEHOUSE_ENABLE_GOPHER", "WAFFLEHOUSE_ENABLE_GEMINI"]:
    require("CMakeLists.txt", flag, f"CMake switch {flag}")
for proto in ["Xmpp = 5", "Ssh = 6", "Nntp = 7"]:
    require("src/backend.h", proto, f"backend enum {proto}")
require("src/mainwindow.cpp", "if (BuildFeatures::Xmpp) m_protocol->addItem", "account creation UI is feature-driven")
require("src/mainwindow.cpp", "BuildFeatures::Xmpp", "GUI gates XMPP")
require("src/mainwindow.cpp", "BuildFeatures::Ssh", "GUI gates SSH")
require("src/mainwindow.cpp", "BuildFeatures::Nntp", "GUI gates NNTP")
require("src/networktoolswindow.cpp", "BuildFeatures::Gopher", "auxiliary modules gate Gopher")
require("src/networktoolswindow.cpp", "BuildFeatures::Gemini", "auxiliary modules gate Gemini")
require("src/networktoolswindow.cpp", "BuildFeatures::Mosh", "auxiliary modules gate Mosh")
forbid("CMakeLists.txt", "WAFFLEHOUSE_ENABLE_MAIL", "IMAP/SMTP CMake feature removed")
forbid("src/buildfeatures.h", "BuildFeatures::Mail", "IMAP/SMTP build feature removed")
forbid("src/networktoolswindow.cpp", "addMailTab", "IMAP/SMTP UI removed")
forbid("build.sh", "mail|imap|smtp|email", "IMAP/SMTP builder aliases removed")
require("build.sh", "--protocols", "Unix/macOS builder supports module selection")
require("scripts/build-windows-msys2.sh", "WAFFLEHOUSE_PROTOCOLS", "Windows builder supports module selection")
require("scripts/build-termux.sh", "--protocols", "Termux builder supports module selection")
require("data/wafflehouse-client.desktop", "Exec=wafflehouse-client", "desktop entry uses wafflehouse-client")

failed = [label for ok, label in checks if not ok]
for ok, label in checks:
    print(("PASS" if ok else "FAIL") + " - " + label)
if failed:
    print(f"\n{len(failed)} validation check(s) failed.", file=sys.stderr)
    sys.exit(1)
print(f"\nPASS - {len(checks)} WaffleHouse-Client modular-fork checks")
