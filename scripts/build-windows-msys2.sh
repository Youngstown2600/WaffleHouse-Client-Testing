#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

echo "WaffleHouse-Client 5.5 - Windows/MSYS2 UCRT64 GUI + CLI"
PROTOCOLS=${WAFFLEHOUSE_PROTOCOLS:-all}
FEATURE_OSCAR=OFF; FEATURE_IRC=OFF; FEATURE_TELNET=OFF; FEATURE_SIP=OFF; FEATURE_MEDIA=OFF
FEATURE_XMPP=OFF; FEATURE_SSH=OFF; FEATURE_NNTP=OFF; FEATURE_MOSH=OFF; FEATURE_GOPHER=OFF; FEATURE_GEMINI=OFF
feature_on() {
  case "${1,,}" in
    aim|oscar) FEATURE_OSCAR=ON ;; irc) FEATURE_IRC=ON ;; telnet|bbs) FEATURE_TELNET=ON ;;
    sip|voip|pjsip|chan_sip|chansip) FEATURE_SIP=ON ;; media|radio|player) FEATURE_MEDIA=ON ;;
    xmpp|jabber) FEATURE_XMPP=ON ;; ssh) FEATURE_SSH=ON ;; nntp|usenet|news) FEATURE_NNTP=ON ;;
    mosh) FEATURE_MOSH=ON ;; gopher) FEATURE_GOPHER=ON ;; gemini) FEATURE_GEMINI=ON ;;
    '') ;; *) echo "Unknown WaffleHouse-Client module: $1" >&2; exit 2 ;;
  esac
}
if [[ "${PROTOCOLS,,}" == all ]]; then
  FEATURE_OSCAR=ON; FEATURE_IRC=ON; FEATURE_TELNET=ON; FEATURE_SIP=ON; FEATURE_MEDIA=ON
  FEATURE_XMPP=ON; FEATURE_SSH=ON; FEATURE_NNTP=ON; FEATURE_MOSH=ON; FEATURE_GOPHER=ON; FEATURE_GEMINI=ON
else
  IFS=',' read -r -a _features <<< "$PROTOCOLS"
  for _f in "${_features[@]}"; do feature_on "$_f"; done
fi
FEATURE_ARGS=(
  "-DWAFFLEHOUSE_ENABLE_OSCAR=$FEATURE_OSCAR" "-DWAFFLEHOUSE_ENABLE_IRC=$FEATURE_IRC"
  "-DWAFFLEHOUSE_ENABLE_TELNET=$FEATURE_TELNET" "-DWAFFLEHOUSE_ENABLE_SIP=$FEATURE_SIP"
  "-DWAFFLEHOUSE_ENABLE_MEDIA=$FEATURE_MEDIA" "-DWAFFLEHOUSE_ENABLE_XMPP=$FEATURE_XMPP"
  "-DWAFFLEHOUSE_ENABLE_SSH=$FEATURE_SSH" "-DWAFFLEHOUSE_ENABLE_NNTP=$FEATURE_NNTP"
  "-DWAFFLEHOUSE_ENABLE_MOSH=$FEATURE_MOSH"
  "-DWAFFLEHOUSE_ENABLE_GOPHER=$FEATURE_GOPHER" "-DWAFFLEHOUSE_ENABLE_GEMINI=$FEATURE_GEMINI"
)
echo "Modules: $PROTOCOLS"

if [[ "${MSYSTEM:-}" != UCRT64 ]]; then
  echo "This script requires an MSYS2 UCRT64 shell. Run build-windows.ps1." >&2; exit 2
fi

PREFIX_NAME=${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}
pacman -S --needed --noconfirm \
  base-devel git curl \
  "$PREFIX_NAME-toolchain" "$PREFIX_NAME-cmake" "$PREFIX_NAME-ninja" "$PREFIX_NAME-pkgconf" "$PREFIX_NAME-qt6-base" "$PREFIX_NAME-qt6-multimedia" \
  "$PREFIX_NAME-libsodium" "$PREFIX_NAME-ncurses" "$PREFIX_NAME-openssl" || {
    echo "Dependency installation failed. Update MSYS2 (pacman -Syu) and rerun." >&2; exit 1;
  }

PJSIP_PREFIX=${WAFFLEHOUSE_PJSIP_PREFIX:-$HOME/.local/wafflehouse-pjsip}
PJSIP_SRC="$ROOT_DIR/third_party/pjproject"
if [[ ! -f "$PJSIP_SRC/pjsip/include/pjsip.h" ]]; then
  mkdir -p "$ROOT_DIR/third_party"
  rm -rf "$PJSIP_SRC"
  git clone --depth 1 --branch 2.17 https://github.com/pjsip/pjproject.git "$PJSIP_SRC"
fi
if [[ ! -f "$PJSIP_PREFIX/.wafflehouse-pjsip-build" ]] || ! grep -q 'windows' "$PJSIP_PREFIX/.wafflehouse-pjsip-build"; then
  ./scripts/build-pjsip.sh "$PJSIP_SRC" "$PJSIP_PREFIX"
fi
export PKG_CONFIG_PATH="$PJSIP_PREFIX/lib/pkgconfig:${MINGW_PREFIX:-/ucrt64}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export CMAKE_PREFIX_PATH="${MINGW_PREFIX:-/ucrt64}:${CMAKE_PREFIX_PATH:-}"

cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/dist/windows" \
  "${FEATURE_ARGS[@]}"
cmake --build build-windows --parallel
cmake --install build-windows

mkdir -p dist/windows
EXE=$(find build-windows -maxdepth 3 -type f -iname 'wafflehouse-client.exe' | head -1)
[[ -n "$EXE" ]] || { echo "wafflehouse-client.exe was not produced" >&2; exit 1; }
cp -f "$EXE" dist/windows/wafflehouse-client.exe
DEPLOY=$(command -v windeployqt6 || command -v windeployqt || true)
if [[ -n "$DEPLOY" ]]; then "$DEPLOY" dist/windows/wafflehouse-client.exe; fi
cat > dist/windows/wafflehouse-client-gui.cmd <<'EOF'
@echo off
"%~dp0wafflehouse-client.exe" --gui %*
EOF
cat > dist/windows/wafflehouse-client-cli.cmd <<'EOF'
@echo off
"%~dp0wafflehouse-client.exe" --cli %*
EOF
echo "Windows package: $ROOT_DIR/dist/windows"
