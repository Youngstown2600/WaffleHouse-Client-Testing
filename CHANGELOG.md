# WaffleHouse-Client 5.5 — Remote WaffleCast broadcasting

- Added saved Media Center **Broadcast Settings** for WaffleCast.
- Added separate local listen TCP port and advertised public/WAN TCP port.
- Added persistent public IP/DDNS configuration for AIM invites and public listen links.
- LAN discovery now prefers the UDP sender/local TCP endpoint, avoiding NAT hairpin requirements when public advertising is enabled.
- Manual remote Connect / Listen preserves the exact public host/port used to reach `/.well-known/wafflecast` and resolves the current tokenized stream path automatically.
- Existing public browser, VLC/mpv, M3U/PLS, album-art, Stop/Play, Pause/Play, and single-instance behavior retained.

# WaffleHouse-Client 5.x Changelog

## 5.3 single-instance hotfix r4

- WaffleHouse-Client is now single-instance per logged-in user. A second GUI or CLI launch detects the existing process and exits cleanly instead of starting another client session.
- The single-instance lock is shared by GUI and CLI frontends and is acquired only after command-line parsing, so `--help` and `--version` remain available while WaffleHouse is already running.
- Retains the r3 Pause -> Play repair and the r2 Stop -> Play queue-rebuild repair.

## 5.3 media transport hotfix r3

- Fixed Pause -> Play: the GUI now distinguishes a paused transport from a stopped/idle transport. Pressing Play while paused clears mpv's `pause` property instead of re-selecting the current playlist index.
- Pause/resume state is updated locally as soon as the mpv IPC property command is queued successfully, eliminating fast-click races with asynchronous property notifications.
- Explicit track selection while paused now clears pause first, so double-clicking another song starts it instead of leaving it silently paused.
- Routed pause toggles through the same state-aware pause/resume methods rather than a raw `cycle pause` command.
- Added `r53_media_pause_resume_test.py` while retaining the r2 Stop -> Play queue rebuild fix.

## 5.3 media transport hotfix r2

- Reworked Media Center Stop -> Play around an application-owned queue. Stop now clears mpv's transport queue completely while preserving WaffleHouse's library, and Play reconstructs mpv directly with `loadfile` commands.
- Removed Stop -> Play dependence on `keep-playlist`, `loadlist`, and a follow-up `playlist-play-index` restart command.
- Prevented empty and partial mpv playlist snapshots during Stop/rebuild from shrinking the persistent library.
- Kept the GUI's current playlist row synchronized with mpv as tracks advance, so the visible selected/current track remains a reliable restart target.
- Expanded `r53_media_stop_resume_test.py` regression coverage for the direct rebuild path.

# WaffleHouse-Client 5.3

## macOS bootstrap and deployment hardening

- Added full-Xcode preflight/configuration and Ventura archived-Xcode guidance before Homebrew dependency work.
- Added official Homebrew bootstrap when `brew` is absent.
- Replaced the required Homebrew `qt` meta-package with the narrower `qtbase` + `qtmultimedia` + `qttools` set used by the application.
- Added explicit Cocoa QPA plugin discovery/bundling and application-local `qt.conf`.
- Expanded `macdeployqt` library search paths to transitive Homebrew dependencies, preventing unresolved Brotli/WebP/Graphite2-style rpaths from being ignored.
- Made deployment errors fatal; a broken `.app` is no longer reported as a successful build or installed.
- Added a Mach-O dependency audit for missing `@rpath` targets and machine-local Homebrew/source paths before signing.
- Moved local ad-hoc signing until after deployment, then verifies the finished bundle and runs a packaged `--version` smoke test.
- Added default standalone DMG creation with an Applications shortcut.
- Added `--bootstrap-only` and `--no-dmg` macOS builder controls.
- Promoted application version to 5.3 / CMake 5.3.0 while retaining all 5.2r3 Update Merger features.

## 2026-09-06 — Update Merger r3

- Hardened embedded SSH interactive authentication: Unix PTY sessions now explicitly use `BatchMode=no` and permit normal password/host-key/MFA prompts in the terminal.
- Removed the Tools -> Command Palette entry and Ctrl+Shift+P GUI command surface.
- Removed the unused main-window GUI slash-command dispatcher after the r2 command field was retired.
- Exposed runtime details through Help -> Runtime Environment and retained all other former palette operations in their natural account, buddy, Softphone, Media, transfer, options, and Help locations.
- Client Capabilities now advertises contextual GUI actions instead of a command palette.
- Conversation-local protocol slash commands remain available where they naturally belong.

