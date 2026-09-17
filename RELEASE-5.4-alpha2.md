# WaffleHouse-Client 5.4 alpha2

## WaffleCast public listening + manual receiver

5.4 alpha2 makes WaffleCast directly testable and usable outside WaffleHouse.

- Media Center adds **Connect / Listen**. Paste a WaffleCast public page, direct stream, M3U, or PLS URL and WaffleHouse normalizes it to the live stream while retaining WaffleCast metadata/cover support.
- Broadcasting adds **Copy Public Link**. The link opens a built-in browser player with live track title, embedded album art, listener count, and play controls.
- Every broadcast exposes standard `audio/mpeg` (`stream.mp3`), M3U (`listen.m3u`), and PLS (`listen.pls`) endpoints. VLC, mpv, compatible browser audio players, and other Internet-radio clients can listen without WaffleHouse.
- Native AIM/IRC `/wafflecast` invitations remain supported and use the same live session.
- The random per-broadcast session token remains part of every URL; ending the broadcast invalidates those URLs.
- Listener count includes WaffleHouse-compatible clients, browser, VLC/mpv, and other direct MP3 listeners.

All 5.3 Stop/Play, Pause/Play, single-instance behavior, album-art support, transient listener playback, and persistent Media Center library protections are retained.
