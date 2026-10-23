#!/bin/sh
# Firmware per WLAN aufspielen (statt USB):
#   sh tools/ota.sh <Adresse der Uhr> [amoled-175c|amoled-216] [Eltern-PIN]
# z. B.  sh tools/ota.sh taschenfunk-e008.local amoled-175c
# Die Uhr prüft, ob die Firmware zu ihrem Board passt, installiert sie und startet neu.
set -e
HOST=${1:?Adresse der Uhr fehlt}
ENV=${2:-amoled-175c}
PIN=${3:-}
cd "$(dirname "$0")/../firmware"
~/.platformio/penv/bin/pio run -e "$ENV" | tail -1
# eine dösende Uhr antwortet manchmal träge – bis zu dreimal versuchen
for i in 1 2 3; do
  curl -sSk --max-time 180 -H "X-Pin: $PIN" -F "file=@.pio/build/$ENV/firmware.bin" "https://$HOST/api/update" && echo && exit 0
  echo "Versuch $i fehlgeschlagen" >&2
  sleep 3
done
exit 1
