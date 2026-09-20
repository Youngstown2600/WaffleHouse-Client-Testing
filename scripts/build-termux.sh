#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
ROOT_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

ACTION=build
ASSUME_YES=0
ORIGINAL_ARGC=$#
PROTOCOLS=${WAFFLEHOUSE_PROTOCOLS:-}
usage() {
  cat <<'USAGE'
Usage: ./scripts/build-termux.sh [--uninstall|--remove-only] [--yes] [--protocols LIST|all]

With no options in an interactive Termux session the builder asks whether to
build/install or uninstall/remove WaffleHouse-Client. Uninstall preserves all
per-user WaffleHouse-Client/WaffleHouse configuration.
USAGE
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --uninstall|--remove-only) ACTION=uninstall ;;
    --yes|-y) ASSUME_YES=1 ;;
    --protocols) shift; [[ $# -gt 0 ]] || { echo "--protocols requires a value" >&2; exit 2; }; PROTOCOLS=$1 ;;
    --protocols=*) PROTOCOLS=${1#--protocols=} ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown Termux builder option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

echo "WaffleHouse-Client 5.6 - Termux/Android GUI + CLI"
if [[ -z "${TERMUX_VERSION:-}" && "${PREFIX:-}" != *com.termux* ]]; then
  echo "This builder must be run inside Termux." >&2; exit 2
fi

if [[ "$ORIGINAL_ARGC" -eq 0 && -t 0 ]]; then
  cat <<'ACTION_PROMPT'

What do you want to do?
  1) Build / Install WaffleHouse-Client
  2) Uninstall / Remove WaffleHouse-Client
ACTION_PROMPT
  read -r -p 'Selection [1]: ' answer
  case "$answer" in
    ''|1) ACTION=build ;;
    2) ACTION=uninstall ;;
    *) echo "Invalid selection." >&2; exit 2 ;;
  esac
fi

if [[ "$ACTION" == uninstall ]]; then
  if [[ "$ASSUME_YES" -ne 1 && -t 0 ]]; then
    read -r -p 'Remove installed WaffleHouse-Client files and preserve user configuration? [y/N]: ' answer
    case "$answer" in y|Y|yes|YES|Yes) ;; *) echo "Uninstall cancelled."; exit 0 ;; esac
  fi
  echo "==> Removing WaffleHouse-Client from Termux"
  rm -f "$PREFIX/bin/wafflehouse-client" \
        "$PREFIX/bin/wafflehouse-client-gui" \
        "$PREFIX/bin/wafflehouse-client-cli" \
        "$PREFIX/bin/wafflehouse-shell" \
        "$PREFIX/share/applications/wafflehouse-client.desktop"
  for size in 16 22 24 32 48 64 128 256 512; do
    rm -f "$PREFIX/share/icons/hicolor/${size}x${size}/apps/wafflehouse-client.png"
  done
  rm -rf "$PREFIX/share/wafflehouse-client"
  echo "WaffleHouse-Client application files removed. User configuration was preserved."
  exit 0
fi


if [[ -z "$PROTOCOLS" ]]; then
  if [[ -t 0 ]]; then
    echo
    echo "Modules to compile (comma-separated, or 'all'):"
    echo "  aim,irc,telnet,sip,media,xmpp,ssh,nntp,mosh,gopher,gemini"
    read -r -p "Modules [all]: " PROTOCOLS
    PROTOCOLS=${PROTOCOLS:-all}
  else
    PROTOCOLS=all
  fi
fi
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

pkg update -y
pkg install -y x11-repo
pkg update -y
pkg install -y clang cmake ninja pkg-config make git curl perl python \
  qt6-qtbase qt6-qtmultimedia libsodium ncurses openssl

PJSIP_PREFIX=${WAFFLEHOUSE_PJSIP_PREFIX:-$HOME/.local/wafflehouse-pjsip}
PJSIP_SRC="$ROOT_DIR/third_party/pjproject"
if [[ ! -f "$PJSIP_SRC/pjsip/include/pjsip.h" ]]; then
  mkdir -p "$ROOT_DIR/third_party"
  rm -rf "$PJSIP_SRC"
  git clone --depth 1 --branch 2.17 https://github.com/pjsip/pjproject.git "$PJSIP_SRC"
fi
if [[ ! -f "$PJSIP_PREFIX/.wafflehouse-pjsip-build" ]]; then
  CC=clang CXX=clang++ ./scripts/build-pjsip.sh "$PJSIP_SRC" "$PJSIP_PREFIX"
fi
export PKG_CONFIG_PATH="$PJSIP_PREFIX/lib/pkgconfig:$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export CMAKE_PREFIX_PATH="$PREFIX:${CMAKE_PREFIX_PATH:-}"
cmake -S . -B build-termux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  "${FEATURE_ARGS[@]}"
cmake --build build-termux --parallel
cmake --install build-termux
cat > "$PREFIX/bin/wafflehouse-client-gui" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
exec wafflehouse-client --gui "$@"
EOF
cat > "$PREFIX/bin/wafflehouse-client-cli" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
exec wafflehouse-client --cli "$@"
EOF
chmod +x "$PREFIX/bin/wafflehouse-client-gui" "$PREFIX/bin/wafflehouse-client-cli"
echo
echo "Installed: $PREFIX/bin/wafflehouse-client"
echo "CLI:       wafflehouse-client-cli"
echo "GUI:       wafflehouse-client-gui  (requires a Termux:X11 graphical session)"
