#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

CLEAN=0
DRY_RUN=0
AUTO_DEPS=1
INSTALL_MODE=ask
INSTALL_PREFIX=/usr/local
PREFIX_EXPLICIT=0
INSTALL_BIN_ALIAS_MODE=0
SYSTEM_BIN=/bin/wafflehouse-client
UPGRADE_MODE=0
UNINSTALL_MODE=0
ASSUME_YES=0
BUILD_TYPE=Release
USER_HOME=${HOME:-$ROOT_DIR}
PJSIP_PREFIX=${PJSIP_PREFIX:-$USER_HOME/.local/wafflehouse-pjsip}
BUILD_PJSIP=0
AUDIO_DIAG_ONLY=0
AUTO_AUDIO_FIX=1
JOBS=
PRIV_METHOD=
ROOT_METHOD=
PKG_CONFIG_CMD=
LINUX_PKG_MANAGER=
BUILD_CC=
BUILD_CXX=
BUILD_MAKE=
AUX_GCC=
AUX_GXX=
MISSING_DEPS=
CHECK_FAILURES=0

# Unix/Linux platform builder used by the combined desktop bundle.
# The top-level build.sh selects this path after the user chooses Linux or FreeBSD/Unix.
if [ -n "${TERMUX_VERSION:-}" ] || [ "${PREFIX:-}" = "/data/data/com.termux/files/usr" ]; then
  echo "This is the Unix/Linux WaffleHouse-Client bundle. Use WaffleHouse-Client-Termux-Build-0.9 on Termux." >&2
  exit 2
fi

HOST_OS=$(uname -s)
case "$HOST_OS" in
  Linux) OS_FAMILY=linux ;;
  FreeBSD) OS_FAMILY=freebsd ;;
  Darwin)
    echo "This is the Unix/Linux WaffleHouse-Client bundle. Use the top-level ./build.sh and select macOS on macOS." >&2
    exit 2
    ;;
  *)
    echo "Unsupported OS: $HOST_OS (this bundle supports Linux and FreeBSD)" >&2
    exit 2
    ;;
esac

usage() {
  cat <<EOF2
Usage: ./build.sh [options]

Build WaffleHouse-Client 5.5 for Linux or FreeBSD (GUI + CLI).
The Media Center adds local media/video, SHOUTcast/Icecast/HTTP/HLS streams, internet radio, and playlists.

The builder performs a full dependency preflight. Missing dependencies are
installed automatically on supported Linux package managers or with FreeBSD pkg,
then every requirement is checked again before CMake is allowed to configure.

Options:
  --pjsip                 Force rebuild of managed PJSIP 2.17 softphone dependency
  --audio-diagnose          Inspect host OS/HDA audio state without changing audio configuration
  --no-audio-fix           Keep diagnostics but disable guarded automatic FreeBSD audio repair
  --clean                 Remove the generated build directory before building
  --dry-run               Show dependency/build/install actions without changing anything
  --no-auto-deps          Do not install missing dependencies automatically
  --install               Build/install and add /bin/wafflehouse-client after success
  --upgrade               Clean-build and upgrade the detected/current installation
                          while preserving all per-user configuration
  --uninstall             Remove installed application files but preserve user config
  --remove-only           Alias of --uninstall (client-up.sh compatibility)
  --yes, -y               Do not ask for upgrade/uninstall confirmation
  --no-install            Build only; do not offer installation
  --prefix PATH           Installation prefix (default: /usr/local)
  --jobs N                Parallel build jobs (default: detected CPU count)
  --build-type TYPE       CMake build type (default: Release)
  -h, --help              Show this help

Default install locations:
  executable: PREFIX/bin/wafflehouse-client
  /bin launcher: /bin/wafflehouse-client (offered after a successful interactive build)
  desktop:    PREFIX/share/applications/wafflehouse-client.desktop

Testing note:
  A normal ./build.sh build does NOT install WaffleHouse-Client automatically.
  After a successful interactive build you are explicitly asked whether to install
  it system-wide and place a launcher at /bin/wafflehouse-client. Enter means No.
  --no-install suppresses the offer entirely.
  Dependency auto-install and the existing guarded FreeBSD audio repair are
  separate from application installation; use --no-auto-deps --no-audio-fix
  when you want a build attempt that makes no such host compatibility changes.
EOF2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --pjsip) BUILD_PJSIP=1 ;;
    --audio-diagnose) AUDIO_DIAG_ONLY=1 ;;
    --no-audio-fix) AUTO_AUDIO_FIX=0 ;;
    --clean) CLEAN=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --no-auto-deps) AUTO_DEPS=0 ;;
    --install) INSTALL_MODE=yes; INSTALL_BIN_ALIAS_MODE=1 ;;
    --upgrade) UPGRADE_MODE=1; INSTALL_MODE=yes; INSTALL_BIN_ALIAS_MODE=1; CLEAN=1 ;;
    --uninstall|--remove-only) UNINSTALL_MODE=1; INSTALL_MODE=no ;;
    --yes|-y) ASSUME_YES=1 ;;
    --no-install) INSTALL_MODE=no ;;
    --prefix)
      shift
      [ "$#" -gt 0 ] || { echo "--prefix requires a path" >&2; exit 2; }
      INSTALL_PREFIX=$1
      PREFIX_EXPLICIT=1
      ;;
    --jobs)
      shift
      [ "$#" -gt 0 ] || { echo "--jobs requires a positive integer" >&2; exit 2; }
      case "$1" in ''|*[!0-9]*|0) echo "--jobs requires a positive integer" >&2; exit 2 ;; esac
      JOBS=$1
      ;;
    --build-type)
      shift
      [ "$#" -gt 0 ] || { echo "--build-type requires a value" >&2; exit 2; }
      BUILD_TYPE=$1
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

# Uninstall/remove-only wins if both it and --upgrade were supplied (for
# compatibility with the historical client-up.sh --remove-only workflow).
if [ "$UNINSTALL_MODE" -eq 1 ]; then
  UPGRADE_MODE=0
  INSTALL_MODE=no
fi

# Upgrade/uninstall mode follows the currently installed standard PREFIX/bin
# layout when --prefix was not supplied. Normal build/install keeps /usr/local
# as its predictable default.
if [ "$PREFIX_EXPLICIT" -eq 0 ] && { [ "$UPGRADE_MODE" -eq 1 ] || [ "$UNINSTALL_MODE" -eq 1 ]; }; then
  if command -v wafflehouse-client >/dev/null 2>&1; then
    CURRENT_BIN=$(command -v wafflehouse-client)
    case "$CURRENT_BIN" in
      */bin/wafflehouse-client)
        DETECTED_PREFIX=${CURRENT_BIN%/bin/wafflehouse-client}
        [ -n "$DETECTED_PREFIX" ] || DETECTED_PREFIX=/
        INSTALL_PREFIX=$DETECTED_PREFIX
        ;;
    esac
  fi
fi

if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
  elif command -v sysctl >/dev/null 2>&1; then
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
  else
    JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
  fi
fi

if [ "$INSTALL_PREFIX" != "/" ]; then INSTALL_PREFIX=${INSTALL_PREFIX%/}; fi
INSTALL_BINDIR="$INSTALL_PREFIX/bin"
INSTALL_DESKTOPDIR="$INSTALL_PREFIX/share/applications"
INSTALL_BIN="$INSTALL_BINDIR/wafflehouse-client"
INSTALL_SHELL="$INSTALL_BINDIR/wafflehouse-shell"
INSTALL_SSH_COMPANION="$INSTALL_BINDIR/wafflehouse-ssh"
INSTALL_DATADIR="$INSTALL_PREFIX/share/wafflehouse-client"
INSTALL_DESKTOP="$INSTALL_DESKTOPDIR/wafflehouse-client.desktop"
INSTALL_ICON_THEME_DIR="$INSTALL_PREFIX/share/icons/hicolor"
LEGACY_GUI="$INSTALL_BINDIR/wafflehouse-gui"
LEGACY_CLI="$INSTALL_BINDIR/wafflehouse-cli"
LEGACY_GUI_DESKTOP="$INSTALL_DESKTOPDIR/wafflehouse-gui.desktop"
LEGACY_CLI_DESKTOP="$INSTALL_DESKTOPDIR/wafflehouse-cli.desktop"

show_header() {
  cat <<EOF2
============================================================
                    WAFFLEHOUSE-CLIENT 5.5
============================================================
Host OS:        $HOST_OS
Build jobs:     $JOBS
Build type:     $BUILD_TYPE
Install prefix: $INSTALL_PREFIX
Binary target:  $INSTALL_BIN
Auto deps:      $(if [ "$AUTO_DEPS" -eq 1 ]; then echo enabled; else echo disabled; fi)
Upgrade mode:   $(if [ "$UPGRADE_MODE" -eq 1 ]; then echo enabled; else echo disabled; fi)

The dependency preflight checks:
  - CMake 3.20 or newer
  - pkg-config/pkgconf
  - C/C++17 compiler compatible with the installed Qt 6 ABI
  - GCC/G++ availability (explicitly required on FreeBSD as an auxiliary toolchain)
  - GNU Make (make on Linux, gmake on FreeBSD)
  - Qt 6 Core, Gui, Widgets, Network, and Multimedia development modules
  - libsodium development files
  - wide-character ncurses (ncursesw) development files
  - xkbcommon development files used by the Qt GUI platform integration
  - PJSIP/PJSUA2 2.17 (managed local build, 32-account / 64-call capable)
  - SIP audio/crypto prerequisites (ALSA on Linux; PortAudio/Opus/G.729 on FreeBSD)
  - S.I.P.H.E.R. r14 FreeBSD HDA compatibility preflight/guarded repair
  - WaffleHouse-Client media playback backend
  - FFmpeg tools for broad media/stream compatibility
EOF2
  if [ "$OS_FAMILY" = freebsd ]; then
    echo "  - FreeBSD base Clang/libc++ for Qt ABI-compatible compilation"
    echo "  - FreeBSD GCC/G++ under /usr/local (required/verified, but not forced onto packaged Qt)"
  fi
  echo
}

find_first_command() {
  for candidate in "$@"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done
  return 1
}

