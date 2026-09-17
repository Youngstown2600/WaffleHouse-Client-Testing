# Building WaffleHouse-Client 5.3 on macOS

WaffleHouse-Client 5.3 changes the macOS path from a "dependencies are already installed" build into a bootstrap + build + package workflow.

From the source root:

```sh
chmod +x build.sh
./build.sh
```

Choose **3) macOS**. For a scripted build:

```sh
./build.sh --os macos --clean
```

## What the 5.3 macOS builder does

Before compilation it now:

1. Detects the macOS version and CPU architecture.
2. Verifies a **full Xcode.app**, not just Command Line Tools.
3. On macOS 13 Ventura, opens Apple's compatible archived Xcode download if Xcode is missing:
   - Ventura 13.5+ -> Xcode 15.2
   - Ventura 13.0-13.4 -> Xcode 14.3.1
4. Detects an extracted `Xcode.app` in `~/Downloads`, offers to move it to `/Applications`, runs `xcode-select`, accepts the license, and runs Xcode first-launch setup.
5. Installs Homebrew with the official Homebrew installer when Homebrew is missing.
6. Installs only the compile/link packages WaffleHouse actually needs: `cmake`, `pkg-config`, `qtbase`, `qtmultimedia`, `qttools`, `libsodium`, `ncurses`, `portaudio`, and `opus`.
7. Builds the managed PJSIP 2.17 tree when needed.
8. Builds WaffleHouse-Client.
9. Runs `macdeployqt` with the Homebrew dependency library paths required by split Qt packages.
10. Explicitly verifies and bundles the macOS Cocoa QPA plugin (`libqcocoa.dylib`) and writes `Contents/Resources/qt.conf` with `Plugins = PlugIns`.
11. Treats unresolved `macdeployqt` errors as fatal. The builder no longer prints `Build succeeded` after a broken deployment.
12. Runs a Mach-O dependency audit so no missing `@rpath` target or machine-local Homebrew/source path can ship.
13. Ad-hoc signs the completed local app and verifies the signature.
14. Runs the packaged executable with `--version` as a smoke test.
15. Creates a standalone drag-to-Applications DMG by default.

Apple requires an Apple Account session for archived Xcode downloads. The builder can open the official Apple download, detect/extract the resulting `.xip`, and configure the application, but it **never requests, captures, or stores Apple credentials**.

To prepare the machine without compiling WaffleHouse:

```sh
./build.sh --os macos --bootstrap-only
```

To prevent any automatic dependency/bootstrap actions:

```sh
./build.sh --os macos --no-auto-deps
```

## Qt packaging fix

The 5.3 builder no longer requires Homebrew's giant `qt` meta-package. WaffleHouse directly uses Qt Core, Gui, Widgets, Network, and Multimedia, so the builder installs the narrower split formula set and uses `qttools` for `macdeployqt`.

The finished application must contain at least:

```text
WaffleHouse-Client.app/
└── Contents/
    ├── MacOS/
    │   └── wafflehouse-client
    ├── Frameworks/
    │   └── ... private Qt/runtime frameworks ...
    ├── PlugIns/
    │   └── platforms/
    │       └── libqcocoa.dylib
    └── Resources/
        ├── qt.conf
        └── sounds/
```

If `macdeployqt` reports an unresolved rpath/library or the Cocoa plugin is absent, the 5.3 builder stops instead of installing a known-broken `.app`.

## DMG and installation

A standalone DMG is created by default:

```text
build-macos/WaffleHouse-Client-5.3-macOS.dmg
```

Disable DMG creation with:

```sh
./build.sh --os macos --no-dmg
```

The built app is normally:

```text
build-macos/wafflehouse-client.app
```

After a successful verified build, the interactive builder asks before copying the application to:

```text
/Applications/WaffleHouse-Client.app
```

and creating:

```text
/usr/local/bin/wafflehouse-client
```

The local app is ad-hoc signed for testing. Public Developer ID distribution/notarization still requires the distributor's own Apple Developer identity.

## Optional media helpers

`mpv` and `ffmpeg` remain optional runtime helpers. They never block the client build. To ask the builder to try installing them:

```sh
./build.sh --os macos --with-media-deps
```

## Uninstall / remove

```sh
./build.sh --os macos --uninstall
```

This removes the installed app and launcher while preserving per-user WaffleHouse configuration, history, accounts, and logs. `--remove-only` is an alias.
