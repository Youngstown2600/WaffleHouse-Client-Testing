WaffleHouse-Client 5.4 alpha1 — All-Platform Source Bundle

5.4 alpha1: adds WaffleCast direct multi-listener Media Center broadcasting, native AIM/IRC `/wafflecast` invitations, transient listener playback that preserves the local library, live track/cover metadata, and a new embedded Album Art panel. See WAFFLECAST.md and RELEASE-5.4-alpha1.md.
Platforms: Linux, FreeBSD/Unix, macOS, Windows, Termux/Android
Build: ./build.sh

5.3 r4 single-instance hotfix: WaffleHouse-Client now allows only one running session per logged-in user. GUI and CLI share the same process lock, so a second launch exits cleanly rather than opening another client. This bundle also retains the r3 Pause -> Play and r2 Stop -> Play Media Center repairs.

5.3 macOS bootstrap/package release: the macOS builder now detects and configures full Xcode, can guide the authenticated Apple archived-Xcode download on Ventura, installs Homebrew when missing, installs the narrowed Qt/toolchain dependency set, builds managed PJSIP, deploys Qt and the Cocoa platform plugin into the app, fails closed on unresolved macdeployqt errors, ad-hoc signs/verifies local builds, smoke-tests the packaged executable, and creates a standalone DMG by default.

The 5.3 release retains the 5.2r3 Update Merger UI and protocol behavior: the desktop is account-centric; the protocol launcher grid, main-window Run field, and Tools Command Palette are removed. Global actions live in normal menus; protocol/account/user actions live in account and buddy context menus; SIP controls live in the Softphone; media and transfer controls remain in their dedicated workspaces. Help -> Runtime Environment exposes runtime details.

NINA NETWORK: WaffleHouse-Client retains the known-good NinaIM/NINAPatcher-compatible OSCAR handshake in the shared GUI/CLI backend.

MODULES: XMPP/Jabber, SSH, NNTP/Usenet, Gopher, Gemini and Mosh remain alongside AIM/OSCAR, IRC, Telnet/BBS/MUD, SIP/VoIP and Media. The GUI is feature-driven and IMAP/SMTP is intentionally not part of this release.

INSTALLATION: successful normal builds ask before system installation. On macOS the verified .app is self-contained; the DMG is intended for normal end users and does not require Homebrew, Xcode, CMake, or a separate Qt installation on the destination Mac.

See RELEASE-5.3.md and BUILDING-macOS.md for the 5.3 release details.
