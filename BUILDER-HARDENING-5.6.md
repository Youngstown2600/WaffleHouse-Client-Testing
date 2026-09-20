# WaffleHouse-Client 5.6 Builder Hardening

This release hardens the macOS builder around failures observed on real Ventura systems and adds defensive preflight checks for future installs.

## Fixed failures

1. **Split Qt / missing Qt6Multimedia CMake package**
   - Verifies `Qt6MultimediaConfig.cmake`, not just the package-manager receipt.
   - Repairs `qtmultimedia` automatically when incomplete.
   - Passes both `Qt6_DIR` and `Qt6Multimedia_DIR` explicitly to CMake.
   - Clears stale CMake cache state and retries configuration after repair.

2. **Fresh Intel Mac with no Homebrew**
   - Detects CPU architecture before package-manager bootstrap.
   - Reuses `/usr/local` Intel Homebrew when already present.
   - If the current official installer refuses a fresh Intel install, bootstraps the Homebrew repository in the historical `/usr/local` Intel prefix instead of invoking the Apple-Silicon installer path.
   - Intel package installs automatically retry from source when a bottle is unavailable.

3. **`macdeployqt` location assumptions**
   - No longer assumes the tool lives under `qttools`.
   - Searches `qtbase`, `qttools`, `qtmultimedia`, and `PATH`.
   - Repairs Qt deployment tooling automatically if the executable is genuinely absent.
   - A successful compile is preserved while deployment tooling is repaired.

## Additional review changes

- Verifies required command-line tools after dependency setup: CMake, pkg-config, xcodebuild, codesign, otool, file, ditto, and hdiutil.
- Verifies the Qt6 base CMake package before configuration.
- Recalculates split-Qt prefixes after repair instead of retaining stale paths.
- Keeps the existing standalone bundle dependency audit, Cocoa platform-plugin verification, ad-hoc signing check, executable smoke test, and DMG existence check.
- Corrected stale 5.5 builder labels in the top-level, Linux/FreeBSD, Termux, and Windows/MSYS2 scripts to 5.6.

## Behavior

Automatic repair is enabled by default. `--no-auto-deps` remains the escape hatch for users who want dependency installation/repair disabled.