find_freebsd_gnu_pair() {
  AUX_GCC=
  AUX_GXX=

  # Prefer unversioned wrappers if the installed gcc package supplies them.
  if command -v gcc >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1; then
    AUX_GCC=$(command -v gcc)
    AUX_GXX=$(command -v g++)
    return 0
  fi

  # FreeBSD gcc packages commonly install versioned binaries (gcc14/g++14, etc.).
  best=0
  best_cc=
  best_cxx=
  for cxx_path in /usr/local/bin/g++[0-9]*; do
    [ -x "$cxx_path" ] || continue
    base=${cxx_path##*/}
    suffix=${base#g++}
    case "$suffix" in ''|*[!0-9]*) continue ;; esac
    cc_path="/usr/local/bin/gcc$suffix"
    [ -x "$cc_path" ] || continue
    if [ "$suffix" -gt "$best" ] 2>/dev/null; then
      best=$suffix
      best_cc=$cc_path
      best_cxx=$cxx_path
    fi
  done

  if [ -n "$best_cc" ] && [ -n "$best_cxx" ]; then
    AUX_GCC=$best_cc
    AUX_GXX=$best_cxx
    return 0
  fi
  return 1
}

version_ge() {
  # Compare dotted numeric versions without relying on GNU sort -V.
  awk -v A="$1" -v B="$2" 'BEGIN {
    n=split(A,a,"."); m=split(B,b,"."); max=(n>m?n:m);
    for(i=1;i<=max;i++){av=(i<=n?a[i]+0:0); bv=(i<=m?b[i]+0:0); if(av>bv)exit 0; if(av<bv)exit 1}
    exit 0
  }'
}

append_missing() {
  dep=$1
  case "|$MISSING_DEPS|" in
    *"|$dep|"*) ;;
    *) MISSING_DEPS="${MISSING_DEPS}${MISSING_DEPS:+|}$dep" ;;
  esac
}

print_check() {
  label=$1
  status=$2
  detail=${3:-}
  if [ "$status" = ok ]; then
    printf '  [OK]      %-30s %s\n' "$label" "$detail"
  else
    printf '  [MISSING] %-30s %s\n' "$label" "$detail"
  fi
}

check_command_dep() {
  label=$1
  key=$2
  shift 2
  found=$(find_first_command "$@" || true)
  if [ -n "$found" ]; then
    print_check "$label" ok "$found"
    return 0
  fi
  print_check "$label" missing "not found"
  append_missing "$key"
  CHECK_FAILURES=$((CHECK_FAILURES + 1))
  return 1
}

check_pkg_module() {
  module=$1
  label=$2
  key=$3
  if [ -n "$PKG_CONFIG_CMD" ] && "$PKG_CONFIG_CMD" --exists "$module" 2>/dev/null; then
    ver=$($PKG_CONFIG_CMD --modversion "$module" 2>/dev/null || true)
    print_check "$label" ok "${ver:+v$ver}"
    return 0
  fi
  print_check "$label" missing "pkg-config module: $module"
  append_missing "$key"
  CHECK_FAILURES=$((CHECK_FAILURES + 1))
  return 1
}

compiler_smoke_test() {
  [ -n "$BUILD_CXX" ] || return 1
  tmpbase="${TMPDIR:-/tmp}/wafflehouse-cxx-test.$$"
  src="$tmpbase.cpp"
  bin="$tmpbase.bin"
  trap 'rm -f "$src" "$bin"' EXIT HUP INT TERM
  printf '#include <iostream>\nint main(){std::cout << "ok"; return 0;}\n' > "$src"
  if "$BUILD_CXX" -std=c++17 "$src" -o "$bin" >/dev/null 2>&1 && [ -x "$bin" ]; then
    rm -f "$src" "$bin"
    trap - EXIT HUP INT TERM
    print_check "C++17 compiler smoke test" ok "$BUILD_CXX"
    return 0
  fi
  rm -f "$src" "$bin"
  trap - EXIT HUP INT TERM
  print_check "C++17 compiler smoke test" missing "$BUILD_CXX cannot compile/link"
  append_missing "working C++17 toolchain"
  CHECK_FAILURES=$((CHECK_FAILURES + 1))
  return 1
}

qt_abi_smoke_test() {
  [ -n "$BUILD_CXX" ] || return 1
  [ -n "$PKG_CONFIG_CMD" ] || return 1
  "$PKG_CONFIG_CMD" --exists Qt6Core 2>/dev/null || return 1

  tmpdir="${TMPDIR:-/tmp}/wafflehouse-qt-abi-test.$$"
  rm -rf "$tmpdir"
  mkdir -p "$tmpdir"
  cat > "$tmpdir/CMakeLists.txt" <<'EOF_QT_CMAKE'
cmake_minimum_required(VERSION 3.20)
project(WaffleHouseQtAbiProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(qt_abi_probe main.cpp)
target_link_libraries(qt_abi_probe PRIVATE Qt6::Core)
EOF_QT_CMAKE
  cat > "$tmpdir/main.cpp" <<'EOF_QT_CPP'
#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <string>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QString value = QStringLiteral("wafflehouse");
    std::string native = value.toStdString();
    QThread *worker = QThread::create([] {});
    delete worker;
    QDir dir;
    dir.mkpath(QStringLiteral("."));
    QTimer::singleShot(std::chrono::milliseconds(1), &app, &QCoreApplication::quit);
    return native.empty() ? 1 : 0;
}
EOF_QT_CPP

  if env CC="$BUILD_CC" CXX="$BUILD_CXX" cmake -S "$tmpdir" -B "$tmpdir/build" -G "Unix Makefiles" \
      -DCMAKE_MAKE_PROGRAM="$BUILD_MAKE" >/dev/null 2>&1 \
      && cmake --build "$tmpdir/build" --parallel 1 >/dev/null 2>&1; then
    rm -rf "$tmpdir"
    print_check "Qt 6 compiler/ABI link test" ok "$BUILD_CXX + installed Qt6"
    return 0
  fi

  rm -rf "$tmpdir"
  print_check "Qt 6 compiler/ABI link test" missing "$BUILD_CXX cannot link against installed Qt6"
  append_missing "Qt/compiler ABI compatibility"
  CHECK_FAILURES=$((CHECK_FAILURES + 1))
  return 1
}

scan_dependencies() {
  MISSING_DEPS=
  CHECK_FAILURES=0
  BUILD_CC=
  BUILD_CXX=
  BUILD_MAKE=
  AUX_GCC=
  AUX_GXX=
  PKG_CONFIG_CMD=

  echo "Dependency audit:"

  cmake_path=$(find_first_command cmake || true)
  if [ -n "$cmake_path" ]; then
    cmake_ver=$(cmake --version 2>/dev/null | sed -n '1s/.*version[[:space:]]*//p')
    if [ -n "$cmake_ver" ] && version_ge "$cmake_ver" 3.20; then
      print_check "CMake >= 3.20" ok "v$cmake_ver ($cmake_path)"
    else
      print_check "CMake >= 3.20" missing "found ${cmake_ver:-unknown version}"
      append_missing "cmake >= 3.20"
      CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
  else
    print_check "CMake >= 3.20" missing "not found"
    append_missing cmake
    CHECK_FAILURES=$((CHECK_FAILURES + 1))
  fi

  PKG_CONFIG_CMD=$(find_first_command pkg-config pkgconf || true)
  if [ -n "$PKG_CONFIG_CMD" ]; then
    print_check "pkg-config/pkgconf" ok "$PKG_CONFIG_CMD"
  else
    print_check "pkg-config/pkgconf" missing "not found"
    append_missing "pkg-config/pkgconf"
    CHECK_FAILURES=$((CHECK_FAILURES + 1))
  fi

  if [ "$OS_FAMILY" = freebsd ]; then
    # FreeBSD's packaged Qt is built for the system Clang/libc++ ABI.  Build the
    # Qt application with the system compiler instead of forcing GCC/libstdc++.
    BUILD_CC=$(find_first_command /usr/bin/cc cc clang || true)
    BUILD_CXX=$(find_first_command /usr/bin/c++ c++ clang++ || true)
    if [ -n "$BUILD_CC" ]; then
      print_check "FreeBSD Qt-compatible C compiler" ok "$BUILD_CC"
    else
      print_check "FreeBSD Qt-compatible C compiler" missing "cc/clang not found"
      append_missing "FreeBSD Clang C compiler"
      CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
    if [ -n "$BUILD_CXX" ]; then
      print_check "FreeBSD Qt-compatible C++ compiler" ok "$BUILD_CXX"
    else
      print_check "FreeBSD Qt-compatible C++ compiler" missing "c++/clang++ not found"
      append_missing "FreeBSD Clang C++ compiler"
      CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi

    # GCC/G++ remain explicit project prerequisites as requested, but are not
    # forced onto a Qt package that was compiled with Clang/libc++.
    if find_freebsd_gnu_pair; then
      print_check "GCC auxiliary compiler" ok "$AUX_GCC"
      print_check "G++ auxiliary compiler" ok "$AUX_GXX"
    else
      print_check "GCC auxiliary compiler" missing "FreeBSD GNU compiler pair not found"
      print_check "G++ auxiliary compiler" missing "FreeBSD GNU compiler pair not found"
      append_missing gcc
      append_missing g++
      CHECK_FAILURES=$((CHECK_FAILURES + 2))
    fi

    BUILD_MAKE=$(find_first_command gmake || true)
    if [ -n "$BUILD_MAKE" ]; then
      print_check "GNU Make (gmake)" ok "$BUILD_MAKE"
    else
      print_check "GNU Make (gmake)" missing "not found"
      append_missing gmake
      CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
  else
    BUILD_CC=$(find_first_command gcc || true)
    BUILD_CXX=$(find_first_command g++ || true)
    BUILD_MAKE=$(find_first_command make gmake || true)
    if [ -n "$BUILD_CC" ]; then print_check "GCC C compiler" ok "$BUILD_CC"; else print_check "GCC C compiler" missing "not found"; append_missing gcc; CHECK_FAILURES=$((CHECK_FAILURES + 1)); fi
    if [ -n "$BUILD_CXX" ]; then print_check "G++ C++ compiler" ok "$BUILD_CXX"; else print_check "G++ C++ compiler" missing "not found"; append_missing g++; CHECK_FAILURES=$((CHECK_FAILURES + 1)); fi
    if [ -n "$BUILD_MAKE" ]; then print_check "GNU Make" ok "$BUILD_MAKE"; else print_check "GNU Make" missing "not found"; append_missing make; CHECK_FAILURES=$((CHECK_FAILURES + 1)); fi
  fi

  check_pkg_module Qt6Core    "Qt 6 Core development"    "Qt6 Core" || true
  check_pkg_module Qt6Gui     "Qt 6 Gui development"     "Qt6 Gui" || true
  check_pkg_module Qt6Widgets "Qt 6 Widgets development" "Qt6 Widgets" || true
  check_pkg_module Qt6Network "Qt 6 Network development" "Qt6 Network" || true
  check_pkg_module Qt6Multimedia "Qt 6 Multimedia development (OSCAR voice)" "Qt6 Multimedia" || true
  check_pkg_module libsodium  "libsodium development"    libsodium || true
  check_pkg_module ncursesw   "ncursesw development"     ncursesw || true
  check_pkg_module xkbcommon  "xkbcommon development"    xkbcommon || true

  check_command_dep "Git" git git || true
  check_command_dep "HTTP download helper" curl curl || true
  check_command_dep "Notification sound player" paplay paplay || true
  check_command_dep "WaffleHouse-Client media backend" mpv mpv || true
  check_command_dep "WaffleHouse-Client FFmpeg tools" ffmpeg ffmpeg || true
  if [ "$OS_FAMILY" = linux ]; then
    check_pkg_module alsa     "ALSA development"          alsa || true
    check_pkg_module openssl  "OpenSSL development"       openssl || true
    check_pkg_module uuid     "libuuid development"       uuid || true
    check_command_dep "pactl audio-route helper" pactl pactl || true
  else
    check_pkg_module portaudio-2.0 "PortAudio development" portaudio || true
    check_pkg_module opus          "Opus development"      opus || true
    check_pkg_module libbcg729     "G.729/bcg729 development" bcg729 || true
    check_pkg_module uuid          "libuuid development"   uuid || true
  fi

  if [ -n "$BUILD_CXX" ]; then compiler_smoke_test || true; fi
  if [ -n "$BUILD_CXX" ] && [ -n "$BUILD_MAKE" ] && [ -n "$PKG_CONFIG_CMD" ]; then
    qt_abi_smoke_test || true
  fi

  echo
}