## 2026-09-06 — Update Merger r2

- Removed the main-window protocol launcher grid.
- Replaced the main-window Command/Run row with Add, Remove, Edit, and state-aware Connect/Disconnect account controls.
- Connect/Disconnect now acts on the highlighted account and changes label according to connection state.
- Standardized the account tree to Account/Buddy List and Protocol / Status with protocol-first status text.

# WaffleHouse-Client 5.2r3

## 5.2r3 compiler-warning cleanup

- SSH PTY error/shutdown writes now inspect `write(2)` results and retry `EINTR`, eliminating fortified-libc `-Wunused-result` warnings.
- Gopher/Gemini receive loops now use explicit braces, eliminating `-Wmisleading-indentation` warnings without changing the final drain behavior.
- The macOS-only `shellQuote()` helper is compiled only on macOS, eliminating the Linux `-Wunused-function` warning.
- No protocol behavior or modular-GUI feature selection was removed by this cleanup.


## Branding / modular GUI cleanup

- Removed the remaining legacy fork-era internal names from the expanded module implementation; the auxiliary window is now `NetworkToolsWindow` and XMPP transaction IDs/default resource labels use WaffleHouse naming.
- Simplified the main GUI header and startup splash to WaffleHouse-Client plus the version only; the full protocol list is no longer rendered underneath the product name.
- Changed the module launcher from a single horizontal row to a compact wrapping grid so an all-module build does not force an oversized main window.
- Adjusted the default main window to 720x540 with a 580x430 minimum while keeping it freely resizable.
- Imported the WaffleHouse SSH Companion launcher as `scripts/wafflehouse-ssh`, updated for 5.2r3, and wired it into Unix installation when SSH support is compiled.
- Added per-profile SSH identity-file, OpenSSH `-o` override, and optional remote-command fields to the GUI; these settings persist in the shared GUI/CLI profile store and are passed to the PTY-backed OpenSSH process without shell interpolation.
- Retains the 5.2r1 PTY-backed Unix SSH terminal fix.

## 5.2r1

## SSH interactive-terminal hotfix

- Fixed the GUI SSH terminal input path: raw keyboard bytes are now forwarded to both Telnet and SSH sessions instead of Telnet only.
- Replaced the Unix SSH `QProcess` pipe with a real pseudo-terminal (`forkpty`) so OpenSSH password, host-key confirmation, shell line discipline, and interactive programs remain inside the WaffleHouse terminal window instead of leaking to the terminal that launched WaffleHouse.
- Added PTY resize propagation with `TIOCSWINSZ`, so SSH receives the embedded terminal's current rows/columns.
- Added clean PTY child reaping/termination to avoid orphaned SSH processes.
- Updated the terminal hint to identify SSH sessions as interactive PTY terminals.
- Windows keeps the existing QProcess fallback until native ConPTY support is added; the 5.2r1 PTY fix targets Linux, FreeBSD, and macOS.

# WaffleHouse-Client changelog

## 5.2

- Promoted the modular communications branch into the WaffleHouse-Client line on top of 5.1r4.
- Added compile-time modular GUI/module matrix. Disabled modules no longer create blank or dead GUI areas.
- Added XMPP/Jabber backend with TLS/STARTTLS, SASL PLAIN-over-TLS, roster, chat, presence and basic MUC.
- Added SSH terminal backend using OpenSSH.
- Added NNTP/Usenet connection, group, article/raw-command and posting support.
- Added modular Gopher, Gemini and Mosh tools.
- Removed the experimental IMAP/SMTP mail module from the 5.2 feature set.
- Extended Linux/FreeBSD/macOS, Windows/MSYS2 and Termux builders with module selection.
- Restored the WaffleHouse-Client application identity, executable, icons, desktop integration and 5.1-compatible data paths while retaining WaffleHouse/CPX wire compatibility.
- Preserved the inherited AIM/OSCAR, IRC, Telnet/BBS, SIP/VoIP, media, secure room/DM, transfer and diagnostic functionality from 5.1r4.

---

## Inherited WaffleHouse-Client changelog

# WaffleHouse-Client 5.1r4

## 5.1r4 persistent Media Library + release cleanup

