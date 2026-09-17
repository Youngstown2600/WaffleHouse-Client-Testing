# WaffleHouse-Client 5.5

## 5.5 — Remote/Public WaffleCast behind NAT

Media Center now includes **Broadcast Settings** for Internet WaffleCast hosting. Save a public IP or DDNS hostname, a local listen TCP port, and an independent public/WAN TCP port. This supports router/pfSense port forwarding such as `WAN TCP 9119 -> broadcaster TCP 8173`. AIM WaffleCast invites and Copy Public Link advertise the public endpoint automatically, while LAN discovery continues to use the broadcaster's local address directly. Remote WaffleHouse listeners may enter only `http://PUBLIC-HOST:PORT`; the client resolves the active tokenized session automatically.

## 5.5 — WaffleCast can be joined directly

Media Center now has **Connect / Listen** for pasting a WaffleCast URL and **Copy Public Link** while hosting. The public link works in a browser without WaffleHouse and exposes standard MP3, M3U, and PLS listen links for VLC/mpv and other compatible players. AIM/IRC native invites still work.


## 5.5 — WaffleCast + embedded album art

WaffleHouse-Client 5.5 adds **WaffleCast**, a native shared Media Center session for WaffleHouse users. Start a broadcast in Media Center, then let another WaffleHouse client discover the broadcast on the LAN or connect to the host/port directly. AIM/IRC invite frames remain compatible, and listeners play the host's direct live stream without replacing their persistent local playlist.

The Media Center also adds a dedicated **Album Art** panel. Embedded cover art from MP3/M4A/FLAC and related audio files is extracted with ffmpeg, displayed locally, and exposed to WaffleCast listeners as image bytes only. Track title and artwork update while the DJ moves through the playlist. See `WAFFLECAST.md` and `RELEASE-5.5.md`.


## 5.3 macOS bootstrap + standalone deployment

WaffleHouse-Client 5.3 carries forward the complete 5.2r3 Update Merger tree and rebuilds the macOS installation path around a self-contained application bundle. The macOS builder can configure full Xcode, bootstrap Homebrew, install the narrowed Qt/toolchain dependencies, package the Cocoa platform plugin, fail on unresolved `macdeployqt` errors, audit Mach-O dependency paths, ad-hoc sign/verify local builds, smoke-test the packaged executable, and create a drag-to-Applications DMG by default. See `RELEASE-5.3.md` and `BUILDING-macOS.md`.

For a macOS environment-only preflight use `./build.sh --os macos --bootstrap-only`; a normal clean macOS build is `./build.sh --os macos --clean`.


### 5.2r3 compiler-warning cleanup

The 5.2r3 maintenance build cleans the GCC/fortified-libc warnings in the Unix SSH PTY backend and Network Tools Gopher/Gemini code while preserving behavior.

WaffleHouse-Client 5.2 is the modular evolution of the 5.1r4 codebase, incorporating the expanded modular protocol work developed after 5.1r4. The goal is one client for classic and modern text/voice/network services without forcing every user to carry every module.



### 5.2r3 branding / GUI / SSH companion refresh

- Removed the remaining legacy fork-era internal naming from the expanded module code. The auxiliary window is now `NetworkToolsWindow`, XMPP request identifiers use the WaffleHouse namespace, and user-facing descriptions consistently identify the program as WaffleHouse-Client.
- Simplified the desktop branding area to **WaffleHouse-Client + version only**. The splash screen and main header no longer print the complete protocol/module list underneath the application name.
- Reworked the modular launcher into a compact four-column wrapping grid. All-protocol builds therefore stay at a normal desktop size instead of forcing one long horizontal row, while minimal builds still show only the modules compiled in.
- Added the updated `scripts/wafflehouse-ssh` companion from the earlier SSH Companion design. It can establish the reverse Unix-socket media/SIP bridges used by `wafflehouse-shell` while normal GUI SSH sessions continue to use the 5.2r1 PTY backend.

### 5.2r1 SSH hotfix

On Linux, FreeBSD, and macOS, SSH sessions now run behind a real pseudo-terminal. This keeps host-key questions, password prompts, interactive shells, and terminal applications inside the WaffleHouse SSH window. It also fixes the 5.2 GUI bug that accepted SSH keystrokes visually but only forwarded raw terminal input to Telnet backends.

## The modular GUI rule