prepare_root_helper() {
  purpose=${1:-install missing dependencies}
  [ -z "$ROOT_METHOD" ] || return 0
  if [ "$DRY_RUN" -eq 1 ]; then ROOT_METHOD=dry-run; return 0; fi
  if [ "$(id -u)" -eq 0 ]; then ROOT_METHOD=root; return 0; fi
  if command -v sudo >/dev/null 2>&1; then ROOT_METHOD=sudo; echo "Administrator privileges are required to $purpose."; sudo -v; return 0; fi
  if command -v doas >/dev/null 2>&1; then ROOT_METHOD=doas; echo "Administrator privileges are required to $purpose."; doas true; return 0; fi
  if command -v su >/dev/null 2>&1; then ROOT_METHOD=su; echo "Administrator privileges are required to $purpose; su will be used."; return 0; fi
  echo "Cannot $purpose: no sudo, doas, su, or root session is available." >&2
  return 1
}

run_as_root() {
  prepare_root_helper
  if [ "$ROOT_METHOD" = dry-run ]; then
    printf '  [dry-run] privileged:'; for arg in "$@"; do printf ' %s' "$arg"; done; echo
    return 0
  fi
  case "$ROOT_METHOD" in
    root) "$@" ;;
    sudo) sudo "$@" ;;
    doas) doas "$@" ;;
    su)
      cmd=
      for arg in "$@"; do escaped=$(printf '%s' "$arg" | sed "s/'/'\\\\''/g"); cmd="$cmd '$escaped'"; done
      if [ "$OS_FAMILY" = freebsd ]; then su -m root -c "$cmd"; else su root -c "$cmd"; fi
      ;;
    *) echo "Root privilege method was not initialized." >&2; exit 1 ;;
  esac
}

audio_warn() {
  echo "  [WARN] $*"
}

audio_info() {
  echo "  [INFO] $*"
}

run_audio_preflight() {
  echo
  echo "============================================================"
  echo " WAFFLEHOUSE-CLIENT AUDIO PREFLIGHT"
  echo "============================================================"
  echo "This check never rewrites mixer settings, HDA pin mappings, loader hints,"
  echo "or PulseAudio/PipeWire configuration. It only reports potential problems."

  if [ "$OS_FAMILY" = freebsd ]; then
    echo "  Platform: FreeBSD OSS / snd_hda"

    if [ -r /dev/sndstat ]; then
      sndstat_out=$(cat /dev/sndstat 2>/dev/null || true)
      echo "  FreeBSD PCM devices:"
      printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | sed 's/^/    /' || true
      play_count=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((play|play/rec)\)' || true)
      rec_count=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((rec|play/rec)\)' || true)
      case "$play_count" in ''|*[!0-9]*) play_count=0 ;; esac
      case "$rec_count" in ''|*[!0-9]*) rec_count=0 ;; esac
      [ "$play_count" -gt 0 ] || audio_warn "No FreeBSD playback PCM device is visible."
      if [ "$rec_count" -eq 0 ]; then
        audio_warn "No FreeBSD recording/capture PCM device is visible."
        audio_warn "PJSIP calls may fail with PJMEDIA_EAUD_NODEFDEV when it opens capture+playback."
      fi
    else
      audio_warn "/dev/sndstat is not readable; unable to inspect FreeBSD PCM devices."
    fi

    default_unit=$(sysctl -n hw.snd.default_unit 2>/dev/null || true)
    default_auto=$(sysctl -n hw.snd.default_auto 2>/dev/null || true)
    [ -n "$default_unit" ] && audio_info "FreeBSD default PCM unit: pcm$default_unit"
    [ -n "$default_auto" ] && audio_info "FreeBSD automatic default-device policy (hw.snd.default_auto): $default_auto"
    if command -v mixer >/dev/null 2>&1 && [ -n "$default_unit" ]; then
      recsrc=$(mixer -d "$default_unit" -s 2>/dev/null || true)
      [ -n "$recsrc" ] && audio_info "FreeBSD active recording source(s): $recsrc"
    fi
    if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
      audio_info "PulseAudio compatibility detected on FreeBSD; the r14 compatibility layer will use it as the preferred live route-change watcher."
    else
      audio_info "the r14 compatibility layer will use native FreeBSD OSS/snd_hda state for automatic audio recovery."
    fi

    if [ -n "${sndstat_out:-}" ]; then
      rec_only=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -E '\(rec\)' || true)
      capture_total=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((rec|play/rec)\)' || true)
      case "$capture_total" in ''|*[!0-9]*) capture_total=0 ;; esac
      if [ "$capture_total" -gt 1 ] && [ -n "$rec_only" ]; then
        audio_warn "Multiple capture-capable PCM devices detected while the default is pcm${default_unit:-?}."
        audio_warn "Applications that use the default capture device may select the internal microphone instead of a dedicated headset/external mic:"
        printf '%s\n' "$rec_only" | sed 's/^/         /'
      fi
    fi

    codec_desc=$(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdacc\.[0-9][0-9]*\.%desc: / /p' | sed 's/^ *//' | paste -sd ';' - 2>/dev/null || true)
    [ -n "$codec_desc" ] && audio_info "HDA codec(s): $codec_desc"

    hda_errors=$(dmesg 2>/dev/null | grep -Ei 'hdaa_audio_as_parse|wrong direction|duplicate pin|disabling association' | tail -20 || true)
    if [ -n "$hda_errors" ]; then
      audio_warn "snd_hda association errors were found in the kernel log:"
      printf '%s\n' "$hda_errors" | sed 's/^/         /'
    fi

    pin_overrides=$(sysctl -a 2>/dev/null | awk '
      /^dev\.hdaa\.[0-9]+\.nid[0-9]+_original:/ {
        key=$1; sub(/_original:$/, "", key); val=$0; sub(/^[^:]*:[[:space:]]*/, "", val); orig[key]=val
      }
      /^dev\.hdaa\.[0-9]+\.nid[0-9]+_config:/ {
        key=$1; sub(/_config:$/, "", key); val=$0; sub(/^[^:]*:[[:space:]]*/, "", val); cfg[key]=val
      }
      END { for (key in orig) if ((key in cfg) && orig[key] != cfg[key]) print key ": original=" orig[key] " | configured=" cfg[key] }
    ' || true)
    if [ -n "$pin_overrides" ]; then
      audio_warn "HDA runtime pin configuration differs from codec original values:"
      printf '%s\n' "$pin_overrides" | sort | sed 's/^/         /'
    fi

    boot_hints=
    for hint_file in /boot/device.hints /boot/loader.conf /boot/loader.conf.local; do
      if [ -r "$hint_file" ]; then
        found=$(grep -HnE '^[[:space:]]*hint\.(hdac|hdaa)\..*nid[0-9]+\.config' "$hint_file" 2>/dev/null || true)
        [ -z "$found" ] || boot_hints="${boot_hints}${boot_hints:+
}$found"
      fi
    done
    if [ -n "$boot_hints" ]; then
      audio_warn "Persistent HDA pin overrides were found (review if capture/playback associations look wrong):"
      printf '%s\n' "$boot_hints" | sed 's/^/         /'
    fi

    hda_detector="$ROOT_DIR/scripts/freebsd-hda-output-detect.awk"
    if [ -r "$hda_detector" ]; then
      hda_dump=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-r14-preflight-hda.XXXXXX")
      sysctl -a 2>/dev/null > "$hda_dump" || true
      hda_candidates=$(awk -f "$hda_detector" "$hda_dump" 2>/dev/null || true)
      rm -f "$hda_dump"
      if [ -n "$hda_candidates" ]; then
        old_ifs=$IFS
        tab=$(printf '\t')
        printf '%s\n' "$hda_candidates" | while IFS="$tab" read -r au spnid target sas ssq hpnid has hsq osas ossq ohas ohsq; do
          [ -n "$au" ] || continue
          if [ "$sas" = "$target" ] && [ "$ssq" = 0 ] && [ "$has" = "$target" ] && [ "$hsq" = 15 ]; then
            audio_info "hdaa${au}: simple laptop Speaker nid${spnid} + Headphones nid${hpnid} already share as=${target}; headphone seq=15 is active."
          else
            audio_warn "hdaa${au}: simple laptop Speaker/Headphones pins are split or missing headphone seq=15 (Speaker as=${sas}/seq=${ssq}, Headphones as=${has}/seq=${hsq})."
            audio_info "normal builds can test the S.I.P.H.E.R. r14 conservative repair using firmware Speaker association ${target}; --audio-diagnose remains read-only."
          fi
        done
        IFS=$old_ifs
      fi
    fi

    if printf '%s\n' "$codec_desc" | grep -qi 'ALC236'; then
      audio_info "Realtek ALC236 detected. Combo-jack headset mic routing can be OEM-specific."
      for hdaa_unit in $(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdaa\.\([0-9][0-9]*\)\.nid[0-9][0-9]*: pin: Mic.*/\1/p' | sort -u); do
        init_clear=$(sysctl -n "dev.hdaa.${hdaa_unit}.init_clear" 2>/dev/null || true)
        [ -z "$init_clear" ] || audio_info "hdaa${hdaa_unit} init_clear=$init_clear"
        mic_nids=$(sysctl -a 2>/dev/null | sed -n "s/^dev\.hdaa\.${hdaa_unit}\.nid\([0-9][0-9]*\): pin: Mic.*/\1/p" | sort -n)
        for mic_nid in $mic_nids; do
          mic_info=$(sysctl "dev.hdaa.${hdaa_unit}.nid${mic_nid}" 2>/dev/null || true)
          printf '%s\n' "$mic_info" | grep -q 'conn=Jack' || continue
          pin_control=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin control: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
          pin_cfg=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin config: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
          audio_info "ALC236 jack mic: hdaa${hdaa_unit} nid${mic_nid} config=${pin_cfg:-unknown} pin-control=${pin_control:-unknown}"
          if [ "$pin_control" = "0x00000025" ] || [ "$pin_control" = "0x25" ]; then
            audio_warn "ALC236 jack mic is using VREF100 (pin control 0x25). On Lenovo subsystem 0x17aa390b we verified voice capture only after init_clear=1 + ivref80 produced pin control 0x24."
            audio_warn "Diagnostic only: do not auto-apply this to unrelated hardware; compare a known-working OS or OEM codec routing first."
          elif [ "$pin_control" = "0x00000024" ] || [ "$pin_control" = "0x24" ]; then
            audio_info "ALC236 jack mic is using VREF80 (0x24), matching the known-working Project-2501 headset-mic configuration."
          fi
        done
      done
    fi
  else
    echo "  Platform: Linux audio preflight"
    if command -v aplay >/dev/null 2>&1; then
      play_lines=$(aplay -l 2>/dev/null | grep '^card ' || true)
      [ -n "$play_lines" ] || audio_warn "ALSA reports no playback hardware through aplay -l."
    else
      audio_info "aplay is unavailable; skipping direct ALSA playback enumeration."
    fi
    if command -v arecord >/dev/null 2>&1; then
      rec_lines=$(arecord -l 2>/dev/null | grep '^card ' || true)
      [ -n "$rec_lines" ] || audio_warn "ALSA reports no capture hardware through arecord -l."
    else
      audio_info "arecord is unavailable; skipping direct ALSA capture enumeration."
    fi
  fi

  if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
    pa_sink=$(pactl get-default-sink 2>/dev/null || true)
    pa_source=$(pactl get-default-source 2>/dev/null || true)
    [ -n "$pa_sink" ] && audio_info "PulseAudio default sink: $pa_sink"
    [ -n "$pa_source" ] && audio_info "PulseAudio default source: $pa_source"
    sink_count=$(pactl list short sinks 2>/dev/null | wc -l | tr -d ' ')
    source_count=$(pactl list short sources 2>/dev/null | grep -v '\.monitor[[:space:]]' | wc -l | tr -d ' ')
    case "$sink_count" in ''|*[!0-9]*) sink_count=0 ;; esac
    case "$source_count" in ''|*[!0-9]*) source_count=0 ;; esac
    [ "$sink_count" -gt 1 ] && audio_info "PulseAudio exposes multiple sinks ($sink_count); applications may keep a previously opened route."
    [ "$source_count" -gt 1 ] && audio_info "PulseAudio exposes multiple capture sources ($source_count); verify the intended headset/internal source."
  fi

  echo "  Preflight result: inspection complete."
}



