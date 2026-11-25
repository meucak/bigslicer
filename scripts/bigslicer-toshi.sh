#!/usr/bin/env bash
set -euo pipefail

BIN="/home/toshi/apps/bigslicer/bigslicer-bin"
GLOBAL_URL="https://meucak.github.io/bigslicer/"
CANDIDATES=(
  "http://192.168.1.100:8098"
  "http://100.69.155.110:8098"
)

find_kasa() {
  for u in "${CANDIDATES[@]}"; do
    if curl -fsS --max-time 0.9 "${u}/api/health" >/dev/null 2>&1; then
      echo "$u"
      return 0
    fi
  done
  return 1
}

launch_url() {
  local url="$1"
  export DISPLAY="${DISPLAY:-:0}"
  export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"
  for b in chromium chromium-browser google-chrome brave-browser microsoft-edge firefox xdg-open; do
    if command -v "$b" >/dev/null 2>&1; then
      if [ "$b" = "firefox" ] || [ "$b" = "xdg-open" ]; then
        nohup "$b" "$url" >/dev/null 2>&1 </dev/null &
      else
        nohup "$b" --app="$url" >/dev/null 2>&1 </dev/null &
      fi
      echo "BigSlicer açıldı: $url"
      return 0
    fi
  done
  echo "BigSlicer URL: $url"
}

case "${1:-}" in
  ""|open)
    if KASA="$(find_kasa)"; then
      launch_url "$KASA"
    else
      launch_url "$GLOBAL_URL"
    fi
    ;;
  --global|global)
    launch_url "$GLOBAL_URL"
    ;;
  status|--status)
    if KASA="$(find_kasa)"; then
      echo "BigSlicer Kasa Düğümü : ÇEVRİMİÇİ ($KASA)"
      curl -fsS --max-time 1.5 "${KASA}/api/health" || true
      echo ""
    else
      echo "BigSlicer Kasa Düğümü : KAPALI (Çevrimdışı WASM PWA hazır: $GLOBAL_URL)"
    fi
    ;;
  *)
    if [ -x "$BIN" ]; then
      exec "$BIN" "$@"
    fi
    echo "BigSlicer native binary bulunamadı: $BIN" >&2
    exit 1
    ;;
esac
