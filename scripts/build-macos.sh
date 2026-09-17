#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

[ "$(uname -s)" = Darwin ] || { echo "build-macos.sh must run on macOS." >&2; exit 2; }

RELEASE_VERSION=5.4-alpha2
CLEAN=0
FORCE_PJSIP=0
AUTO_DEPS=1
MAKE_DMG=1
WITH_MEDIA_DEPS=0
BUILD_TYPE=Release
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 2)
PJSIP_PREFIX=${PJSIP_PREFIX:-${HOME:-$ROOT_DIR}/.local/wafflehouse-pjsip}
BUILD_DIR=${BUILD_DIR:-$ROOT_DIR/build-macos}
INSTALL_MODE=ask
INSTALL_PREFIX=/usr/local
APP_INSTALL_DIR=${WAFFLEHOUSE_APP_INSTALL_DIR:-/Applications}
ASSUME_YES=0
UNINSTALL_MODE=0
BOOTSTRAP_ONLY=0

usage() {
  cat <<USAGE
Usage: ./build.sh --os macos [options]

Build WaffleHouse-Client $RELEASE_VERSION for macOS. The macOS builder now performs a
complete development-environment preflight before compilation: full Xcode, Homebrew,
required formulae, PJSIP, Qt deployment, bundle verification, ad-hoc local signing,
and a standalone DMG are handled in one workflow.

  --clean          remove the macOS build directory first
  --pjsip          force rebuild managed PJSIP 2.17
  --dmg            create a standalone DMG (default)
  --no-dmg         skip DMG creation
  --with-media-deps
                   optionally try to install mpv/ffmpeg with Homebrew;
                   failures do not abort the WaffleHouse-Client build
  --no-auto-deps   do not bootstrap/install Xcode, Homebrew, or formulae
  --bootstrap-only prepare Xcode/Homebrew/dependencies and exit before compiling
  --install        install after a successful build without the final prompt
  --no-install     build only and suppress the install prompt
  --uninstall      remove the installed app + launcher; preserve user config
  --remove-only    alias of --uninstall
  --prefix PATH    command launcher prefix (default /usr/local)
  --yes, -y        answer yes to builder-owned confirmation prompts
  --jobs N         parallel build jobs
  --build-type T   CMake build type (default Release)
  -h, --help       show this help

Default output:
  app: $BUILD_DIR/wafflehouse-client.app
  dmg: $BUILD_DIR/WaffleHouse-Client-$RELEASE_VERSION-macOS.dmg

Default optional installation targets:
  app:      $APP_INSTALL_DIR/WaffleHouse-Client.app
  launcher: $INSTALL_PREFIX/bin/wafflehouse-client

Important: Apple requires an Apple Account session for archived Xcode downloads.
On macOS 13 the builder opens Apple's official compatible Xcode download. It can
then extract/move/configure Xcode automatically, but it never asks for or stores
Apple credentials.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --clean) CLEAN=1 ;;
    --pjsip) FORCE_PJSIP=1 ;;
    --dmg) MAKE_DMG=1 ;;
    --no-dmg) MAKE_DMG=0 ;;
    --with-media-deps) WITH_MEDIA_DEPS=1 ;;
    --no-auto-deps) AUTO_DEPS=0 ;;
    --bootstrap-only) BOOTSTRAP_ONLY=1; INSTALL_MODE=no ;;
    --install) INSTALL_MODE=yes ;;
    --no-install) INSTALL_MODE=no ;;
    --uninstall|--remove-only) UNINSTALL_MODE=1; INSTALL_MODE=no ;;
    --yes|-y) ASSUME_YES=1 ;;
    --prefix) shift; [ "$#" -gt 0 ] || { echo "--prefix requires a path" >&2; exit 2; }; INSTALL_PREFIX=$1 ;;
    --jobs) shift; JOBS=${1:?--jobs requires a value} ;;
    --build-type) shift; BUILD_TYPE=${1:?--build-type requires a value} ;;
    --dry-run) echo "--dry-run is currently a Linux/FreeBSD builder option." >&2; exit 2 ;;
    --upgrade|--audio-diagnose|--no-audio-fix)
      echo "$1 is currently a Linux/FreeBSD builder option." >&2; exit 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown macOS build option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