freebsd_audio_pcm_counts() {
  if [ ! -r /dev/sndstat ]; then
    echo "0 0"
    return 0
  fi
  _fa_snd=$(cat /dev/sndstat 2>/dev/null || true)
  _fa_play=$(printf '%s\n' "$_fa_snd" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((play|play/rec)\)' || true)
  _fa_rec=$(printf '%s\n' "$_fa_snd" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((rec|play/rec)\)' || true)
  case "$_fa_play" in ''|*[!0-9]*) _fa_play=0 ;; esac
  case "$_fa_rec" in ''|*[!0-9]*) _fa_rec=0 ;; esac
  echo "$_fa_play $_fa_rec"
}

freebsd_pause_pulseaudio_for_hda() {
  FREEBSD_PULSE_RESTART=0
  command -v pactl >/dev/null 2>&1 || return 0
  pactl info >/dev/null 2>&1 || return 0
  _fa_server=$(pactl info 2>/dev/null | sed -n 's/^Server Name:[[:space:]]*//p' | head -1)
  case "$_fa_server" in
    pulseaudio|PulseAudio|*pulseaudio*)
      if command -v pulseaudio >/dev/null 2>&1; then
        FREEBSD_PULSE_RESTART=1
        audio_info "Temporarily stopping the user PulseAudio daemon so snd_hda can rebuild PCM devices safely."
        pulseaudio -k >/dev/null 2>&1 || true
        sleep 1
      fi
      ;;
  esac
}

freebsd_resume_pulseaudio_after_hda() {
  [ "${FREEBSD_PULSE_RESTART:-0}" -eq 1 ] || return 0
  if command -v pulseaudio >/dev/null 2>&1; then
    pulseaudio --start >/dev/null 2>&1 || audio_warn "PulseAudio did not restart automatically; start it with: pulseaudio --start"
  fi
  FREEBSD_PULSE_RESTART=0
}

freebsd_hda_hint_identity() {
  _fa_unit=$1
  _fa_hdacc=$(sysctl -n "dev.hdaa.${_fa_unit}.%parent" 2>/dev/null || true)
  case "$_fa_hdacc" in hdacc[0-9]*) ;; *) return 1 ;; esac
  _fa_hdacc_num=${_fa_hdacc#hdacc}
  _fa_hdac=$(sysctl -n "dev.hdacc.${_fa_hdacc_num}.%parent" 2>/dev/null || true)
  case "$_fa_hdac" in hdac[0-9]*) ;; *) return 1 ;; esac
  _fa_hdac_num=${_fa_hdac#hdac}
  _fa_loc=$(sysctl -n "dev.hdacc.${_fa_hdacc_num}.%location" 2>/dev/null || true)
  _fa_cad=$(printf '%s\n' "$_fa_loc" | sed -n 's/.*cad=\([0-9][0-9]*\).*/\1/p')
  [ -n "$_fa_cad" ] || return 1
  echo "$_fa_hdac_num $_fa_cad"
}

freebsd_hda_has_custom_hint() {
  _fa_hdac=$1
  _fa_cad=$2
  _fa_hdaa=$3
  _fa_nid=$4
  for _fa_hint_file in /boot/device.hints /boot/loader.conf /boot/loader.conf.local; do
    [ -r "$_fa_hint_file" ] || continue
    if [ "$_fa_hint_file" = /boot/device.hints ]; then
      # Ignore only the exact generated r14 managed blocks. Any additional
      # user-authored hint for either candidate pin still disables automation.
      _fa_hint_scan=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-r14-hint-scan.XXXXXX")
      awk -v s_begin="# BEGIN SIPHER R14 FREEBSD AUDIO hdaa${_fa_hdaa}" \
          -v s_end="# END SIPHER R14 FREEBSD AUDIO hdaa${_fa_hdaa}" \
          -v w_begin="# BEGIN WAFFLEHOUSE SIPHER-R14 FREEBSD AUDIO hdaa${_fa_hdaa}" \
          -v w_end="# END WAFFLEHOUSE SIPHER-R14 FREEBSD AUDIO hdaa${_fa_hdaa}" '
        $0 == s_begin || $0 == w_begin {skip=1; next}
        $0 == s_end || $0 == w_end {skip=0; next}
        !skip {print}
      ' "$_fa_hint_file" > "$_fa_hint_scan"
      if grep -Eq "^[[:space:]]*hint\\.hdac\\.${_fa_hdac}\\.cad${_fa_cad}\\.nid${_fa_nid}\\.config=" "$_fa_hint_scan" || \
         grep -Eq "^[[:space:]]*hint\\.hdaa\\.${_fa_hdaa}\\.nid${_fa_nid}\\.config=" "$_fa_hint_scan"; then
        rm -f "$_fa_hint_scan"
        return 0
      fi
      rm -f "$_fa_hint_scan"
    else
      grep -Eq "^[[:space:]]*hint\\.hdac\\.${_fa_hdac}\\.cad${_fa_cad}\\.nid${_fa_nid}\\.config=" "$_fa_hint_file" && return 0
      grep -Eq "^[[:space:]]*hint\\.hdaa\\.${_fa_hdaa}\\.nid${_fa_nid}\\.config=" "$_fa_hint_file" && return 0
    fi
  done
  return 1
}
freebsd_persist_headphone_hint() {
  _fa_hdaa=$1
  _fa_hdac=$2
  _fa_cad=$3
  _fa_hp_nid=$4
  _fa_target_as=$5
  _fa_marker_begin="# BEGIN WAFFLEHOUSE SIPHER-R14 FREEBSD AUDIO hdaa${_fa_hdaa}"
  _fa_marker_end="# END WAFFLEHOUSE SIPHER-R14 FREEBSD AUDIO hdaa${_fa_hdaa}"

  prepare_root_helper "persistent FreeBSD snd_hda headphone compatibility"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] back up /boot/device.hints"
    echo "  [dry-run] persist hint.hdac.${_fa_hdac}.cad${_fa_cad}.nid${_fa_hp_nid}.config=\"as=${_fa_target_as} seq=15 device=Headphones\""
    return 0
  fi

  _fa_stamp=$(date +%Y%m%d-%H%M%S)
  if [ -e /boot/device.hints ]; then
    run_as_root cp -p /boot/device.hints "/boot/device.hints.wafflehouse-r14-backup-${_fa_stamp}"
  fi

  _fa_tmp=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-r14-device.hints.XXXXXX")
  if [ -r /boot/device.hints ]; then
    awk -v begin="$_fa_marker_begin" -v end="$_fa_marker_end" '
      $0 == begin {skip=1; next}
      $0 == end {skip=0; next}
      !skip {print}
    ' /boot/device.hints > "$_fa_tmp"
  fi
  {
    echo ""
    echo "$_fa_marker_begin"
    echo "# Auto-detected simple laptop Speaker + Headphones layout."
    echo "# seq=15 makes the headphone pin duplicate/auto-mute the first output in the association."
    echo "hint.hdac.${_fa_hdac}.cad${_fa_cad}.nid${_fa_hp_nid}.config=\"as=${_fa_target_as} seq=15 device=Headphones\""
    echo "$_fa_marker_end"
  } >> "$_fa_tmp"
  run_as_root install -m 0644 "$_fa_tmp" /boot/device.hints
  rm -f "$_fa_tmp"
  audio_info "Persisted FreeBSD headphone auto-switch hint; backup: /boot/device.hints.wafflehouse-r14-backup-${_fa_stamp}"
}

