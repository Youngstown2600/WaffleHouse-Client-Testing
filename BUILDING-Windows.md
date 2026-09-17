# WaffleHouse-Client 5.3 — Windows

Run `./build-windows.ps1` from PowerShell. The builder uses MSYS2 UCRT64 so the existing Qt, pkg-config, libsodium, ncurses, and managed PJSIP 2.17 code paths stay consistent with Unix builds. The resulting `dist/windows` folder contains `wafflehouse-client.exe` plus GUI/CLI launchers. Both frontends use the same OSCAR backend and therefore the same NINA compatibility path.


## Uninstall / remove

The Windows builder produces a portable package rather than registering a system installer. Running:

```powershell
.\build-windows.ps1 -Uninstall
```

removes the builder-managed `dist\windows` package and preserves per-user WaffleHouse settings. If you manually copied the portable package elsewhere, remove that copied folder separately.