case "$JOBS" in ''|*[!0-9]*|0) echo "--jobs requires a positive integer" >&2; exit 2 ;; esac
[ "$INSTALL_PREFIX" = / ] || INSTALL_PREFIX=${INSTALL_PREFIX%/}
INSTALL_BIN="$INSTALL_PREFIX/bin/wafflehouse-client"
INSTALL_APP="$APP_INSTALL_DIR/WaffleHouse-Client.app"
DMG_PATH="$BUILD_DIR/WaffleHouse-Client-$RELEASE_VERSION-macOS.dmg"

run_admin() {
  if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi
}

say_step() {
  printf '\n==> %s\n' "$*"
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

ask_yes_no() {
  prompt=$1
  default=${2:-no}
  if [ "$ASSUME_YES" -eq 1 ]; then return 0; fi
  [ -t 0 ] || { [ "$default" = yes ]; return; }
  if [ "$default" = yes ]; then
    printf '%s [Y/n]: ' "$prompt"
  else
    printf '%s [y/N]: ' "$prompt"
  fi
  IFS= read -r answer
  case "$answer" in
    y|Y|yes|YES|Yes) return 0 ;;
    n|N|no|NO|No) return 1 ;;
    '') [ "$default" = yes ] ;;
    *) return 1 ;;
  esac
}

if [ "$UNINSTALL_MODE" -eq 1 ]; then
  if [ "$ASSUME_YES" -ne 1 ] && [ -t 0 ]; then
    echo "Uninstall targets:"
    echo "  app:      $INSTALL_APP"
    echo "  launcher: $INSTALL_BIN"
    ask_yes_no "Remove WaffleHouse-Client application files and preserve user configuration?" no || {
      echo "Uninstall cancelled."
      exit 0
    }
  fi
  say_step "Removing installed WaffleHouse-Client $RELEASE_VERSION for macOS"
  removed=0
  if [ -e "$INSTALL_APP" ] || [ -L "$INSTALL_APP" ]; then run_admin rm -rf "$INSTALL_APP"; echo "Removed app: $INSTALL_APP"; removed=1; fi
  if [ -e "$INSTALL_BIN" ] || [ -L "$INSTALL_BIN" ]; then run_admin rm -f "$INSTALL_BIN"; echo "Removed launcher: $INSTALL_BIN"; removed=1; fi
  if [ "$removed" -eq 0 ]; then echo "No installed WaffleHouse-Client files were found at the standard targets."; fi
  echo "Per-user WaffleHouse-Client/WaffleHouse configuration was preserved."
  exit 0
fi

MACOS_VERSION=$(sw_vers -productVersion 2>/dev/null || echo unknown)
MACOS_MAJOR=$(printf '%s' "$MACOS_VERSION" | awk -F. '{print $1}')
MACOS_MINOR=$(printf '%s' "$MACOS_VERSION" | awk -F. '{print $2+0}')
ARCH=$(uname -m)

case "$MACOS_MAJOR" in
  ''|*[!0-9]*) fail "Could not determine the macOS version (sw_vers returned '$MACOS_VERSION')." ;;
  0|1|2|3|4|5|6|7|8|9|10|11|12) fail "WaffleHouse-Client $RELEASE_VERSION supports macOS 13 or newer; detected $MACOS_VERSION." ;;
  *) : ;;
esac

# macOS 13 cannot install today's newest Xcode from the App Store. Use Apple's
# archived versions that are compatible with Ventura. Apple still requires the
# user to authenticate the browser session; the builder never handles credentials.
XCODE_RECOMMENDED=
XCODE_DOWNLOAD_URL=https://developer.apple.com/download/all/
if [ "$MACOS_MAJOR" -eq 13 ]; then
  if [ "$MACOS_MINOR" -ge 5 ]; then
    XCODE_RECOMMENDED=15.2
    XCODE_DOWNLOAD_URL='https://developer.apple.com/services-account/download?path=/Developer_Tools/Xcode_15.2/Xcode_15.2.xip'
  else
    XCODE_RECOMMENDED=14.3.1
    XCODE_DOWNLOAD_URL='https://developer.apple.com/services-account/download?path=/Developer_Tools/Xcode_14.3.1/Xcode_14.3.1.xip'
  fi