- Media Center queue is now persisted per user in `media/library.json` and mirrored to `media/queue.m3u8`; launching WaffleHouse restores the playlist without automatically starting playback.
- Local `.pls`, `.m3u`, `.m3u8`, and `.xspf` playlist definitions are copied into the internal `media/playlists/` cache while the expanded stream queue is stored independently of the original file.
- Playlist add/remove/clear/import operations, current item, volume/mute, shuffle, and repeat state are saved atomically and shared by the GUI/CLI MediaController.
- Added a Media Center **Library Folder** shortcut and XSPF import filter; renamed the destructive queue action to **Clear Library** to make persistence explicit.
- Release bundle cleanup removes old versioned validation receipts, the one-off r20 merge audit, and obsolete historical platform-comparison artifact. The bundle now carries a single current `VALIDATION.md` plus the actual regression tests.
- Retains all 5.1r3 account-label/softphone changes, 5.1r2 uninstall lifecycle, and the complete 5.0r20 OSCAR/file-transfer fix set.

## 5.1r3 AIM/IRC labels + softphone control cleanup

- Added an optional local **Account label** for AIM/OSCAR and IRC profiles. Labels are persisted in the shared GUI/CLI settings model and can identify accounts as `NINA`, `Waffle BBS`, `Work IRC`, etc. without changing the actual AIM screen name or IRC nickname used on the wire.
- Account labels are used in the GUI account tree/context menus/connections list and in CLI connection labels; the real protocol identity remains available in tooltips/status details.
- Hardened dial-pad geometry so all twelve keys use identical fixed dimensions and uniform grid rows/columns across Qt platform styles. One-line keys (`1`, `*`, `#`) can no longer collapse differently from two-line lettered keys.
- Compacted Active Calls controls into two four-button primary rows plus DTMF and Diagnostics utility rows. Transfer buttons use compact visible labels with full action-name tooltips.
- Retains the complete 5.1 + 5.0r20 OSCAR/file-transfer maintenance merge and all 5.1r2 uninstall/remove behavior.

## 5.1r2 builder uninstall/remove lifecycle

- Added an interactive **Build / Install / Upgrade** versus **Uninstall / Remove** choice before protocol selection or dependency checks.
- Added direct uninstall/remove flags for Linux/FreeBSD, macOS, Termux, and the portable Windows package.
- Removal deletes installed application payloads while preserving per-user settings, accounts, history, and logs.

## 5.1r1 softphone GUI layout repair

- Reworked the Phone workspace so the destination field has a dedicated full-width row instead of being squeezed beside the runtime prefix.
- Capped the READY / call-state banner height so it cannot consume the dialer vertically.
- Enlarged the keypad and primary CALL / HANG UP controls for desktop use.
- Rebalanced the Phone splitter so the dialer remains usable while Active Calls stays flexible.
- Reflowed live-call controls so long transfer actions are not clipped.
- Preserves the full 5.1 + 5.0r20 OSCAR/file-transfer maintenance merge and all 5.1 platform/NINA work.

## 5.1

- Re-merged and regression-verified every WaffleHouse-Client 5.0r20 maintenance fix into the 5.1 all-platform tree after confirming the original 5.1 branch had been promoted from 5.0r18 rather than r20.
- Restored the printable unsecured-transfer `[[WHFILE2:...]]` envelope and receive compatibility with both intact and separator-stripped legacy `WHFILE1` frames, preventing macOS/Qt/AIM normalization from exposing transfer control payloads as chat text.
- Restored asynchronous OSCAR LOCATE Away/Idle hydration, Query2-to-classic fallback, online-buddy tracking, and conservative 60-second presence refresh so revival/private servers can supply authoritative away text and idle time on every supported platform.
- Restored GUI presence-cache merge semantics and manual AIM User Info hydration so refreshed Away/Idle state does not discard capability/sign-on metadata and offline events cannot retain stale presence.
- Restored the two 5.0r20 regression gates and verified them alongside all 5.1 NINA/platform regressions.
- Promoted the 5.0r18 desktop tree to WaffleHouse-Client 5.1.
- Imported the known-good NinaIM 0.2/0.3 NINAPatcher-compatible AIM/OSCAR sign-on path.
- Added AIM network profiles (`auto`, `nina`, `custom`) to GUI and CLI saved accounts.
- NINA is auto-detected for `*.nina.chat` hosts and can also be explicitly selected.
- NINA BOSS bootstrap now supports client-sign-on-first startup, includes multi-connection TLV `0x004A`, accepts 16/32-bit BUCP challenge lengths, advertises only implemented OSCAR families, uses stock-AIM family/tool versions, and tolerates non-echoed HOST_VERSIONS/rate request IDs.
- BOS redirect overrides no longer rewrite secondary OSCAR ChatNav/Chat/Admin/BART/etc. service redirects.
- Added Windows 10/11 MSYS2/UCRT64 build support and GUI/CLI launchers.
- Added Termux/Android native build support and GUI/CLI launchers; GUI requires Termux:X11 at runtime.
- Linux/Unix, FreeBSD, macOS, Windows, and Termux share the exact same OSCAR backend compatibility implementation.

