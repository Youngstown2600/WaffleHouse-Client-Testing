# WaffleCast — WaffleHouse-Client 5.5

WaffleCast lets one WaffleHouse-Client user broadcast the audio currently playing in Media Center to other WaffleHouse clients.

## Current 5.5 workflow

1. Load and play a local audio file in Media Center.
2. For LAN-only use, the defaults are enough. For Internet broadcasting, open **Broadcast Settings** and save the public IP/DDNS name plus local and public/WAN TCP ports.
3. Click **Start Broadcast**.
4. A WaffleHouse listener can use **Connect / Listen**. LAN broadcasts are discovered automatically; remote listeners can enter only `http://PUBLIC-HOST:PORT`.
5. For AIM delivery, use **Copy Invite** and send the copied WaffleCast invite through the AIM conversation. The receiving WaffleHouse client recognizes the frame and offers to listen.
6. People without WaffleHouse can use **Copy Public Link** in a browser, VLC, mpv, or another compatible network audio player.

AIM/IRC is only a signaling path when an invite is sent; the audio itself travels directly over the WaffleCast HTTP stream. The GUI does not require slash commands for WaffleCast operation. The host's files are never exposed as browsable/downloadable files.

## Audio / metadata

- Host transcode: ffmpeg -> 128 kbit/s MP3 live stream.
- Listener playback: existing mpv Media Center backend.
- The listener's persistent Media Center playlist is preserved; WaffleCast uses a transient mpv source.
- The DJ's current title, pause state, listener count, and artwork generation are exposed through the tokenized session metadata endpoint.
- Embedded MP3/M4A/FLAC/etc. cover art is extracted in memory and displayed in Media Center. WaffleCast listeners fetch only the extracted image bytes, never the original music file.

## Networking

Alpha 1 is direct-connect. On the same LAN, the detected LAN IPv4 address is usually sufficient. Across the Internet, the chosen host/port must be reachable (for example via a public IPv6 address, a forwarded TCP port, or another route). A relay/NAT-traversal mode is a future WaffleCast enhancement.

## Security / privacy

Each broadcast receives a new random session token. The stream, metadata, and artwork endpoints require that unguessable token in the path. Ending the broadcast invalidates the session immediately.

## 5.5: manual and public listening

### WaffleHouse-compatible listener
1. Host starts WaffleCast and chooses **Copy Public Link**.
2. Listener opens Media Center and chooses **Connect / Listen**.
3. Paste the public Listen URL, `stream.mp3`, `listen.m3u`, or `listen.pls` URL.
4. The client normalizes it to the WaffleCast live MP3 stream and continues polling WaffleCast metadata and cover art.

### Listener without WaffleHouse
Open the **Public Link** in a web browser. The page provides an HTML5 audio player, current title, album art, listener count, and direct links for MP3, M3U, and PLS. VLC/mpv can open the direct MP3/M3U/PLS URLs.

The public page is still protected by the session's random token URL. Ending the WaffleCast invalidates the token.


## 5.5: LAN discovery and bare host/port

`Connect / Listen` now sends a `WAFFLECAST_DISCOVER/1` UDP probe on port **8172** before asking for a URL. Active hosts return their current title and complete tokenized stream URL, including any custom audio TCP port. If discovery does not find a host, entering only a base URL such as `http://10.0.0.2:9119` causes WaffleHouse to request `/.well-known/wafflecast` and resolve the live session automatically.

## 5.5: Internet broadcasting and NAT/port forwarding

Open **Media Center -> WaffleCast -> Broadcast Settings** before starting the broadcast.

Configure:

- **Local listen TCP port**: the TCP socket opened by WaffleHouse on the broadcaster.
- **Advertise this broadcast to Internet/remote listeners**: enable for off-LAN listeners.
- **Public IP / DNS hostname**: the broadcaster's ISP-facing IP or DDNS hostname.
- **Public/WAN TCP port**: the external port listeners use. It may differ from the local listen port.

Example with pfSense:

`WAN TCP 9119 -> 10.0.0.2 TCP 8173`

WaffleHouse settings:

- Local listen port: `8173`
- Public IP/DNS: your public ISP address or DDNS hostname
- Public/WAN port: `9119`

A remote WaffleHouse client can choose **Connect / Listen** and enter `http://PUBLIC-IP:9119`. The client probes the well-known WaffleCast endpoint, resolves the current random session path, and begins playback. A received AIM WaffleCast invite also uses the configured public address automatically.

LAN discovery intentionally ignores the advertised WAN route for playback and connects to the broadcaster's UDP sender address plus local TCP port. This avoids requiring NAT reflection/hairpin NAT for machines on the same network.