fi

xcode_usable() {
  app=$1
  [ -x "$app/Contents/Developer/usr/bin/xcodebuild" ] || return 1
  DEVELOPER_DIR="$app/Contents/Developer" "$app/Contents/Developer/usr/bin/xcodebuild" -version >/dev/null 2>&1
}

find_downloaded_xcode_app() {
  if xcode_usable "$HOME/Downloads/Xcode.app"; then
    printf '%s\n' "$HOME/Downloads/Xcode.app"
    return 0
  fi
  for app in "$HOME"/Downloads/Xcode*.app; do
    [ -e "$app" ] || continue
    if xcode_usable "$app"; then printf '%s\n' "$app"; return 0; fi
  done
  return 1
}

find_downloaded_xip() {
  if [ -n "$XCODE_RECOMMENDED" ]; then
    candidate="$HOME/Downloads/Xcode_${XCODE_RECOMMENDED}.xip"
    [ -f "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
  fi
  for xip in "$HOME"/Downloads/Xcode*.xip; do
    [ -f "$xip" ] || continue
    printf '%s\n' "$xip"
    return 0
  done
  return 1
}

configure_xcode() {
  app=$1
  [ "$app" = /Applications/Xcode.app ] || {
    say_step "Moving full Xcode into /Applications"
    if [ -e /Applications/Xcode.app ]; then
      backup="/Applications/Xcode.backup.$(date +%Y%m%d%H%M%S).app"
      echo "Existing /Applications/Xcode.app is not usable; moving it to $backup"
      run_admin mv /Applications/Xcode.app "$backup"
    fi
    run_admin mv "$app" /Applications/Xcode.app
    app=/Applications/Xcode.app
  }

  xcode_usable "$app" || fail "Xcode.app exists but xcodebuild is not usable: $app"
  say_step "Configuring full Xcode"
  run_admin xcode-select -s "$app/Contents/Developer"
  run_admin xcodebuild -license accept
  run_admin xcodebuild -runFirstLaunch
  selected=$(xcode-select -p 2>/dev/null || true)
  [ "$selected" = "$app/Contents/Developer" ] || fail "xcode-select did not switch to full Xcode (selected: $selected)."
  xcodebuild -version
}

ensure_xcode() {
  if xcode_usable /Applications/Xcode.app; then
    configure_xcode /Applications/Xcode.app
    return 0
  fi

  downloaded_app=$(find_downloaded_xcode_app 2>/dev/null || true)
  if [ -n "$downloaded_app" ]; then
    configure_xcode "$downloaded_app"
    return 0
  fi

  if [ "$AUTO_DEPS" -eq 0 ]; then
    fail "Full Xcode is required. Install a compatible Xcode.app in /Applications, then rerun without --no-auto-deps."
  fi

  echo
  echo "Full Xcode is required; Command Line Tools alone are not sufficient for this build."
  if [ -n "$XCODE_RECOMMENDED" ]; then
    echo "Detected macOS $MACOS_VERSION. Recommended archived Xcode: $XCODE_RECOMMENDED"
  else
    echo "Detected macOS $MACOS_VERSION. Opening Apple's Xcode downloads page."
  fi
  echo "Apple may require you to sign in with your Apple Account in Safari."
  echo "WaffleHouse-Client never asks for or stores those credentials."
  open "$XCODE_DOWNLOAD_URL" >/dev/null 2>&1 || true

  if [ ! -t 0 ]; then
    fail "Xcode download requires an interactive Apple sign-in. Complete the download, then rerun the builder."
  fi

  printf '\nWhen Xcode finishes downloading, press Enter here to continue (or Ctrl-C to stop): '
  IFS= read -r _continue

  downloaded_app=$(find_downloaded_xcode_app 2>/dev/null || true)
  if [ -n "$downloaded_app" ]; then
    configure_xcode "$downloaded_app"
    return 0
  fi

  xip=$(find_downloaded_xip 2>/dev/null || true)
  [ -n "$xip" ] || fail "No Xcode.app or Xcode .xip was found in ~/Downloads. Finish the Apple download and rerun ./build.sh."

  echo "Found: $xip"
  ask_yes_no "Extract the Xcode archive now? This is large and can take a while." yes || \
    fail "Xcode extraction was skipped. Extract the .xip manually and rerun the builder."

  say_step "Extracting Xcode archive"
  xip_dir=$(dirname "$xip")
  xip_name=$(basename "$xip")
  (
    cd "$xip_dir"
    /usr/bin/xip -x "$xip_name"
  )

  downloaded_app=$(find_downloaded_xcode_app 2>/dev/null || true)
  [ -n "$downloaded_app" ] || fail "Xcode archive extraction finished, but Xcode.app was not found in ~/Downloads."
  configure_xcode "$downloaded_app"
}

ensure_homebrew() {
  if command -v brew >/dev/null 2>&1; then
    return 0
  fi
  for candidate in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    if [ -x "$candidate" ]; then
      eval "$("$candidate" shellenv)"
      command -v brew >/dev/null 2>&1 && return 0
    fi
  done

  [ "$AUTO_DEPS" -eq 1 ] || fail "Homebrew is missing and --no-auto-deps was supplied."
  command -v curl >/dev/null 2>&1 || fail "curl is required to bootstrap Homebrew."

  say_step "Installing Homebrew using the official Homebrew installer"
  export NONINTERACTIVE=1
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  unset NONINTERACTIVE

  for candidate in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    if [ -x "$candidate" ]; then
      eval "$("$candidate" shellenv)"
      break
    fi
  done
  command -v brew >/dev/null 2>&1 || fail "Homebrew installation completed but 'brew' is not on PATH."
}

need_formula() {
  formula=$1
  brew list --versions "$formula" >/dev/null 2>&1 && return 0
  if [ "$AUTO_DEPS" -eq 0 ]; then
    echo "Missing Homebrew dependency: $formula" >&2
    return 1
  fi
  say_step "Installing macOS dependency: $formula"
  brew install "$formula"
}

optional_formula() {
  formula=$1
  purpose=$2
  if brew list --versions "$formula" >/dev/null 2>&1; then
    echo "Optional runtime dependency present: $formula ($purpose)"
    return 0
  fi
  if [ "$WITH_MEDIA_DEPS" -eq 0 ]; then
    echo "NOTE: optional runtime dependency '$formula' is not installed ($purpose)."
    echo "      WaffleHouse-Client still builds; only that helper-backed feature is unavailable."
    echo "      Re-run with --with-media-deps if you want the builder to try Homebrew."
    return 0
  fi
  say_step "Trying optional macOS runtime dependency: $formula"
  if brew install "$formula"; then
    echo "Installed optional dependency: $formula"
  else
    echo "WARNING: Homebrew could not install optional dependency '$formula'; continuing." >&2
  fi
}

say_step "macOS development environment preflight"
echo "macOS:         $MACOS_VERSION"
echo "Architecture:  $ARCH"
ensure_xcode
ensure_homebrew
BREW_PREFIX=$(brew --prefix)
echo "Homebrew:      $BREW_PREFIX"

# Do not install the giant Homebrew `qt` meta-package. WaffleHouse uses only
# Qt Core/Gui/Widgets/Network/Multimedia plus deployment tooling. Keeping the
# dependency set narrow reduces source builds and avoids dragging unrelated
# Qt modules into the application bundle on older macOS releases.
for formula in cmake pkg-config qtbase qtmultimedia qttools libsodium ncurses portaudio opus; do
  need_formula "$formula"
done
command -v git >/dev/null 2>&1 || fail "git is required after Xcode setup."

optional_formula mpv "local/radio media playback"
optional_formula ffmpeg "WaffleCast/album-art and SSH/remote media helper"

if [ "$BOOTSTRAP_ONLY" -eq 1 ]; then
  echo
  echo "macOS bootstrap complete. Xcode, Homebrew, and required WaffleHouse dependencies are ready."
  exit 0
fi

if [ "$FORCE_PJSIP" -eq 1 ]; then rm -f "$PJSIP_PREFIX/.wafflehouse-pjsip-build"; fi
if ! PKG_CONFIG_PATH="$PJSIP_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}" pkg-config --exact-version=2.17 libpjproject >/dev/null 2>&1 || \
   [ ! -f "$PJSIP_PREFIX/.wafflehouse-pjsip-build" ]; then
  say_step "Preparing managed PJSIP 2.17 for macOS"
  PJSIP_PREFIX="$PJSIP_PREFIX" "$ROOT_DIR/scripts/bootstrap-pjsip.sh"
fi

[ "$CLEAN" -eq 0 ] || rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

QTBASE_PREFIX=$(brew --prefix qtbase)
QTMULTIMEDIA_PREFIX=$(brew --prefix qtmultimedia)
QTTOOLS_PREFIX=$(brew --prefix qttools)
SODIUM_PREFIX=$(brew --prefix libsodium)
NCURSES_PREFIX=$(brew --prefix ncurses)
PORTAUDIO_PREFIX=$(brew --prefix portaudio)
OPUS_PREFIX=$(brew --prefix opus)
export PATH="$QTTOOLS_PREFIX/bin:$QTBASE_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$PJSIP_PREFIX/lib/pkgconfig:$SODIUM_PREFIX/lib/pkgconfig:$NCURSES_PREFIX/lib/pkgconfig:$PORTAUDIO_PREFIX/lib/pkgconfig:$OPUS_PREFIX/lib/pkgconfig:$BREW_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
QT_CMAKE_PREFIX="$QTBASE_PREFIX;$QTMULTIMEDIA_PREFIX;$QTTOOLS_PREFIX"

printf '%s\n' "============================================================" \
  "                    WAFFLEHOUSE-CLIENT $RELEASE_VERSION" \
  "============================================================" \
  "Platform:       macOS $MACOS_VERSION" \
  "Architecture:   $ARCH" \
  "Build type:     $BUILD_TYPE" \
  "Xcode:          $(xcodebuild -version | tr '\n' ' ' | sed 's/[[:space:]]*$//')" \
  "Homebrew:       $BREW_PREFIX" \
  "Qt base:        $QTBASE_PREFIX" \
  "PJSIP:          $PJSIP_PREFIX" \
  "Install app:    $INSTALL_APP" \
  "CLI launcher:   $INSTALL_BIN" \
  "DMG:            $(if [ "$MAKE_DMG" -eq 1 ]; then echo "$DMG_PATH"; else echo disabled; fi)"

say_step "Configuring WaffleHouse-Client $RELEASE_VERSION"
# Feature flags are fixed -DNAME=ON/OFF tokens supplied by the top-level builder.
# shellcheck disable=SC2086
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_PREFIX_PATH="$QT_CMAKE_PREFIX" ${WAFFLEHOUSE_FEATURE_CMAKE_ARGS:-}

say_step "Building WaffleHouse-Client $RELEASE_VERSION"
cmake --build "$BUILD_DIR" -j "$JOBS"

APP="$BUILD_DIR/wafflehouse-client.app"
[ -d "$APP" ] || fail "macOS app bundle not produced at $APP"
MACDEPLOYQT="$QTTOOLS_PREFIX/bin/macdeployqt"
if [ ! -x "$MACDEPLOYQT" ]; then
  MACDEPLOYQT=$(find "$QTTOOLS_PREFIX" -type f -name macdeployqt -perm -111 -print -quit 2>/dev/null || true)
fi
[ -n "$MACDEPLOYQT" ] && [ -x "$MACDEPLOYQT" ] || fail "macdeployqt not found under $QTTOOLS_PREFIX"

# Keep notification sounds self-contained inside the .app.
mkdir -p "$APP/Contents/Resources/sounds"
cp -f "$ROOT_DIR"/sounds/*.wav "$APP/Contents/Resources/sounds/"

# Seed the Cocoa QPA plugin before macdeployqt. On older Homebrew/Qt combinations
# macdeployqt can fail to discover it when Qt is split into separate kegs.
QT_PLUGIN_DIR=
for qtpaths in "$QTBASE_PREFIX/bin/qtpaths" "$QTBASE_PREFIX/bin/qtpaths6" "$QTTOOLS_PREFIX/bin/qtpaths"; do
  if [ -x "$qtpaths" ]; then
    candidate=$($qtpaths --plugin-dir 2>/dev/null || true)
    if [ -n "$candidate" ] && [ -d "$candidate" ]; then QT_PLUGIN_DIR=$candidate; break; fi
  fi
done
if [ -z "$QT_PLUGIN_DIR" ]; then
  cocoa=$(find "$QTBASE_PREFIX" -type f -path '*/platforms/libqcocoa.dylib' -print -quit 2>/dev/null || true)
  [ -n "$cocoa" ] && QT_PLUGIN_DIR=$(dirname "$(dirname "$cocoa")")
fi
[ -n "$QT_PLUGIN_DIR" ] || fail "Could not locate the Qt plugin directory under $QTBASE_PREFIX"
COCOA_PLUGIN="$QT_PLUGIN_DIR/platforms/libqcocoa.dylib"
[ -f "$COCOA_PLUGIN" ] || fail "Qt Cocoa platform plugin is missing: $COCOA_PLUGIN"
mkdir -p "$APP/Contents/PlugIns/platforms"
cp -f "$COCOA_PLUGIN" "$APP/Contents/PlugIns/platforms/libqcocoa.dylib"

# Give macdeployqt every relevant Homebrew library directory, not only Qt kegs.
# This fixes Ventura failures where split Qt plugins depend on Brotli, WebP,
# HarfBuzz, Graphite2, etc. and macdeployqt otherwise sees only an @rpath name.
set -- "$APP" -verbose=2 -always-overwrite -no-codesign
DEP_FORMULAS="qtbase qtmultimedia qttools libsodium ncurses portaudio opus"
for root_formula in qtbase qtmultimedia qttools; do
  deps=$(brew deps --installed --formula "$root_formula" 2>/dev/null || true)
  DEP_FORMULAS="$DEP_FORMULAS $deps"
done
# Include any already-installed split Qt module so a plugin that legitimately
# references it can be resolved, without requiring the giant `qt` meta-package.
installed_qt=$(brew list --formula 2>/dev/null | awk '/^qt/ {print}' || true)
DEP_FORMULAS="$DEP_FORMULAS $installed_qt"
seen_libpaths=
for formula in $DEP_FORMULAS; do
  prefix=$(brew --prefix "$formula" 2>/dev/null || true)
  [ -n "$prefix" ] && [ -d "$prefix/lib" ] || continue
  case " $seen_libpaths " in
    *" $prefix/lib "*) continue ;;
  esac
  seen_libpaths="$seen_libpaths $prefix/lib"
  set -- "$@" "-libpath=$prefix/lib"