configure_freebsd_hda_output_compat() {
  [ "$OS_FAMILY" = freebsd ] || return 0
  _fa_detector="$ROOT_DIR/scripts/freebsd-hda-output-detect.awk"
  [ -r "$_fa_detector" ] || { audio_warn "FreeBSD HDA compatibility detector is missing: $_fa_detector"; return 0; }

  _fa_dump=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-r14-hda.XXXXXX")
  _fa_candidates_file=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-r14-hda-candidates.XXXXXX")
  sysctl -a 2>/dev/null > "$_fa_dump" || { rm -f "$_fa_dump" "$_fa_candidates_file"; return 0; }
  awk -f "$_fa_detector" "$_fa_dump" > "$_fa_candidates_file" 2>/dev/null || true
  rm -f "$_fa_dump"
  [ -s "$_fa_candidates_file" ] || {
    rm -f "$_fa_candidates_file"
    audio_info "No high-confidence simple FreeBSD laptop Speaker/Headphones association repair is needed."
    return 0
  }

  _fa_tab=$(printf '\t')
  while IFS="$_fa_tab" read -r _fa_unit _fa_spnid _fa_target _fa_sp_as _fa_sp_seq _fa_hpnid _fa_hp_as _fa_hp_seq _fa_osp_as _fa_osp_seq _fa_ohp_as _fa_ohp_seq; do
    [ -n "$_fa_unit" ] || continue

    _fa_ident=$(freebsd_hda_hint_identity "$_fa_unit" 2>/dev/null || true)
    if [ -z "$_fa_ident" ]; then
      audio_warn "hdaa${_fa_unit}: simple Speaker/Headphones mismatch detected, but hdac/cad identity could not be resolved; leaving it unchanged."
      continue
    fi
    set -- $_fa_ident
    _fa_hdac=$1
    _fa_cad=$2

    _fa_managed=0
    if [ -r /boot/device.hints ] && {
      grep -Fq "# BEGIN SIPHER R14 FREEBSD AUDIO hdaa${_fa_unit}" /boot/device.hints ||
      grep -Fq "# BEGIN WAFFLEHOUSE SIPHER-R14 FREEBSD AUDIO hdaa${_fa_unit}" /boot/device.hints;
    }; then
      _fa_managed=1
    fi
    if freebsd_hda_has_custom_hint "$_fa_hdac" "$_fa_cad" "$_fa_unit" "$_fa_spnid" || \
       freebsd_hda_has_custom_hint "$_fa_hdac" "$_fa_cad" "$_fa_unit" "$_fa_hpnid"; then
      audio_warn "hdaa${_fa_unit}: existing user HDA pin hint found for Speaker/Headphones; WaffleHouse will not override custom audio policy."
      continue
    fi

    _fa_need_runtime=0
    [ "$_fa_sp_as" = "$_fa_target" ] && [ "$_fa_sp_seq" = 0 ] || _fa_need_runtime=1
    [ "$_fa_hp_as" = "$_fa_target" ] && [ "$_fa_hp_seq" = 15 ] || _fa_need_runtime=1

    _fa_need_persist=0
    [ "$_fa_ohp_as" = "$_fa_target" ] && [ "$_fa_ohp_seq" = 15 ] || _fa_need_persist=1
    [ "$_fa_managed" -eq 1 ] && _fa_need_persist=0

    if [ "$_fa_need_runtime" -eq 0 ] && [ "$_fa_need_persist" -eq 0 ]; then
      audio_info "hdaa${_fa_unit}: Speaker nid${_fa_spnid} + Headphones nid${_fa_hpnid} already use association ${_fa_target} with headphone seq=15."
      continue
    fi

    echo
    echo "==> FreeBSD laptop audio compatibility: hdaa${_fa_unit}"
    echo "    Speaker:    nid${_fa_spnid} current as=${_fa_sp_as} seq=${_fa_sp_seq}; firmware target as=${_fa_target} seq=0"
    echo "    Headphones: nid${_fa_hpnid} current as=${_fa_hp_as} seq=${_fa_hp_seq}; target as=${_fa_target} seq=15"
    echo "    Layout qualifies for unattended repair: exactly one fixed Speaker + one jack Headphones, no Line-out."

    if [ "$DRY_RUN" -eq 1 ]; then
      echo "  [dry-run] temporarily rebuild hdaa${_fa_unit} with Speaker as=${_fa_target}/seq=0 and Headphones as=${_fa_target}/seq=15"
      echo "  [dry-run] validate playback/capture PCM availability and roll back on regression"
      if [ "$_fa_need_persist" -eq 1 ]; then
        freebsd_persist_headphone_hint "$_fa_unit" "$_fa_hdac" "$_fa_cad" "$_fa_hpnid" "$_fa_target" || true
      fi
      continue
    fi

    if ! prepare_root_helper "FreeBSD snd_hda laptop speaker/headphone compatibility"; then
      audio_warn "hdaa${_fa_unit}: root access unavailable; skipping automatic audio repair. Re-run with privileges available or use --no-audio-fix."
      continue
    fi
    set -- $(freebsd_audio_pcm_counts)
    _fa_pre_play=$1
    _fa_pre_rec=$2

    freebsd_pause_pulseaudio_for_hda

    _fa_apply_ok=1
    run_as_root sysctl "dev.hdaa.${_fa_unit}.nid${_fa_spnid}_config=as=${_fa_target} seq=0" >/dev/null 2>&1 || _fa_apply_ok=0
    run_as_root sysctl "dev.hdaa.${_fa_unit}.nid${_fa_hpnid}_config=as=${_fa_target} seq=15" >/dev/null 2>&1 || _fa_apply_ok=0
    run_as_root sysctl "dev.hdaa.${_fa_unit}.reconfig=1" >/dev/null 2>&1 || _fa_apply_ok=0
    sleep 1

    _fa_sp_now=$(sysctl -n "dev.hdaa.${_fa_unit}.nid${_fa_spnid}_config" 2>/dev/null || true)
    _fa_hp_now=$(sysctl -n "dev.hdaa.${_fa_unit}.nid${_fa_hpnid}_config" 2>/dev/null || true)
    case "$_fa_sp_now" in *"as=${_fa_target} seq=0 device=Speaker"*) ;; *) _fa_apply_ok=0 ;; esac
    case "$_fa_hp_now" in *"as=${_fa_target} seq=15 device=Headphones"*) ;; *) _fa_apply_ok=0 ;; esac

    set -- $(freebsd_audio_pcm_counts)
    _fa_post_play=$1
    _fa_post_rec=$2
    [ "$_fa_post_play" -gt 0 ] || _fa_apply_ok=0
    if [ "$_fa_pre_rec" -gt 0 ] && [ "$_fa_post_rec" -eq 0 ]; then _fa_apply_ok=0; fi

    if [ "$_fa_apply_ok" -ne 1 ]; then
      audio_warn "hdaa${_fa_unit}: validation failed; rolling the runtime pin associations back immediately."
      run_as_root sysctl "dev.hdaa.${_fa_unit}.nid${_fa_spnid}_config=as=${_fa_sp_as} seq=${_fa_sp_seq}" >/dev/null 2>&1 || true
      run_as_root sysctl "dev.hdaa.${_fa_unit}.nid${_fa_hpnid}_config=as=${_fa_hp_as} seq=${_fa_hp_seq}" >/dev/null 2>&1 || true
      run_as_root sysctl "dev.hdaa.${_fa_unit}.reconfig=1" >/dev/null 2>&1 || true
      sleep 1
      freebsd_resume_pulseaudio_after_hda
      audio_warn "hdaa${_fa_unit}: no persistent changes were written. WaffleHouse-Client will continue using the host's existing audio configuration."
      continue
    fi

    audio_info "hdaa${_fa_unit}: runtime repair validated (${_fa_post_play} playback-capable PCM, ${_fa_post_rec} capture-capable PCM)."
    if [ "$_fa_need_persist" -eq 1 ]; then
      if ! freebsd_persist_headphone_hint "$_fa_unit" "$_fa_hdac" "$_fa_cad" "$_fa_hpnid" "$_fa_target"; then
        audio_warn "hdaa${_fa_unit}: runtime repair works, but persistence failed; audio may revert after reboot."
      fi
    fi
    freebsd_resume_pulseaudio_after_hda
  done < "$_fa_candidates_file"
  rm -f "$_fa_candidates_file"
}

configure_audio_fixes() {
  [ "$AUTO_AUDIO_FIX" -eq 1 ] || { audio_info "Automatic audio repair disabled (--no-audio-fix)."; return 0; }
  [ "$OS_FAMILY" = freebsd ] || return 0
  configure_freebsd_hda_output_compat
  configure_known_alc236_audio_fix
}

