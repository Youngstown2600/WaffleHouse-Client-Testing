# WaffleCast — WaffleHouse-Client 5.4 alpha1

WaffleCast lets one WaffleHouse-Client user broadcast the audio currently playing in Media Center to other WaffleHouse clients.

## Alpha 1 workflow

1. Load/play a local audio file in Media Center.
2. Click **Start Broadcast** in the WaffleCast section.
3. Confirm the hostname/IP and TCP port (default 8173) listeners can reach.
4. In an AIM IM, IRC PM, or IRC channel, type `/wafflecast` (alias `/wcast`).
5. Another WaffleHouse-Client 5.4 alpha1 user receives a native Listen prompt. Their Media Center opens and plays the live stream.

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
