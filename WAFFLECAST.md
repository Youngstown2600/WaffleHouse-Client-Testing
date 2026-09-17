# WaffleCast — WaffleHouse-Client 5.4 alpha2

WaffleCast lets one WaffleHouse-Client user broadcast the audio currently playing in Media Center to other WaffleHouse clients.

## Alpha 1 workflow

1. Load/play a local audio file in Media Center.
2. Click **Start Broadcast** in the WaffleCast section.
3. Confirm the hostname/IP and TCP port (default 8173) listeners can reach.
4. In an AIM IM, IRC PM, or IRC channel, type `/wafflecast` (alias `/wcast`).
5. Another WaffleHouse-Client 5.4 alpha2 user receives a native Listen prompt. Their Media Center opens and plays the live stream.

AIM/IRC carries only the compact session invitation. Audio is sent directly over a tokenized HTTP stream from the host WaffleHouse client. The host's files are never exposed as downloadable files.

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

## 5.4 alpha2: manual and public listening

### WaffleHouse-compatible listener
1. Host starts WaffleCast and chooses **Copy Public Link**.
2. Listener opens Media Center and chooses **Connect / Listen**.
3. Paste the public Listen URL, `stream.mp3`, `listen.m3u`, or `listen.pls` URL.
4. The client normalizes it to the WaffleCast live MP3 stream and continues polling WaffleCast metadata and cover art.

### Listener without WaffleHouse
Open the **Public Link** in a web browser. The page provides an HTML5 audio player, current title, album art, listener count, and direct links for MP3, M3U, and PLS. VLC/mpv can open the direct MP3/M3U/PLS URLs.

The public page is still protected by the session's random token URL. Ending the WaffleCast invalidates the token.
