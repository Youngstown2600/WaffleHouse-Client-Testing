# WaffleHouse-Client modular architecture

WaffleHouse-Client 5.2 uses compile-time feature flags as the single source of truth for what the user sees.

## CMake switches

| Module | Switch |
|---|---|
| AIM/OSCAR | `WAFFLEHOUSE_ENABLE_OSCAR` |
| IRC | `WAFFLEHOUSE_ENABLE_IRC` |
| Telnet/BBS | `WAFFLEHOUSE_ENABLE_TELNET` |
| SIP/VoIP | `WAFFLEHOUSE_ENABLE_SIP` |
| Media | `WAFFLEHOUSE_ENABLE_MEDIA` |
| XMPP/Jabber | `WAFFLEHOUSE_ENABLE_XMPP` |
| SSH | `WAFFLEHOUSE_ENABLE_SSH` |
| NNTP/Usenet | `WAFFLEHOUSE_ENABLE_NNTP` |
| Mosh | `WAFFLEHOUSE_ENABLE_MOSH` |
| Gopher | `WAFFLEHOUSE_ENABLE_GOPHER` |
| Gemini | `WAFFLEHOUSE_ENABLE_GEMINI` |

All compile-time feature switches now use the `WAFFLEHOUSE_*` namespace so the expanded modules are first-class WaffleHouse-Client 5.2 features.

## GUI behavior

`src/buildfeatures.h` exposes the compiled feature matrix. The main window uses it to construct the module row and protocol choices. No button is created for a disabled feature. The auxiliary network-tools window similarly creates only tabs whose backing module is enabled.

Core session modules use the normal saved-connection/backend model. Gopher, Gemini and Mosh are auxiliary tools and live in the Network Tools window instead of pretending to be chat accounts.

## New protocol backends

- `src/xmppbackend.*`
- `src/sshbackend.*`
- `src/nntpbackend.*`
- `src/networktoolswindow.*`

## Compatibility boundary

CPX secure-message framing and selected WaffleHouse OSCAR extension identifiers remain unchanged so the fork can communicate with existing compatible clients. User-facing application identity, executable name and normal application data paths are WaffleHouse-Client-specific.