configure_known_alc236_audio_fix() {
  [ "$AUTO_AUDIO_FIX" -eq 1 ] || return 0
  [ "$OS_FAMILY" = freebsd ] || return 0

  codec_desc=$(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdacc\.[0-9][0-9]*\.%desc: / /p' | sed 's/^ *//' | paste -sd ';' - 2>/dev/null || true)
  printf '%s\n' "$codec_desc" | grep -qi 'ALC236' || return 0

  # This is intentionally a narrow hardware fingerprint derived from the
  # Project-2501/Lenovo ALC236 codec we verified against Linux. Unknown ALC236
  # layouts are diagnosed but never rewritten automatically.
  matched_unit=
  for unit in $(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdaa\.\([0-9][0-9]*\)\.nid25_original:.*/\1/p' | sort -u); do
    n18=$(sysctl -n "dev.hdaa.${unit}.nid18_original" 2>/dev/null || true)
    n20=$(sysctl -n "dev.hdaa.${unit}.nid20_original" 2>/dev/null || true)
    n25=$(sysctl -n "dev.hdaa.${unit}.nid25_original" 2>/dev/null || true)
    n33=$(sysctl -n "dev.hdaa.${unit}.nid33_original" 2>/dev/null || true)
    case "$n18" in *0x90a60130*) ;; *) continue ;; esac
    case "$n20" in *0x90170120*) ;; *) continue ;; esac
    case "$n25" in *0x04a11040*) ;; *) continue ;; esac
    case "$n33" in *0x04211010*) ;; *) continue ;; esac
    matched_unit=$unit
    break
  done
  [ -n "$matched_unit" ] || {
    audio_info "ALC236 detected, but it does not match the verified Project-2501 pin fingerprint; no automatic HDA changes will be made."
    return 0
  }

  unit=$matched_unit
  mic_info=$(sysctl "dev.hdaa.${unit}.nid25" 2>/dev/null || true)
  pin_control=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin control: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
  init_clear=$(sysctl -n "dev.hdaa.${unit}.init_clear" 2>/dev/null || true)
  config=$(sysctl -n "dev.hdaa.${unit}.config" 2>/dev/null || true)

  known_bad_hints=0
  if [ -r /boot/device.hints ] && \
     grep -Eq '^[[:space:]]*hint\.hdac\.0\.cad0\.nid18\.config="as=1 seq=0 device=Speaker"' /boot/device.hints && \
     grep -Eq '^[[:space:]]*hint\.hdac\.0\.cad0\.nid21\.config="as=1 seq=1 device=Headphones"' /boot/device.hints && \
     grep -Eq '^[[:space:]]*hint\.hdac\.0\.cad0\.nid25\.config="as=1 seq=2 device=Mic"' /boot/device.hints; then
    known_bad_hints=1
  fi

  needs_fix=0
  [ "$pin_control" = "0x00000024" ] || [ "$pin_control" = "0x24" ] || needs_fix=1
  [ "$init_clear" = "1" ] || needs_fix=1
  case ",$config," in *,ivref80,*) ;; *) needs_fix=1 ;; esac

  persist_ok=0
  if [ -r /etc/sysctl.conf ] && { grep -q '^# BEGIN TRUNKMONKEY ALC236 HEADSET MIC$' /etc/sysctl.conf || grep -q '^# BEGIN WAFFLEHOUSE ALC236 HEADSET MIC$' /etc/sysctl.conf; } && \
     grep -q "^dev.hdaa.${unit}.init_clear=1$" /etc/sysctl.conf && \
     grep -q "^dev.hdaa.${unit}.config=forcestereo,ivref80$" /etc/sysctl.conf; then
    persist_ok=1
  fi

  [ "$needs_fix" -eq 1 ] || [ "$persist_ok" -eq 0 ] || [ "$known_bad_hints" -eq 1 ] || {
    audio_info "Verified ALC236 headset-mic repair is already active and persistent (VREF80 / pin control 0x24)."
    return 0
  }

  echo
  echo "==> Verified FreeBSD ALC236 headset-mic repair"
  echo "    Hardware fingerprint matches the Project-2501 layout."
  echo "    Current: init_clear=${init_clear:-?} config=${config:-?} pin-control=${pin_control:-?}"
  prepare_root_helper "verified FreeBSD ALC236 headset-mic repair"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] back up /etc/sysctl.conf and persist init_clear=1 + forcestereo,ivref80"
    [ "$known_bad_hints" -eq 0 ] || echo "  [dry-run] back up /boot/device.hints and disable the exact known-bad S.I.P.H.E.R.-era pin override trio"
    echo "  [dry-run] reconfigure hdaa${unit} and verify nid25 pin control 0x24"
    return 0
  fi

  stamp=$(date +%Y%m%d-%H%M%S)
  [ ! -e /etc/sysctl.conf ] || run_as_root cp -p /etc/sysctl.conf "/etc/sysctl.conf.wafflehouse-backup-$stamp"
  tmp_sysctl=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-sysctl.conf.XXXXXX")
  if [ -r /etc/sysctl.conf ]; then
    awk '
      /^# BEGIN (TRUNKMONKEY|WAFFLEHOUSE) ALC236 HEADSET MIC$/ {skip=1; next}
      /^# END (TRUNKMONKEY|WAFFLEHOUSE) ALC236 HEADSET MIC$/ {skip=0; next}
      !skip {print}
    ' /etc/sysctl.conf > "$tmp_sysctl"
  fi
  {
    echo ""
    echo "# BEGIN WAFFLEHOUSE ALC236 HEADSET MIC"
    echo "# Verified ALC236 combo-jack mic repair: VREF80, preserved across reboot."
    echo "dev.hdaa.${unit}.init_clear=1"
    echo "dev.hdaa.${unit}.config=forcestereo,ivref80"
    echo "dev.hdaa.${unit}.reconfig=1"
    echo "# END WAFFLEHOUSE ALC236 HEADSET MIC"
  } >> "$tmp_sysctl"
  run_as_root install -m 0644 "$tmp_sysctl" /etc/sysctl.conf
  rm -f "$tmp_sysctl"

  if [ "$known_bad_hints" -eq 1 ]; then
    run_as_root cp -p /boot/device.hints "/boot/device.hints.wafflehouse-backup-$stamp"
    tmp_hints=$(mktemp "${TMPDIR:-/tmp}/wafflehouse-device.hints.XXXXXX")
    awk '
      /^hint\.hdac\.0\.cad0\.nid18\.config="as=1 seq=0 device=Speaker"$/ {print "# WaffleHouse-Client disabled known-bad S.I.P.H.E.R.-era override: "$0; next}
      /^hint\.hdac\.0\.cad0\.nid21\.config="as=1 seq=1 device=Headphones"$/ {print "# WaffleHouse-Client disabled known-bad S.I.P.H.E.R.-era override: "$0; next}
      /^hint\.hdac\.0\.cad0\.nid25\.config="as=1 seq=2 device=Mic"$/ {print "# WaffleHouse-Client disabled known-bad S.I.P.H.E.R.-era override: "$0; next}
      {print}
    ' /boot/device.hints > "$tmp_hints"
    run_as_root install -m 0644 "$tmp_hints" /boot/device.hints
    rm -f "$tmp_hints"
    audio_warn "Known-bad boot HDA pin overrides were disabled. A reboot is recommended after this build so codec associations are rebuilt from the original pin map."
  fi

  freebsd_pause_pulseaudio_for_hda
  run_as_root sysctl "dev.hdaa.${unit}.init_clear=1" >/dev/null
  run_as_root sysctl "dev.hdaa.${unit}.config=forcestereo,ivref80" >/dev/null
  run_as_root sysctl "dev.hdaa.${unit}.reconfig=1" >/dev/null
  sleep 1
  mic_info=$(sysctl "dev.hdaa.${unit}.nid25" 2>/dev/null || true)
  pin_control=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin control: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
  if [ "$pin_control" = "0x00000024" ] || [ "$pin_control" = "0x24" ]; then
    audio_info "ALC236 repair verified: nid25 is now IN + VREF80 (pin control 0x24)."
  else
    audio_warn "ALC236 repair was applied but nid25 did not verify as pin control 0x24; leaving backups in place for manual review."
  fi
  freebsd_resume_pulseaudio_after_hda
}


detect_linux_package_manager() {
  if command -v apt-get >/dev/null 2>&1; then LINUX_PKG_MANAGER=apt
  elif command -v dnf >/dev/null 2>&1; then LINUX_PKG_MANAGER=dnf
  elif command -v yum >/dev/null 2>&1; then LINUX_PKG_MANAGER=yum
  elif command -v pacman >/dev/null 2>&1; then LINUX_PKG_MANAGER=pacman
  elif command -v zypper >/dev/null 2>&1; then LINUX_PKG_MANAGER=zypper
  elif [ -f /etc/slackware-version ] || command -v slackpkg >/dev/null 2>&1; then LINUX_PKG_MANAGER=slackware
  else LINUX_PKG_MANAGER=
  fi
}