done

DEPLOY_LOG="$BUILD_DIR/macdeployqt.log"
say_step "Deploying Qt frameworks, plugins, and runtime libraries"
if "$MACDEPLOYQT" "$@" >"$DEPLOY_LOG" 2>&1; then
  cat "$DEPLOY_LOG"
else
  cat "$DEPLOY_LOG" >&2
  fail "macdeployqt returned a failure status. See $DEPLOY_LOG"
fi
if grep -E '(^|[[:space:]])ERROR:|Cannot resolve (rpath|dependency|library)' "$DEPLOY_LOG" >/dev/null 2>&1; then
  echo "macdeployqt reported unresolved deployment errors:" >&2
  grep -E '(^|[[:space:]])ERROR:|Cannot resolve (rpath|dependency|library)' "$DEPLOY_LOG" >&2 || true
  fail "Refusing to mark this build successful while deployment errors remain."
fi

# macdeployqt normally writes qt.conf; write the required plugin path explicitly
# so the installed bundle never falls back to a Homebrew plugin directory.
mkdir -p "$APP/Contents/Resources" "$APP/Contents/PlugIns/platforms"
cat > "$APP/Contents/Resources/qt.conf" <<'QTCONF'
[Paths]
Plugins = PlugIns
QTCONF

# macdeployqt should have preserved the seeded plugin. Restore it if an older
# tool replaced the PlugIns directory, then run deployment once more so its Qt
# references are rewritten to the private frameworks inside the app.
if [ ! -f "$APP/Contents/PlugIns/platforms/libqcocoa.dylib" ]; then
  cp -f "$COCOA_PLUGIN" "$APP/Contents/PlugIns/platforms/libqcocoa.dylib"
  POST_LOG="$BUILD_DIR/macdeployqt-post-plugin.log"
  set -- "$APP" -verbose=2 -always-overwrite -no-codesign
  for libpath in $seen_libpaths; do set -- "$@" "-libpath=$libpath"; done
  if "$MACDEPLOYQT" "$@" >"$POST_LOG" 2>&1; then
    cat "$POST_LOG"
  else
    cat "$POST_LOG" >&2
    fail "macdeployqt failed while redeploying the Cocoa plugin."
  fi
