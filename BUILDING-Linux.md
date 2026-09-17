# WaffleHouse-Client 5.3 — Linux build notes

WaffleHouse-Client 5.2 builds the same C++/Qt6 GUI + ncurses CLI on Debian-family Linux, Fedora, and Slackware. OSCAR voice adds the Qt 6 Multimedia development module to the existing Core/Gui/Widgets/Network requirements. The Media Center still uses `mpv` + `ffmpeg`.

## Debian / Ubuntu / Linux Mint and derivatives

`build.sh` can install `qt6-base-dev`, `qt6-multimedia-dev`, compiler/CMake/pkg-config, libsodium, ncurses, xkbcommon, PJSIP prerequisites, ALSA development files, mpv, and ffmpeg.

## Fedora / dnf systems

The builder uses the Qt 6 base and `qt6-qtmultimedia-devel` development packages plus the corresponding crypto/audio/network prerequisites.

## Slackware

The builder detects Slackware/slackpkg and uses available base packages; if `sbopkg` is available it can assist with Qt6/mpv packages. Package availability varies by Slackware installation, so the final dependency audit remains authoritative.

## System installation and /bin launcher

After a successful interactive build, WaffleHouse asks whether to install the application system-wide and add `/bin/wafflehouse-client`. The default is **No**. When accepted, the normal executable/data install remains under the configured prefix (default `/usr/local`) and `/bin/wafflehouse-client` is added as the global launcher.

## Testing without system application installation

```sh
./build.sh --os linux --no-install
```

Use `--no-auto-deps` as well if you only want the dependency audit/build attempt and do not want the builder to install missing packages.


## Uninstall / remove

Interactive `./build.sh` now offers **Uninstall / Remove** before dependency or protocol prompts. Scripted removal is also available:

```sh
./build.sh --os linux --uninstall
```

This removes the installed executable, `/bin/wafflehouse-client` launcher, `wafflehouse-shell`, desktop entry, installed icons, and WaffleHouse shared sound/resource directory. **Per-user configuration, accounts, history, and logs are preserved.** `--remove-only` is an alias, and `--yes` can be used for unattended removal.
