# WaffleHouse-Client 5.2r3 validation

Validation performed on the source tree in this bundle:

- `tests/wafflehouse52_modular_test.py`: **PASS — 29/29 modular/no-mail checks**.
- `tests/ssh_interactive_pty_test.py`: **PASS — 17/17 SSH PTY/profile/authentication checks**.
- `tests/wafflehouse52r3_branding_gui_test.py`: **PASS — 181/181 branding/GUI/companion checks**.
- Full source regression suite: **PASS — 25/25 test files**.
- `tests/main_contextual_actions_r3_test.py`: **PASS — 22/22 checks**.
- `tests/r52r3_warning_cleanup_test.py`: **PASS — 8/8 compiler-warning cleanup checks**.
- Python syntax: `scripts/wafflehouse-ssh` and Python tests: **PASS** (`python3 -m py_compile`).
- Shell syntax: top-level and Linux/FreeBSD, macOS, Windows/MSYS2 and Termux builders: **PASS**.
- CMake parsing reaches Qt dependency discovery successfully.
- A native Qt compile was **not** performed in the packaging sandbox because the environment does not contain the Qt 6 development package/configuration (`Qt6Config.cmake`).

## 5.2r3 compiler-warning cleanup

- SSH child error reporting and cooperative shutdown no longer discard fortified `write(2)` return values; `EINTR` is retried through a small best-effort helper.
- Gopher and Gemini ready-read loops use explicit braces, removing misleading-indentation diagnostics while preserving the final `readAll()` drain.
- `shellQuote()` is compiled only on macOS, where it is used by the Mosh terminal launcher.
- The exact warning patterns reported from GCC are absent from the 5.2r3 source tree.

## 5.2r3 branding / modular GUI

- No legacy fork product branding remains in the source/documentation bundle.
- The old `RetroServicesWindow` source/class naming is removed; the auxiliary module UI is `NetworkToolsWindow`.
- XMPP internal transaction identifiers and fallback nickname use WaffleHouse naming.
- Main GUI header is application name + version only; the complete protocol list is not rendered under the application name.
- Startup splash is similarly reduced to WaffleHouse-Client + version.
- Main protocol/module launcher is removed; the main window is account-centric with Add/Remove/Edit/Connect-Disconnect controls.
- Main-window Run input and Tools Command Palette are removed; former GUI command entry points are exposed through contextual menus and protocol workspaces.
- Help -> Runtime Environment provides runtime/session details without a generic command surface.
- Main window defaults to 720x540 with a 580x430 minimum and remains user-resizable.

## SSH

- GUI raw-terminal keyboard routing includes both Telnet and SSH.
- Unix SSH backend uses `forkpty(3)` rather than a plain `QProcess` pipe.
- OpenSSH stdin/stdout/stderr and `/dev/tty` prompts are attached to the embedded PTY.
- `BatchMode=no` and `NumberOfPasswordPrompts=3` keep password and keyboard-interactive authentication prompts enabled.
- SSH terminal resizes propagate via `TIOCSWINSZ`.
- PTY child processes are reaped on disconnect/close.
- Saved SSH profiles can specify an identity file, OpenSSH `-o` overrides, and an optional remote command.
- `scripts/wafflehouse-ssh` imports the useful media/SIP reverse stream-local forwarding design from the earlier SSH Companion and is installed on Unix when SSH is compiled.

Platform runtime testing is still required on Linux/FreeBSD/macOS/Windows/Termux before calling the new XMPP, SSH, NNTP, Gopher, Gemini and Mosh modules production-ready.