fi

[ -f "$APP/Contents/PlugIns/platforms/libqcocoa.dylib" ] || fail "Bundle verification failed: libqcocoa.dylib is missing."
[ -d "$APP/Contents/Frameworks" ] || fail "Bundle verification failed: Contents/Frameworks is missing."
[ -x "$APP/Contents/MacOS/wafflehouse-client" ] || fail "Bundle verification failed: main executable is missing."

# Audit every Mach-O payload before signing. A distributable app must not retain
# Homebrew/source-tree absolute paths, and every @rpath dependency must exist in
# Contents/Frameworks. This catches the exact class of "build succeeded" bundle
# that later dies on another Mac with missing Brotli/WebP/Qt libraries.
say_step "Auditing bundled Mach-O dependencies"
AUDIT_LOG="$BUILD_DIR/bundle-dependency-audit.log"
: > "$AUDIT_LOG"
audit_failed=0
find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f -print | while IFS= read -r binary; do
  file "$binary" 2>/dev/null | grep -q 'Mach-O' || continue
  otool -L "$binary" 2>/dev/null | awk 'NR > 1 {print $1}' | while IFS= read -r dep; do
    case "$dep" in
      @rpath/*)
        rel=${dep#@rpath/}
        [ -e "$APP/Contents/Frameworks/$rel" ] || printf 'MISSING_RPATH|%s|%s\n' "$binary" "$dep"
        ;;
      @loader_path/*)
        rel=${dep#@loader_path/}
        base=$(dirname "$binary")
        [ -e "$base/$rel" ] || printf 'MISSING_LOADER_PATH|%s|%s\n' "$binary" "$dep"
        ;;
      @executable_path/*)
        rel=${dep#@executable_path/}
        [ -e "$APP/Contents/MacOS/$rel" ] || printf 'MISSING_EXECUTABLE_PATH|%s|%s\n' "$binary" "$dep"
        ;;
      /System/Library/*|/usr/lib/*) : ;;
      "$BREW_PREFIX"/*|/usr/local/opt/*|/opt/homebrew/opt/*|"$ROOT_DIR"/*)
        printf 'EXTERNAL_BUILD_PATH|%s|%s\n' "$binary" "$dep"
        ;;
      /*)
        # Other absolute paths are suspicious in a standalone release. Keep the
        # audit strict instead of silently shipping machine-local dependencies.
        printf 'EXTERNAL_ABSOLUTE_PATH|%s|%s\n' "$binary" "$dep"
        ;;
    esac
  done
done > "$AUDIT_LOG"
if [ -s "$AUDIT_LOG" ]; then
  cat "$AUDIT_LOG" >&2
  fail "Bundle dependency audit found unresolved or machine-local Mach-O references."
fi

# The finished local build is ad-hoc signed only after all deployment mutations.
# Distribution signing/notarization can replace this signature later.
say_step "Ad-hoc signing the finished local bundle"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

say_step "Verifying standalone bundle"
VERSION_OUTPUT=$($APP/Contents/MacOS/wafflehouse-client --version 2>&1) || {
  echo "$VERSION_OUTPUT" >&2
  fail "The packaged executable could not start for the --version smoke test."
}
printf 'Smoke test: %s\n' "$VERSION_OUTPUT"
case "$VERSION_OUTPUT" in
  *"$RELEASE_VERSION"*) : ;;
  *) fail "Packaged executable does not report WaffleHouse-Client $RELEASE_VERSION." ;;
esac

# A release DMG is built after deployment/signing so it contains the exact
# verified app and a conventional Applications shortcut.
if [ "$MAKE_DMG" -eq 1 ]; then
  say_step "Creating standalone WaffleHouse-Client $RELEASE_VERSION DMG"
  DMG_STAGE="$BUILD_DIR/dmg-stage"
  rm -rf "$DMG_STAGE" "$DMG_PATH"
  mkdir -p "$DMG_STAGE"
  ditto "$APP" "$DMG_STAGE/WaffleHouse-Client.app"
  ln -s /Applications "$DMG_STAGE/Applications"
  hdiutil create -volname "WaffleHouse-Client $RELEASE_VERSION" \
    -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG_PATH"
  rm -rf "$DMG_STAGE"
  [ -f "$DMG_PATH" ] || fail "DMG creation reported success but $DMG_PATH does not exist."
fi

ask_install() {
  [ "$INSTALL_MODE" = ask ] || return 0
  if [ "$ASSUME_YES" -eq 1 ]; then INSTALL_MODE=yes; return 0; fi
  if [ ! -t 0 ]; then INSTALL_MODE=no; return 0; fi
  echo
  echo "Build and standalone bundle verification succeeded."
  echo "Optional installation will copy the app to:"
  echo "  $INSTALL_APP"
  echo "and add the command launcher:"
  echo "  $INSTALL_BIN"
  printf 'Install WaffleHouse-Client now? [y/N]: '
  IFS= read -r answer
  case "$answer" in y|Y|yes|YES|Yes) INSTALL_MODE=yes ;; *) INSTALL_MODE=no ;; esac
}

ask_install
if [ "$INSTALL_MODE" = yes ]; then
  say_step "Installing WaffleHouse-Client $RELEASE_VERSION for macOS"
  run_admin mkdir -p "$APP_INSTALL_DIR" "$INSTALL_PREFIX/bin"
  if [ -e "$INSTALL_APP" ]; then run_admin rm -rf "$INSTALL_APP"; fi
  run_admin ditto "$APP" "$INSTALL_APP"
  if [ -e "$INSTALL_BIN" ] || [ -L "$INSTALL_BIN" ]; then run_admin rm -f "$INSTALL_BIN"; fi
  run_admin ln -s "$INSTALL_APP/Contents/MacOS/wafflehouse-client" "$INSTALL_BIN"
  run_admin codesign --verify --deep --strict "$INSTALL_APP"
  echo "Installed app:      $INSTALL_APP"
  echo "Installed launcher: $INSTALL_BIN"
else
  echo
  echo "System installation skipped."
  echo "Built app: $APP"
fi

echo
echo "Optional media runtime status:"
if brew list --versions mpv >/dev/null 2>&1 || command -v mpv >/dev/null 2>&1; then
  echo "  mpv:    available"
else
  echo "  mpv:    not installed (client works; Media playback is disabled)"
fi
if brew list --versions ffmpeg >/dev/null 2>&1 || command -v ffmpeg >/dev/null 2>&1; then
  echo "  ffmpeg: available"
else
  echo "  ffmpeg: not installed (WaffleCast broadcasting/album-art and SSH remote-media helpers are unavailable)"
fi

cat <<DONE

WaffleHouse-Client $RELEASE_VERSION macOS build complete.
GUI app: $APP
CLI:    "$APP/Contents/MacOS/wafflehouse-client" --cli
$(if [ "$MAKE_DMG" -eq 1 ]; then echo "DMG:    $DMG_PATH"; fi)

The local app is ad-hoc signed so it can be tested immediately. A distributor
with an Apple Developer ID can replace the ad-hoc signature and notarize the
same verified bundle for public distribution.
DONE