install_linux_dependencies() {
  detect_linux_package_manager
  case "$LINUX_PKG_MANAGER" in
    apt)
      echo "Installing complete Debian/Ubuntu/Linux Mint dependency set..."
      run_as_root apt-get update
      run_as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential gcc g++ make cmake pkg-config \
        qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins qt6-multimedia-dev \
        libsodium-dev libncurses-dev libxkbcommon-dev ca-certificates git curl unzip \
        libasound2-dev libssl-dev uuid-dev pulseaudio-utils mpv ffmpeg
      ;;
    dnf)
      echo "Installing complete dnf dependency set..."
      run_as_root dnf install -y \
        gcc gcc-c++ make cmake pkgconf-pkg-config qt6-qtbase-devel qt6-qtmultimedia-devel \
        libsodium-devel ncurses-devel libxkbcommon-devel ca-certificates git curl unzip \
        alsa-lib-devel openssl-devel libuuid-devel pulseaudio-utils mpv ffmpeg
      ;;
    yum)
      echo "Installing complete yum dependency set..."
      run_as_root yum install -y \
        gcc gcc-c++ make cmake pkgconfig qt6-qtbase-devel qt6-qtmultimedia-devel \
        libsodium-devel ncurses-devel libxkbcommon-devel ca-certificates git curl unzip alsa-lib-devel openssl-devel libuuid-devel pulseaudio-utils mpv ffmpeg
      ;;
    pacman)
      echo "Installing complete pacman dependency set..."
      run_as_root pacman -S --needed --noconfirm \
        base-devel gcc make cmake pkgconf qt6-base qt6-multimedia libsodium ncurses libxkbcommon ca-certificates \
        git curl unzip alsa-lib openssl util-linux-libs libpulse mpv ffmpeg
      ;;
    zypper)
      echo "Installing complete zypper dependency set..."
      run_as_root zypper --non-interactive install \
        gcc gcc-c++ make cmake pkgconf-pkg-config qt6-base-devel qt6-multimedia-devel \
        libsodium-devel ncurses-devel libxkbcommon-devel ca-certificates git curl unzip \
        alsa-devel libopenssl-devel libuuid-devel pulseaudio-utils mpv ffmpeg
      ;;
    slackware)
      echo "Slackware detected. Installing available base dependencies with slackpkg..."
      if command -v slackpkg >/dev/null 2>&1; then
        for pkg in gcc gcc-g++ make cmake pkg-config libsodium ncurses libxkbcommon ca-certificates git curl unzip alsa-lib openssl util-linux pulseaudio ffmpeg python3; do
          run_as_root slackpkg -batch=on -default_answer=y install "$pkg" >/dev/null 2>&1 || true
        done
      fi
      if command -v sbopkg >/dev/null 2>&1; then
        echo "sbopkg detected; attempting SlackBuilds for Qt6/mpv where needed..."
        for pkg in qt6 mpv; do
          command -v "$pkg" >/dev/null 2>&1 || run_as_root sbopkg -B -i "$pkg" || true
        done
      else
        echo "NOTE: Slackware third-party packages are repository-dependent." >&2
        echo "      If Qt6 or mpv remain missing, install them from your configured" >&2
        echo "      SlackBuilds/repository." >&2
      fi
      ;;
    *)
      echo "No supported Linux package manager found." >&2
      echo "Supported: apt-get, dnf/yum, pacman, zypper, Slackware slackpkg/sbopkg." >&2
      exit 1
      ;;
  esac
}

install_freebsd_dependencies() {
  command -v pkg >/dev/null 2>&1 || { echo "FreeBSD pkg(8) was not found." >&2; exit 1; }
  echo "Installing complete FreeBSD dependency set..."
  run_as_root env ASSUME_ALWAYS_YES=yes pkg install -y \
    cmake pkgconf qt6-base qt6-multimedia libsodium ncurses libxkbcommon gcc gmake ca_root_nss \
    git curl unzip portaudio opus bcg729 libuuid pulseaudio mpv ffmpeg

  # FreeBSD normally supplies Clang in the base system. If this installation
  # does not, install LLVM so an ABI-compatible Clang/libc++ compiler is present.
  if ! command -v c++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    echo "FreeBSD base C++ compiler is unavailable; installing LLVM/Clang..."
    run_as_root env ASSUME_ALWAYS_YES=yes pkg install -y llvm
  fi
}


ensure_dependencies() {
  echo "==> Checking ALL build dependencies"
  scan_dependencies
  if [ "$CHECK_FAILURES" -eq 0 ]; then
    echo "All dependency checks passed."
    echo
    return 0
  fi

  echo "Missing/failed requirements:"
  oldifs=$IFS; IFS='|'
  for dep in $MISSING_DEPS; do echo "  - $dep"; done
  IFS=$oldifs
  echo

  if [ "$AUTO_DEPS" -ne 1 ]; then
    echo "Automatic dependency installation is disabled (--no-auto-deps)." >&2
    exit 1
  fi

  if [ "$OS_FAMILY" = freebsd ]; then install_freebsd_dependencies; else install_linux_dependencies; fi

  if [ "$DRY_RUN" -eq 1 ]; then
    echo "[dry-run] No packages were changed; post-install verification skipped."
    echo
    return 0
  fi

  hash -r 2>/dev/null || true
  echo
  echo "==> Re-checking ALL dependencies after package installation"
  scan_dependencies
  if [ "$CHECK_FAILURES" -ne 0 ]; then
    echo "Dependency installation completed, but the following requirements still failed:" >&2
    oldifs=$IFS; IFS='|'
    for dep in $MISSING_DEPS; do echo "  - $dep" >&2; done
    IFS=$oldifs
    echo "CMake will not run until every dependency check passes." >&2
    exit 1
  fi
  echo "All dependency checks passed after installation."
  echo
}

activate_pjsip() {
  for pcdir in "$PJSIP_PREFIX/lib/pkgconfig" "$PJSIP_PREFIX/libdata/pkgconfig"; do
    if [ -d "$pcdir" ]; then
      case ":${PKG_CONFIG_PATH:-}:" in *":$pcdir:"*) ;; *) PKG_CONFIG_PATH="$pcdir${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" ;; esac
    fi
  done
  export PKG_CONFIG_PATH
  if [ "$OS_FAMILY" = freebsd ]; then export PKG_CONFIG_ALLOW_SYSTEM_LIBS=1; fi
}

have_managed_pjsip() {
  activate_pjsip
  [ -f "$PJSIP_PREFIX/.wafflehouse-pjsip-build" ] || return 1
  grep -q -- '-v10$' "$PJSIP_PREFIX/.wafflehouse-pjsip-build" 2>/dev/null || return 1
  [ -n "$PKG_CONFIG_CMD" ] || PKG_CONFIG_CMD=$(find_first_command pkg-config pkgconf || true)
  [ -n "$PKG_CONFIG_CMD" ] || return 1
  "$PKG_CONFIG_CMD" --exists libpjproject 2>/dev/null || return 1
  [ "$("$PKG_CONFIG_CMD" --modversion libpjproject 2>/dev/null || true)" = "2.17" ] || return 1
  "$PKG_CONFIG_CMD" --libs --static libpjproject >/dev/null 2>&1 || return 1
}

ensure_pjsip() {
  activate_pjsip
  if [ "$BUILD_PJSIP" -eq 0 ] && have_managed_pjsip; then
    echo "==> Managed PJSIP 2.17 softphone dependency found: $PJSIP_PREFIX"
    return 0
  fi
  echo "==> Building managed PJSIP 2.17 softphone dependency"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] PJSIP_PREFIX=$PJSIP_PREFIX scripts/bootstrap-pjsip.sh"
    return 0
  fi
  PJSIP_PREFIX="$PJSIP_PREFIX" "$ROOT_DIR/scripts/bootstrap-pjsip.sh"
  activate_pjsip
  if ! have_managed_pjsip; then
    echo "PJSIP bootstrap completed but the managed libpjproject 2.17 installation could not be validated." >&2
    exit 1
  fi
}

run_cmd() {
  if [ "$DRY_RUN" -eq 1 ]; then printf '  [dry-run]'; for arg in "$@"; do printf ' %s' "$arg"; done; echo; else "$@"; fi
}

remove_build_tree() {
  reason=$1
  [ -d "$ROOT_DIR/build" ] || return 0
  printf '%b\n' "$reason"
  if [ "$DRY_RUN" -eq 1 ]; then echo "  [dry-run] rm -rf $ROOT_DIR/build"; else rm -rf "$ROOT_DIR/build"; fi
}

check_relocated_cache() {
  cache="$ROOT_DIR/build/CMakeCache.txt"
  [ -f "$cache" ] || return 0
  cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache" | head -n 1 || true)
  current_source=$(pwd -P)
  if [ -n "$cached_source" ] && [ "$cached_source" != "$current_source" ]; then
    remove_build_tree "Relocated source tree detected; removing stale CMake cache:\n  cached:  $cached_source\n  current: $current_source"
  fi
}

check_toolchain_cache() {
  cache="$ROOT_DIR/build/CMakeCache.txt"
  [ -f "$cache" ] || return 0

  cached_cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$cache" | head -n 1 || true)
  [ -n "$cached_cxx" ] || cached_cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:STRING=//p' "$cache" | head -n 1 || true)
  cached_make=$(sed -n 's/^CMAKE_MAKE_PROGRAM:FILEPATH=//p' "$cache" | head -n 1 || true)
  [ -n "$cached_make" ] || cached_make=$(sed -n 's/^CMAKE_MAKE_PROGRAM:STRING=//p' "$cache" | head -n 1 || true)

  wanted_cxx=$(readlink -f "$BUILD_CXX" 2>/dev/null || printf '%s' "$BUILD_CXX")
  wanted_make=$(readlink -f "$BUILD_MAKE" 2>/dev/null || printf '%s' "$BUILD_MAKE")
  actual_cached_cxx=$(readlink -f "$cached_cxx" 2>/dev/null || printf '%s' "$cached_cxx")
  actual_cached_make=$(readlink -f "$cached_make" 2>/dev/null || printf '%s' "$cached_make")

  if [ -n "$cached_cxx" ] && [ "$actual_cached_cxx" != "$wanted_cxx" ]; then
    remove_build_tree "Compiler selection changed; removing stale CMake cache:\n  cached C++: $cached_cxx\n  selected:   $BUILD_CXX"
    return 0
  fi
  if [ -n "$cached_make" ] && [ "$actual_cached_make" != "$wanted_make" ]; then
    remove_build_tree "Make program changed; removing stale CMake cache:\n  cached make: $cached_make\n  selected:    $BUILD_MAKE"
  fi
}

ask_install() {
  [ "$INSTALL_MODE" = ask ] || return 0
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "[dry-run] System installation remains disabled unless --install is explicitly supplied."
    INSTALL_MODE=no
    return 0
  fi
  if [ ! -t 0 ]; then INSTALL_MODE=no; return 0; fi
  echo "Build target: ./build/wafflehouse-client"
  if [ -e "$INSTALL_BIN" ] || [ -L "$INSTALL_BIN" ]; then
    echo "Existing installation detected: $INSTALL_BIN"
    printf 'Upgrade/install WaffleHouse-Client and add %s? [y/N] ' "$SYSTEM_BIN"
  else
    echo "System installation is optional."
    printf 'Install WaffleHouse-Client and add it to /bin as %s? [y/N] ' "$SYSTEM_BIN"
  fi
  IFS= read -r answer
  case "$answer" in
    y|Y|yes|YES|Yes) INSTALL_MODE=yes; INSTALL_BIN_ALIAS_MODE=1 ;;
    *) INSTALL_MODE=no; INSTALL_BIN_ALIAS_MODE=0 ;;
  esac
}