## 5.0r20

- Replaced the unsecured file-transfer wire wrapper's leading ASCII Record Separator (`0x1E`) with a printable `[[WHFILE2:...]]` envelope so macOS/Qt/AIM normalization cannot turn internal transfer frames into visible garbage text.
- Kept receive compatibility with exact legacy `WHFILE1` frames and with legacy frames whose leading separator was stripped in transit.
- Added asynchronous OSCAR LOCATE presence hydration after buddy arrival/status updates so Away message/state and Idle minutes are recovered when a revival/private BOS provides an incomplete BUDDY UserInfo block.
- Added a conservative 60-second refresh for online AIM buddies, Query2-to-classic LOCATE fallback, and merge semantics that preserve richer native buddy metadata.

## 5.0r18

- Fix AIM/OSCAR account context-menu **Join AIM Chat…** being incorrectly disabled when BOS does not advertise CHAT_NAV (0x000D) and CHAT (0x000E) directly.
- Treat OSCAR ChatNav/Chat correctly as on-demand redirect services obtained through OSERVICE SERVICE_REQUEST (0x0001/0x0004); the backend already performs those redirects when joining a room.
- Keep runtime failure honest: if a server truly does not provide ChatNav/Chat, the join attempt now reaches the backend and reports the OSCAR service error instead of being preemptively grayed out.
- Clarify the OSCAR Feature Center text so BOS-advertised foodgroups are not confused with separately redirected services.
- Retains the 5.0r17 persistent OSCAR audit viewer and /bin launcher prompt.

## 5.0r17

- Added persistent per-account OSCAR Login Audit / Full Wire Trace files using the same credential-redacted diagnostic stream already shown in Activity.
- Added **Tools → View OSCAR Audit Log…** and an OSCAR account context-menu entry; multiple AIM accounts can be selected and the viewer displays the exact log path.
- Full OSCAR logs rotate at 5 MiB with one `.old` generation; the GUI tails the last 512 KiB for large logs.
- Linux/FreeBSD builders explicitly ask after a successful build whether to install WaffleHouse-Client system-wide and add `/bin/wafflehouse-client`; Enter/default remains No.
- `/bin/wafflehouse-client` is a launcher for the normal prefix-installed executable, keeping shared desktop/icon/resource installation in the correct prefix instead of scattering assets under `/share`.
- Retains the 5.0r16 BuildFeatures compile fix, 5.0r15 IM composer-focus fix, and 5.0r14 native OSCAR presence/profile hardening.

## 5.0r15

- Fixed new IM/chat windows opening with keyboard focus in the read-only transcript instead of the message composer.
- Transcript uses mouse-only focus for selection/copying; first-show focus is queued onto the message input after window activation.
- Added a regression gate for IM composer focus behavior.

## 5.0r14

- Fixed native AIM/OSCAR buddy presence decoding: BUDDY arrival/departure packets now retain away/unavailable, DND, N/A, busy, free-for-chat, invisible, and idle state instead of collapsing every connected buddy to plain Online.
- Buddy-list GUI and CLI now display native OSCAR presence and idle information; Locate user-info refreshes can also supply the buddy away message.
- Buddy capability UUIDs received in native arrival packets are retained using OSCAR's present/absent TLV semantics.
- Added Buddy rights negotiation for initial departure notifications without falsely advertising unsupported BART/icon support.
- Locally saved AIM profiles are persisted in the account and replayed after BOS login, working around revival/private servers that keep LOCATE profile data only for a session.
- Native AIM chat capability is now advertised because WaffleHouse implements OSCAR chat rooms; unsupported legacy Talk/Direct-IM/File/BART capabilities remain detection-only and are not falsely advertised.
- Added a native OSCAR feature regression audit covering idle, away/back, profile TLVs/replay, Query2 user info, rich buddy presence, SSI/auth, privacy, chat, typing/stored messages, account admin, and capability gating.
- BUDDY arrival/departure notifications now consume every batched UserInfo record instead of stopping after the first buddy.
- Added native host-event handling for OSERVICE own-user-info updates, OSCAR warning/evil-level changes, and BUDDY rejected-watch notifications.

