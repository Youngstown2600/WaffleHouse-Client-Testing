# WaffleHouse-Client 5.3 — Termux / Android

Run `./scripts/build-termux.sh` inside Termux. It enables the Termux X11 repository, installs the native Termux toolchain/Qt packages, builds the shared WaffleHouse source, and installs `wafflehouse-client`, `wafflehouse-client-cli`, and `wafflehouse-client-gui` under `$PREFIX/bin`. CLI runs in the normal terminal. GUI requires a Termux:X11 session. Both frontends share the exact same AIM/OSCAR backend; NINA is auto-detected from `*.nina.chat` or can be selected explicitly as the `nina` network profile.


## Uninstall / remove

With no arguments the Termux builder now asks whether to build/install or uninstall/remove. For scripted removal:

```sh
./scripts/build-termux.sh --uninstall
```

This removes the installed WaffleHouse binaries/wrappers, shell helper, desktop entry, icons, and shared sounds under `$PREFIX` while preserving per-user WaffleHouse configuration. `--remove-only` is an alias.