confirm_lifecycle_action() {
  action=$1
  [ "$ASSUME_YES" -eq 1 ] && return 0
  [ "$DRY_RUN" -eq 1 ] && return 0
  [ -t 0 ] || return 0
  case "$action" in
    upgrade)
      printf 'Upgrade WaffleHouse-Client under %s and preserve user configuration? [y/N] ' "$INSTALL_PREFIX"
      ;;
    uninstall)
      printf 'Remove WaffleHouse-Client application files under %s and preserve user configuration? [y/N] ' "$INSTALL_PREFIX"
      ;;
  esac
  IFS= read -r answer
  case "$answer" in y|Y|yes|YES|Yes) return 0 ;; *) echo "Action cancelled."; exit 0 ;; esac
}

prepare_privileges() {
  if [ "$INSTALL_MODE" != yes ] && [ "$UNINSTALL_MODE" -ne 1 ]; then return 0; fi
  bind_parent=$INSTALL_BINDIR; data_parent=$INSTALL_DESKTOPDIR
  while [ ! -d "$bind_parent" ] && [ "$bind_parent" != "/" ]; do bind_parent=$(dirname "$bind_parent"); done
  while [ ! -d "$data_parent" ] && [ "$data_parent" != "/" ]; do data_parent=$(dirname "$data_parent"); done
  system_bin_writable=1
  if [ "$INSTALL_BIN_ALIAS_MODE" -eq 1 ] && [ "$INSTALL_BIN" != "$SYSTEM_BIN" ] && [ ! -w "$(dirname "$SYSTEM_BIN")" ]; then system_bin_writable=0; fi
  if [ -w "$bind_parent" ] && [ -w "$data_parent" ] && [ "$system_bin_writable" -eq 1 ]; then PRIV_METHOD=none; return 0; fi
  if [ "$DRY_RUN" -eq 1 ]; then PRIV_METHOD=dry-run; echo "[dry-run] Installation lifecycle may require root privileges for $INSTALL_PREFIX."; return 0; fi
  if [ "$(id -u)" -eq 0 ]; then PRIV_METHOD=root; return 0; fi
  if command -v sudo >/dev/null 2>&1; then PRIV_METHOD=sudo; echo "Root privileges are required only for the install/upgrade lifecycle step."; sudo -v; return 0; fi
  if command -v doas >/dev/null 2>&1; then PRIV_METHOD=doas; echo "Root privileges are required only for the install/upgrade lifecycle step."; doas true; return 0; fi
  if command -v su >/dev/null 2>&1; then PRIV_METHOD=su; echo "Root privileges are required only for the install/upgrade lifecycle step; su will be used."; return 0; fi
  echo "Cannot modify the installation under $INSTALL_PREFIX: no root helper is available." >&2; exit 1
}

run_privileged() {
  if [ "$DRY_RUN" -eq 1 ]; then printf '  [dry-run] privileged:'; for arg in "$@"; do printf ' %s' "$arg"; done; echo; return 0; fi
  case "$PRIV_METHOD" in
    none|root) "$@" ;;
    sudo) sudo "$@" ;;
    doas) doas "$@" ;;
    su)
      cmd=; for arg in "$@"; do escaped=$(printf '%s' "$arg" | sed "s/'/'\\\\''/g"); cmd="$cmd '$escaped'"; done
      if [ "$OS_FAMILY" = freebsd ]; then su -m root -c "$cmd"; else su root -c "$cmd"; fi
      ;;
    *) echo "Privilege method was not initialized." >&2; exit 1 ;;
  esac
}

refresh_desktop_database() {
  [ "$DRY_RUN" -eq 0 ] || return 0
  command -v update-desktop-database >/dev/null 2>&1 || return 0
  [ -d "$INSTALL_DESKTOPDIR" ] || return 0
  update-desktop-database "$INSTALL_DESKTOPDIR" >/dev/null 2>&1 || true
}

refresh_icon_cache() {
  [ "$DRY_RUN" -eq 0 ] || return 0
  command -v gtk-update-icon-cache >/dev/null 2>&1 || return 0
  [ -d "$INSTALL_ICON_THEME_DIR" ] || return 0
  gtk-update-icon-cache -f -t "$INSTALL_ICON_THEME_DIR" >/dev/null 2>&1 || true
}

remove_path_if_present() {
  target=$1
  if [ -e "$target" ] || [ -L "$target" ]; then
    run_privileged rm -f "$target"
    echo "Removed obsolete install artifact: $target"
  fi
}

remove_tree_if_present() {
  target=$1
  if [ -d "$target" ] || [ -L "$target" ]; then
    run_privileged rm -rf "$target"
    echo "Removed installed application data: $target"
  fi
}

cleanup_legacy_installation() {
  # The unified executable/desktop entry are replaced by cmake --install after
  # the new build succeeds. Only obsolete 1.x split launchers are removed here.
  remove_path_if_present "$LEGACY_GUI"
  remove_path_if_present "$LEGACY_CLI"
  remove_path_if_present "$LEGACY_GUI_DESKTOP"
  remove_path_if_present "$LEGACY_CLI_DESKTOP"
}

uninstall_application_files() {
  prepare_privileges
  echo "==> Removing installed WaffleHouse-Client application files"
  if [ "$INSTALL_BIN" != "$SYSTEM_BIN" ]; then remove_path_if_present "$SYSTEM_BIN"; fi
  remove_path_if_present "$INSTALL_BIN"
  remove_path_if_present "$INSTALL_SHELL"
  remove_path_if_present "$INSTALL_SSH_COMPANION"
  remove_path_if_present "$INSTALL_DESKTOP"
  remove_tree_if_present "$INSTALL_DATADIR"
  for size in 16 22 24 32 48 64 128 256 512; do
    remove_path_if_present "$INSTALL_ICON_THEME_DIR/${size}x${size}/apps/wafflehouse-client.png"
  done
  cleanup_legacy_installation
  refresh_desktop_database
  refresh_icon_cache
  echo
  echo "Application files removed. Per-user WaffleHouse-Client/WaffleHouse configuration was preserved."
}

show_header

if [ "$UNINSTALL_MODE" -eq 1 ]; then
  confirm_lifecycle_action uninstall
  uninstall_application_files
  exit 0
fi

if [ "$UPGRADE_MODE" -eq 1 ]; then
  echo "Integrated upgrade mode: build first, then replace installed application files; user configuration is never removed."
  confirm_lifecycle_action upgrade
fi

if [ "$AUDIO_DIAG_ONLY" -eq 1 ]; then
  run_audio_preflight
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "Audio diagnosis dry run complete."
  else
    echo "Audio diagnosis complete (read-only)."
  fi
  exit 0
fi

ensure_dependencies
run_audio_preflight
configure_audio_fixes
ensure_pjsip

if [ "$CLEAN" -eq 1 ]; then
  remove_build_tree "Cleaning generated CMake build directory."
else
  check_relocated_cache
  check_toolchain_cache
fi

if [ "$INSTALL_MODE" = yes ]; then
  echo "Build will be installed after compilation succeeds (--install/--upgrade requested)."
elif [ "$INSTALL_MODE" = ask ]; then
  echo "Testing-safe mode: build first; system installation will be offered only after a successful build (default: No)."
else
  echo "Build only; no system installation will be performed."
fi

echo
echo "==> Configuring WaffleHouse-Client 5.5"
# Use the compiler and GNU Make that passed the preflight. On FreeBSD this
# intentionally means Clang/libc++ for ABI compatibility with packaged Qt6;
# GCC/G++ are still checked/installed as explicit project prerequisites.
# WAFFLEHOUSE_FEATURE_CMAKE_ARGS contains only fixed -DNAME=ON/OFF tokens from the top-level builder.
# shellcheck disable=SC2086
run_cmd env CC="$BUILD_CC" CXX="$BUILD_CXX" cmake -S . -B build \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
  -DCMAKE_MAKE_PROGRAM="$BUILD_MAKE" ${WAFFLEHOUSE_FEATURE_CMAKE_ARGS:-}

echo
echo "==> Building WaffleHouse-Client 5.5"
run_cmd cmake --build build --parallel "$JOBS"

# A normal test build never writes the application into PREFIX/bin unless the
# user explicitly approves this prompt. Enter/default is No.
ask_install

if [ "$INSTALL_MODE" = yes ]; then
  echo
  echo "==> Installing WaffleHouse-Client 5.5"
  prepare_privileges
  if [ -e "$INSTALL_BIN" ] || [ -L "$INSTALL_BIN" ]; then
    echo "Existing WaffleHouse-Client detected; performing in-place upgrade after successful build."
  fi
  cleanup_legacy_installation
  run_privileged cmake --install "$ROOT_DIR/build" --prefix "$INSTALL_PREFIX"
  if [ "$INSTALL_BIN_ALIAS_MODE" -eq 1 ] && [ "$INSTALL_BIN" != "$SYSTEM_BIN" ]; then
    run_privileged rm -f "$SYSTEM_BIN"
    run_privileged ln -s "$INSTALL_BIN" "$SYSTEM_BIN"
  fi
  refresh_desktop_database
  refresh_icon_cache
  echo
  echo "Installed executable: $INSTALL_BIN"
  if [ "$INSTALL_BIN_ALIAS_MODE" -eq 1 ]; then echo "Installed /bin launcher: $SYSTEM_BIN"; fi
  echo "Installed desktop entry: $INSTALL_DESKTOP"
  echo "User configuration preserved: yes"
else
  echo
  echo "Built executable: $ROOT_DIR/build/wafflehouse-client"
fi

echo
echo "Runtime mode selection:"
echo "  Desktop/menu launch              -> GUI"
echo "  Interactive terminal launch      -> CLI"
echo "  wafflehouse-client --gui         -> force GUI"
echo "  wafflehouse-client --cli         -> force CLI"

if [ "$DRY_RUN" -eq 1 ]; then echo "Dry run complete."; else echo "WaffleHouse-Client 5.5 build complete."; fi
