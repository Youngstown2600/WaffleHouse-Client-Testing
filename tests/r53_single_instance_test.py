#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.cpp").read_text()

assert "#include <QLockFile>" in main
assert "acquireSingleInstanceGuard" in main
assert "lock->tryLock(0)" in main
assert "QLockFile::LockFailedError" in main
assert "Only one instance can run at a time" in main

# Both CLI and GUI paths must acquire the exact same guard after command-line
# parsing, so --help/--version remain usable while a normal second session is blocked.
assert main.count("SingleInstanceGuard instanceGuard = acquireSingleInstanceGuard();") == 2
assert main.count("parser.process(app);") == 2
assert main.index("parser.process(app);") < main.index("SingleInstanceGuard instanceGuard = acquireSingleInstanceGuard();")

print("PASS: WaffleHouse-Client uses a shared cross-frontend single-instance QLockFile guard")
