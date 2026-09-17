# WaffleHouse-Client 5.2r3 Update Merger r3

Update date: 2026-09-06

## Contextual GUI command integration

- Removed **Tools -> Command Palette** and its `Ctrl+Shift+P` shortcut.
- Removed the now-unused main-window GUI slash-command dispatcher instead of leaving a hidden alternate command interface behind.
- Kept protocol-native slash commands inside conversation/session windows where command syntax is part of the protocol workflow (for example IRC room commands and secure-session aliases).
- Mapped the former palette entry points to their normal GUI homes:
  - client features -> **Tools -> Client Capabilities**;
  - contacts -> **Tools -> Unified Contacts**;
  - history -> **Tools -> Search History**;
  - SIP calls/diagnostics -> **Tools -> Open Softphone** and its Active Calls controls;
  - new IM / room join -> selected account and buddy context menus;
  - transfers -> **Tools -> File Transfer Log / Activity**;
  - notification configuration -> **Tools -> Options**;
  - runtime details -> **Help -> Runtime Environment**;
  - help/version information -> **Help** / **About**.
- Advanced AIM/OSCAR actions remain in the selected AIM account/buddy context menus; raw protocol access remains an explicit advanced Tools action.
- Media commands remain in the **Media** menu and Media Center rather than a generic command interface.
- Client Capabilities now reports **Contextual GUI Actions** instead of a Command Palette capability.

## Main-window workflow retained from r2

- No protocol/module launcher button grid.
- No Command input field or Run button.
- Account action row remains **Add**, **Remove**, **Edit**, **Connect / Disconnect**.
- Connect / Disconnect follows the highlighted account state.
- Main tree remains **Account/Buddy List** with **Protocol / Status**.

## Validation

- Added `tests/main_contextual_actions_r3_test.py` to guard removal of the generic GUI command surface and verify its former entry points are available through contextual GUI controls.
- Full inherited + r3 regression suite: **PASS — 25/25 test files**.
- CMake configuration reaches Qt dependency discovery; this sandbox does not contain `Qt6Config.cmake`, so native Qt/PJSIP linking was not performed here.

## SSH interactive authentication hardening

- Unix PTY SSH sessions explicitly use `BatchMode=no` and `NumberOfPasswordPrompts=3`, so password, host-key and keyboard-interactive/MFA prompts remain interactive inside the embedded terminal.