## 5.0r13

- Builder feature selection: AIM/OSCAR, IRC, Telnet/BBS, SIP/VoIP, and Media can be selected per build.
- GUI/CLI hide and reject protocols omitted by the selected build profile.
- Telnet/BBS profiles store exact rows/columns and auto-fit the GUI font; CLI requests a matching outer terminal size and advertises exact NAWS dimensions.
- SIP multi-account behavior is retained: multiple registrations can stay active concurrently against different PBXs, including Asterisk chan_pjsip and legacy chan_sip compatibility profiles.
- AIM/OSCAR idle handling now reports real idle seconds through native OSCAR OSERVICE idle signaling; WaffleHouse no longer invents client-side auto-away thresholds/messages.
- OSCAR login/full wire debug and credential redaction from 5.0r12 are preserved.

## 5.0r12

- Added AIM/OSCAR diagnostic levels: **Off**, **Login Audit**, and **Full Wire Trace**.
- Login Audit traces TCP/FLAP/BUCP/BOS bootstrap, server families, version/rate negotiation, redirects, and decoded authorization failures.
- Full Wire Trace records every OSCAR FLAP/SNAC header and payload while redacting passwords, password hashes, challenge material, and auth/service cookies.
- Legacy FLAP channel-4 authorization failures are decoded instead of collapsing to a generic `server signed off` message.
- Confirmed and hardened native OSCAR idle signaling via `OSERVICE__IDLE_NOTIFICATION` SNAC `0x0001/0x0011` with a 32-bit idle-seconds payload; zero advertises active.
- Added explicit native-idle audit lines and OSCAR debug settings to GUI and CLI account editors.

## 5.0r11

- macOS: fixed post-link `macdeployqt` deployment with Homebrew's split Qt 6 module kegs by supplying module `-libpath` directories, including QtSvg and the QtPdf-containing qtwebengine keg.
- SIP account setup now visibly asks which remote server type is in use: Auto/generic SIP, Asterisk PJSIP (`chan_pjsip`), or legacy Asterisk `chan_sip`.
- CLI accepts the friendly aliases `pjsip`, `chan_pjsip`, and `chan_sip`. WaffleHouse continues to use PJSIP 2.17 internally in every mode.

# WaffleHouse-Client 5.x Changelog

## 5.3 media transport hotfix r3

- Fixed Pause -> Play: the GUI now distinguishes a paused transport from a stopped/idle transport. Pressing Play while paused clears mpv's `pause` property instead of re-selecting the current playlist index.
- Pause/resume state is updated locally as soon as the mpv IPC property command is queued successfully, eliminating fast-click races with asynchronous property notifications.
- Explicit track selection while paused now clears pause first, so double-clicking another song starts it instead of leaving it silently paused.
- Routed pause toggles through the same state-aware pause/resume methods rather than a raw `cycle pause` command.
- Added `r53_media_pause_resume_test.py` while retaining the r2 Stop -> Play queue rebuild fix.

## 5.0r11

- Fixed macOS PJSIP static-link handling so pkg-config framework pairs such as `-framework CoreServices` are passed to AppleClang as frameworks instead of being misinterpreted as `-lCoreServices`.
- Framework normalization is generic for every framework advertised by PJSIP, avoiding the same failure if another macOS framework appears in the static link metadata.
- Removed the unused `this` capture in the Search History clear-button lambda reported by AppleClang.
- Retains all 5.0r9 Asterisk `chan_sip` compatibility behavior unchanged.

## 5.0r9

- Rebuilt from the user-supplied 5.0r8 desktop bundle as the source-of-truth baseline.
- Added legacy Asterisk `chan_sip` server compatibility mode while retaining PJSIP 2.17 as WaffleHouse-Client's client-side SIP stack.
- Added `auto`, `standard`, and `asterisk-chan_sip` compatibility choices to GUI and CLI SIP account editors.
- `asterisk-chan_sip` mode disables RFC 5626 SIP-Outbound behavior and enables legacy-safe Contact/Via rewriting for older Asterisk registrars.
- SIP account identity now honors the configured Caller-ID domain independently of the registrar/domain, useful for older PBXs and trunks.
- Digest authentication continues to follow the registrar-provided realm using a wildcard realm credential.
- Added regression coverage for the chan_sip compatibility path.

