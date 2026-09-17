# WaffleHouse-Client 5.4 alpha1 — Media Center

## Album Art and WaffleCast (5.4 alpha1)

The Now Playing area includes a 180x180 Album Art panel. For supported local audio files, WaffleHouse uses ffmpeg to extract the first embedded/attached cover image into memory and converts it to PNG for display. The music file itself is not exposed.

WaffleCast lets the current Media Center source be broadcast to other WaffleHouse clients. The host uses ffmpeg to produce a real-time 128 kbit/s MP3 stream. AIM/IRC carries only the compact tokenized invite (`/wafflecast` or `/wcast`); listeners play the stream through the existing mpv backend. Listener playback is transient, so it does not overwrite the persistent Media Center queue. Tokenized metadata and cover endpoints keep the listener's title/art display synchronized with the DJ.


The WaffleHouse Media Center is the integrated mpv-backed local-media and internet-radio player. The same persistent media library is shared by the GUI and CLI on supported desktop/Unix builds and Termux where Media is enabled.

## Persistent media library

5.1r4 makes the Media playlist persistent by default. WaffleHouse restores the saved queue when the client starts, but it **does not automatically start playback**.

The library is stored below Qt's per-user application-data directory in a `media/` folder:

```text
media/
├── library.json       # saved queue, titles, current item, modes, imported-playlist metadata
├── queue.m3u8         # portable mirror used to rehydrate mpv
└── playlists/         # cached copies of imported local playlist definitions
```

On Linux this is normally below `~/.local/share/...`; macOS, Windows, FreeBSD and Termux use their platform-appropriate Qt application-data location. The Media Center's **Library Folder** button opens the exact directory in use.

Persistent state includes:

- playlist/queue entries and their order
- resolved stream URLs from imported playlists
- display titles supplied by mpv/playlist metadata
- current playlist index
- volume and mute state
- shuffle state
- repeat mode
- imported playlist source metadata

Queue changes are written atomically using `QSaveFile`. The queue is also mirrored as `queue.m3u8`, so a newly started mpv process can restore the complete saved queue without requiring the original imported playlist file.

## Playlist imports

**Import Playlist** accepts:

- `.pls`
- `.m3u`
- `.m3u8`
- `.xspf`
- supported playlist URLs

When a local playlist definition is imported, WaffleHouse stores a content-addressed copy under `media/playlists/`. The original file is still used for the initial load so relative paths inside local playlists remain valid. Once mpv expands the playlist, the resolved queue is stored independently in `library.json` and `queue.m3u8`.

Deleting the original `.pls`/`.m3u` afterward therefore does not erase already imported internet-radio entries from WaffleHouse.

WaffleHouse does **not** silently copy MP3, FLAC, video, or other large local media files into its data directory. Local media entries remain path references.

## Media Center controls

Use **Media → Open Media Center**. The window provides transport controls, seek, volume/mute, queue management, shuffle/repeat, local/internet playlist imports, direct SHOUTcast/Icecast/HTTP(S)/HLS stream URLs, SHOUTcast directory search, playlist export, a **Library Folder** shortcut, and a 10-band EQ. Video is displayed by mpv in its native video window.

CLI media commands include `/media`, `/mstatus`, `/mplay`, `/mstream`, `/mshoutcast`, `/menqueue`, `/mplaylist`, `/mpause`, `/mresume`, `/mstop`, `/mnext`, `/mprev`, `/mseek`, `/mvolume`, `/mmute`, `/mshuffle`, `/mrepeat`, and `/meq`.

Stop preserves the current queue. After a restart, Play/Resume restores the saved queue into the backend lazily and resumes the remembered playlist item.

## Backend

The media engine controls an external `mpv` process through JSON IPC. Linux and FreeBSD use a private native Unix-domain socket; `ffmpeg` remains available for the SSH companion/remote-audio path and broad codec support. HLS `.m3u8` manifests intended as one continuous stream should be opened with **Stream URL** or `/mstream`; ordinary station/media playlists should use **Playlist URL** / **Import Playlist** / `/mplaylist`.

The dedicated YouTube resolver remains intentionally absent; mpv runs with `--ytdl=no` and WaffleHouse does not require yt-dlp/Deno.
