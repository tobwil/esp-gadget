#!/bin/sh
# Tests am Rechner: Hören und Zeichnen (C++), Sprachdatei, Weboberfläche (Python), beide Firmware-Builds.
#   sh tests/run_host.sh          # alles
#   sh tests/run_host.sh quick    # ohne Firmware-Builds
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
echo "== C++: Hören und Zeichnen"
c++ -std=c++17 -O2 -Wall -Wno-unused-function -Ifirmware/src firmware/src/hearing.cpp firmware/src/kidart.cpp tests/host/test_host.cpp -o "$T/t"
"$T/t"
echo "== Python: Sprachdatei, Weboberfläche, Vorschau"
python3 tests/host/check_assets.py
if [ "$1" != quick ]; then
  echo "== Firmware: beide Uhren"
  for e in amoled-175c amoled-216; do
    OUT=$(cd firmware && ~/.platformio/penv/bin/pio run -e $e 2>&1)
    echo "$OUT" | grep -E "error|RAM:|Flash:|SUCCESS|FAILED"
    echo "$OUT" | grep -q SUCCESS || exit 1
    # Budget für statischen internen RAM: was hier liegt, fehlt WLAN, HTTPS und Radio (main: ≈ 94 KB;
    # 38 KB zu viel ließen die Weboberfläche beim Radiohören abbrechen und die Uhr neu starten)
    RAM=$(echo "$OUT" | sed -n 's/.*RAM:.*used \([0-9]*\) bytes.*/\1/p')
    if [ "$RAM" -gt 102400 ]; then echo "FEHLER $e: statischer RAM $RAM Byte > 100 KB"; exit 1; fi
  done
fi
echo "== alles grün"