## 5.0r8
- Fixed the macOS builder so optional media tools cannot block compilation of the client. `mpv` and `ffmpeg` are now runtime-only optional dependencies; failure to install either one is non-fatal.
- Removed the stale macOS `yt-dlp` build dependency; the 5.0r8 media path does not use the removed YouTube resolver.
- Added `--with-media-deps` for users who explicitly want the builder to attempt Homebrew installation of `mpv`/`ffmpeg`. A Homebrew “no bottle available” error now produces a warning and the WaffleHouse build continues.
- macOS runtime discovery now checks Homebrew, MacPorts, Fink, and `/Applications/mpv.app` locations so Finder-launched `.app` bundles can find `mpv`/`ffmpeg` even when those directories are absent from the GUI process PATH.

## 5.0r7
- Combined the Linux/FreeBSD and macOS desktop releases into one shared source bundle. The top-level builder now asks the user to select Linux, FreeBSD/Unix, or macOS before any platform build/dependency work begins.
- Normal builds on every desktop platform ask before installing WaffleHouse-Client into a system bin directory; Enter/default remains No.
- Synchronized macOS from the 5.0r3 platform package to the complete 5.0r6 application source, bringing over the 5.0r4 typing-window fix, 5.0r5 OSCAR transfer/window/rate fixes, and 5.0r6 secure-direct finalization/profile-ID fixes.
- Fixed the macOS status/menu-bar icon white-on-white regression by explicitly keeping the WaffleHouse icon non-template/full-color and removing the QSystemTrayIcon synchronously during application quit.
- Bundled built-in notification sounds inside the macOS `.app` so an installed/moved app does not depend on the extracted source directory.

## 5.0r6
- Fixed secure CPX direct downloads that reached 100% but remained as `*.cpxpart` while the receiver stayed on “Receiving direct” and the sender stayed on “Verifying”.
- Corrected GUI file-transfer ownership lookup: transfer records store the stable connection profile ID, but several completion/cancel/resume/direct callbacks incorrectly looked it up as a transient backend ID.
- Direct receive completion now always performs local SHA-256 verification and final rename even if the account/control-channel object becomes temporarily unavailable; peer completion confirmation is sent when the owning account is available.
- Hardened the encrypted direct socket sender so a queued final frame can drain and close proactively instead of depending on the receiver to close first.
- The same profile-ID lookup correction also restores GUI cancel, resume, decline, direct-progress peer naming, and direct-fallback handling for file transfers.

## 5.0r5
- Fixed AIM/OSCAR Send File from the buddy list so launching a transfer does not create an IM window.
- File-transfer OFFER/ACCEPT/DATA/ACK/DONE/COMPLETE control traffic is now consumed as transport traffic and does not create GUI conversations.
- Secure file-transfer payloads are recognized before an IM window is created, preventing the same pop-up behavior on encrypted transfers.
- Corrected OSCAR rate-class reply parsing (supports both 30-byte and 35-byte layouts) and tuned relay transfers to use larger payloads at a roughly two-second cadence, reducing ICBM rate-limit pressure that could stall downloads after they started.
- Extended reliable-transfer ACK retry tolerance and kept Send File available from an online IM even without an active secure session.

## 5.0r4
- Fixed OSCAR typing notifications so they stay in the normal AIM IM window instead of opening a separate transient "typing" conversation window.
- The existing IM header now remains the single typing-state indicator (typing, paused, or cleared).

## 5.0r3
- Pruned the release archive to platform-required source, assets, builders, and current documentation only.
- Removed other-platform adapters/builders, historical 3.x release artifacts, examples, companion payloads, and internal regression-test trees from the customer-facing archive.
- Tailored CMake configuration specifically to the Unix/Linux bundle so it no longer exposes dead build branches for other platforms.

## 5.0r2
- Added conventional GUI application exit: File → Exit on Linux/FreeBSD with Ctrl+Q.
- Uses the existing clean shutdown path; tray-close behavior remains unchanged.

## 5.0r1
- Redesigned the Softphone around a combined Phone workspace.
- Added a dedicated SIP Accounts left-navigation tab.
- Exposed Answer, Reject, Hang Up, Hold, Resume, Mute, Blind Transfer, Attended Transfer, DTMF, and Diagnostics in the GUI.
- Widened the Softphone navigation rail to prevent clipped labels.
- Cleaned GCC OSCAR initializer and misleading-indentation warnings.

## 5.0
- Introduced the 5.x unified desktop application/core design, unified contacts/history/capabilities, expanded SIP diagnostics and transfer controls, and command-palette support while preserving AIM/OSCAR, IRC, Telnet/BBS, media, secure rooms, encrypted transfers, themes, and CLI/GUI operation.
