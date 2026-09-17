# WaffleHouse-Client 5.2r3

WaffleHouse-Client 5.2 is the WaffleHouse-branded release of the expanded modular client branch built from WaffleHouse-Client 5.1r4.

## Major additions over 5.1r4

- XMPP/Jabber
- SSH terminal sessions with Unix PTY support, per-profile identity/options/remote-command overrides, and the optional WaffleHouse SSH media/SIP companion
- NNTP/Usenet
- Gopher
- Gemini
- Mosh launcher integration
- Existing AIM/OSCAR, IRC, Telnet/BBS/MUD, SIP/VoIP, Media, CPX secure messaging/rooms, file transfer, OSCAR voice and diagnostics retained

IMAP/SMTP was deliberately removed before this 5.2 package; WaffleHouse-Client is not an email client.

## Modular GUI

The GUI is feature-driven. It reads the same compile-time feature matrix used by the builders and creates only the protocol/account controls and module launchers that were compiled into that build. If a user builds only IRC and Telnet, SIP/XMPP/SSH/NNTP/Media/etc. do not leave blank panels or dead buttons in the interface. In 5.2r3 the launcher wraps into a compact grid, and the product header/splash show only WaffleHouse-Client plus the version instead of printing the entire module list.

The builders accept selectable protocol/module sets, including `--protocols` on Unix-like targets and the corresponding Windows selection parameter.

## Identity and compatibility

- Display name: `WaffleHouse-Client`
- Release: `5.2r3`
- Executable: `wafflehouse-client`
- Settings/data paths: WaffleHouse-Client-compatible paths from 5.1r4
- WaffleHouse/CPX wire framing remains compatible with existing WaffleHouse-family peers
- Original WaffleHouse icons/logo are restored in the Qt resource bundle and Unix desktop installation

The new XMPP/SSH/NNTP/Gopher/Gemini/Mosh modules remain early implementations and should receive runtime testing on each target platform.
