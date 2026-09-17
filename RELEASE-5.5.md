# WaffleHouse-Client 5.5

## WaffleCast discovery and bare-host connection

This build keeps the working tokenized WaffleCast stream and AIM/IRC invite receiver, but removes the need to know the tokenized URL when connecting manually.

- **Connect / Listen searches the LAN first** using UDP discovery on port 8172.
- The WaffleCast audio TCP port remains user-selectable (for example 9119). The discovery reply carries the actual current stream URL and random session token.
- A listener can enter only a base address such as `http://10.0.0.2:9119`. WaffleHouse probes `/.well-known/wafflecast` and resolves the active stream automatically.
- Full Listen, `stream.mp3`, M3U, PLS, and AIM/IRC invite URLs remain compatible.
- The working AIM/IRC incoming invite path is preserved. WaffleCast no longer depends on a GUI `/wafflecast` command for ordinary use.
- Existing Stop/Play, Pause/Play, single-instance, album-art, public-browser, VLC/mpv, and transient-library behavior is preserved.

### LAN firewall note

For automatic LAN discovery, allow **UDP 8172** on the broadcaster. The selected WaffleCast audio port must also be reachable over **TCP** (for example TCP 9119). Manual base-URL connection still works when UDP discovery is unavailable.

## Internet / remote WaffleCast broadcasting

Media Center now has **WaffleCast → Broadcast Settings** so Internet broadcasting can be configured once and reused.

- **Local listen TCP port** controls the port WaffleHouse opens on the broadcasting computer.
- **Advertise to Internet/remote listeners** switches WaffleCast from LAN-only advertising to a configured public address.
- **Public IP / DNS hostname** is the ISP-facing address or DDNS hostname placed into AIM invite frames and public listen links.
- **Public/WAN TCP port** is independent of the local listen port, so NAT/port forwarding may translate between them.
- Example: pfSense `WAN TCP 9119 -> Kusanagi TCP 8173`. Configure local port `8173`, public port `9119`, and the public IP/DDNS hostname in WaffleCast Broadcast Settings.
- A remote WaffleHouse listener can use **Connect / Listen** and enter only `http://PUBLIC-IP:9119`; WaffleHouse probes `/.well-known/wafflecast`, learns the current session path/token, and joins it automatically.
- AIM invite reception remains compatible and uses the public advertised address when Internet broadcasting is enabled.
- Browser/VLC/mpv/M3U/PLS links also use the public advertised address and port.
- LAN discovery remains direct: local clients use the UDP sender's LAN address and the broadcaster's local TCP port instead of hairpinning through the WAN address.

Port forwarding is performed on the user's router/firewall; WaffleHouse does not modify pfSense, UPnP, or NAT rules automatically.