The interface is built from the features compiled into the binary. If a build contains only IRC and Telnet/BBS, the launcher presents only IRC and Telnet/BBS. SIP, media, XMPP, SSH, Usenet and auxiliary-tool controls are not left behind as empty panels or disabled placeholders. The launcher wraps into a compact grid so enabling every module does not force the main window to an excessive width.

The same feature matrix is used by the connection wizard, module launcher, About screen and Network Tools window.

## Modules

### Core account/session modules

- **AIM / OSCAR** — inherited 5.1r4 OSCAR engine: IM, chat rooms, SSI/buddies, profiles/away/idle, diagnostics, CPX secure messaging, file transfer and OSCAR voice extensions.
- **IRC** — channels/PMs, member tracking, TLS, CPX secure PMs, file transfer and existing WaffleHouse IRC behavior.
- **Telnet / BBS / MUD** — ANSI terminal, BBS/MUD sessions and automatic terminal sizing inherited from 5.1r4.
- **SIP / VoIP** — PJSIP 2.17 multi-account softphone inherited from 5.1r4.
- **XMPP / Jabber** — new alpha backend with TLS/STARTTLS, SASL PLAIN over TLS, roster, one-to-one chat, presence and basic MUC rooms.
- **SSH** — integrated OpenSSH terminal module. Linux/FreeBSD/macOS use a real PTY for host-key prompts, password prompts and interactive shells. Saved SSH profiles can specify an identity file, semicolon-separated OpenSSH `-o` overrides, and an optional remote command while still honoring normal `~/.ssh/config`, `known_hosts`, and `ssh-agent`. The package also carries `scripts/wafflehouse-ssh`, an optional media/SIP bridge companion for WaffleHouse shell-server deployments.
- **NNTP / Usenet** — new alpha backend with TLS/plain connections, AUTHINFO, GROUP, ARTICLE/raw commands and basic POST support.

### Auxiliary network tools

These appear in **Network Tools** only when compiled in:

- **Gopher** — simple Gopher fetch/browser view.
- **Gemini** — simple TLS Gemini fetch/browser view.
- **Mosh** — launches the system `mosh` client so its native roaming/UDP terminal behavior is preserved.
- **Media / Radio** — inherited WaffleHouse media/radio/streaming module.

## Build and choose modules

On Linux, FreeBSD and macOS:

```sh
./build.sh
```

The interactive builder asks which modules to compile. For scripted builds:

```sh
./build.sh --os linux --protocols all
./build.sh --os linux --protocols irc,telnet,ssh,nntp
./build.sh --os freebsd --protocols aim,irc,telnet,xmpp,ssh
./build.sh --os macos --protocols aim,irc,telnet,sip,media,xmpp,ssh,nntp,gopher,gemini,mosh
```

Windows 10/11:

```powershell
.\build-windows.ps1
.\build-windows.ps1 -Protocols "irc,telnet,ssh,nntp"
```

Termux:

```sh
./scripts/build-termux.sh
./scripts/build-termux.sh --protocols irc,telnet,xmpp,ssh
```

Available feature tokens are:

```text
aim, irc, telnet, sip, media, xmpp, ssh, nntp, mosh, gopher, gemini
```

Aliases such as `oscar`, `bbs`, `jabber`, `usenet`, `news`, `voip` are accepted where applicable.

## Frontends

The same binary supports:

```sh
wafflehouse-client --gui
wafflehouse-client --cli
```

GUI mode is the primary desktop interface. CLI mode keeps the inherited ncurses frontend and has core connection support for XMPP, SSH and NNTP in addition to the WaffleHouse protocols.

## Compatibility

WaffleHouse-Client deliberately retains the WaffleHouse/CPX protocol framing needed to interoperate with compatible WaffleHouse-family clients for secure AIM/IRC functionality. When the current 5.3 settings store is empty, it can import saved connections from the older `WaffleHouseGUI` and `WaffleHouse-CLI` stores without deleting the legacy data.

## Alpha notes

The AIM/OSCAR, IRC, Telnet/BBS, SIP and media code is inherited from 5.1r4. XMPP, SSH, NNTP, Gopher, Gemini and Mosh integration are new in 5.2 and should be treated as early implementations. In particular, the XMPP backend does not yet implement the full XMPP extension ecosystem, NNTP is a basic reader/poster rather than a full threaded newsreader, and SSH/Mosh depend on the platform clients being installed.

See `WAFFLEHOUSE-MODULES.md` for the feature architecture and `README-WAFFLEHOUSE-5.1r4.md` for the inherited 5.1r4 reference documentation.
