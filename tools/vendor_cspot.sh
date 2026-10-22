#!/bin/sh
# Holt cspot (Spotify Connect, Fork von philippe44) samt der nötigen Teile von „bell“ als
# PlatformIO-Bibliothek nach firmware/lib/cspot und erzeugt die nanopb-Protobuf-Dateien.
#
#   sh tools/vendor_cspot.sh
#
# Braucht git und python3. Lizenz von cspot: GPL-3.0 (siehe firmware/lib/cspot/LICENSE.md).
set -e
CSPOT_REV=99e5c95ad6d862a1b7845dda81fd9c8233b45b9f
BELL_REV=46a4a8f21f3b0bc0ccefd01915b24d74f0ab9bef

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DST="$ROOT/firmware/lib/cspot"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

git clone -q https://github.com/philippe44/cspot.git "$TMP/cspot"
git -C "$TMP/cspot" checkout -q $CSPOT_REV
git clone -q https://github.com/philippe44/bell.git "$TMP/bell"
git -C "$TMP/bell" checkout -q $BELL_REV
C="$TMP/cspot/cspot"
B="$TMP/bell"

rm -rf "$DST"
mkdir -p "$DST/src/cspot" "$DST/src/proto/protobuf" "$DST/src/bell" "$DST/src/nanopb" "$DST/src/cjson" \
         "$DST/src/tremor" "$DST/include"

# cspot
cp "$C"/src/*.cpp "$DST/src/cspot/"
cp "$C"/include/*.h "$DST/include/"
cp "$TMP/cspot/LICENSE.md" "$DST/"

# Protobuf (nanopb) einmalig erzeugen
python3 -m venv "$TMP/venv"
"$TMP/venv/bin/pip" -q install "nanopb==0.4.9" "protobuf<6" grpcio-tools
for p in "$C"/protobuf/*.proto; do
  (cd "$C" && "$TMP/venv/bin/nanopb_generator" -q -I . -D "$DST/src/proto" "protobuf/$(basename "$p")")
done

# bell: Hilfen, Netz/TLS, Plattform (ohne Webserver, MQTT, Tar)
cp "$B"/main/utilities/*.cpp "$B"/main/utilities/*.c "$B"/main/utilities/include/*.h "$DST/src/bell/"
for f in BinaryReader BinaryStream BufferedStream CircularBuffer FileStream HTTPClient SocketStream TLSSocket URLParser X509Bundle; do
  cp "$B/main/io/$f.cpp" "$DST/src/bell/"
done
cp "$B"/main/io/picohttpparser.c "$B"/main/io/include/*.h "$DST/src/bell/"
rm -f "$DST/src/bell/BellHTTPServer.h" "$DST/src/bell/BellMQTTClient.h" "$DST/src/bell/MGStreamAdapter.h" "$DST/src/bell/BellTar.h"
cp "$B"/main/platform/*.h "$B"/main/platform/esp/*.cpp "$DST/src/bell/"

# nanopb, cJSON, Tremor (Vorbis). MP3 (libhelix) und JPEG bringt der Arduino-Core selbst mit.
cp "$B"/external/nanopb/pb*.c "$B"/external/nanopb/pb*.h "$DST/src/nanopb/"
cp "$B"/external/cJSON/cJSON.c "$B"/external/cJSON/cJSON.h "$DST/src/cjson/"
cp "$B"/external/tremor/*.c "$B"/external/tremor/*.h "$DST/src/tremor/"
rm -f "$DST/src/tremor/ivorbisfile_example.c"

# Alle Header nach include/ (PlatformIO durchsucht den Ordner vor den ESP-IDF-Pfaden;
# sonst greifen gleichnamige Dateien wie bt/.../crypto.h – macOS unterscheidet keine Groß-/Kleinschreibung)
for d in bell nanopb cjson tremor; do mv "$DST"/src/$d/*.h "$DST/include/"; done
mkdir -p "$DST/include/protobuf"
mv "$DST"/src/proto/protobuf/*.h "$DST/include/protobuf/"

# kleine Anpassungen für den Arduino-Core:
# - bells HTTPClient.h heißt wie das Arduino-HTTPClient.h → umbenennen
# - fmt::format (nur für Range-Header) durch std::to_string ersetzen
mv "$DST/include/HTTPClient.h" "$DST/include/BellHTTPClient.h"
grep -rl '"HTTPClient.h"' "$DST/src" "$DST/include" | while read f; do
  sed -i.bak 's/"HTTPClient.h"/"BellHTTPClient.h"/' "$f" && rm "$f.bak"
done
sed -i.bak -e '/#include "fmt\/core.h"/d' \
  -e 's/fmt::format("bytes={}-{}", from, to)/"bytes=" + std::to_string(from) + "-" + std::to_string(to)/' \
  -e 's/fmt::format("bytes=-{}", nbytes)/"bytes=-" + std::to_string(nbytes)/' "$DST/include/BellHTTPClient.h"
rm "$DST/include/BellHTTPClient.h.bak"

sed -i.bak '1i\
#include <cstring>
' "$DST/src/bell/URLParser.cpp" && rm "$DST/src/bell/URLParser.cpp.bak"

# Load-Frame meldet FLUSH: so kann die Uhr ihren Puffer leeren, ohne bei normalen Titelwechseln
# (lückenlos) das Ende des vorigen Titels abzuschneiden
python3 - "$DST/src/cspot/SpircHandler.cpp" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
key = "      // Stop the current track, if any\n      trackPlayer->resetState();\n      break;"
assert key in s, "Load-Fall nicht gefunden"
s = s.replace(key, "      // Stop the current track, if any\n      trackPlayer->resetState();\n      sendEvent(EventType::FLUSH);\n      break;", 1)
open(p, "w").write(s)
PY

# Zeroconf: DH-Schlüssel speicherbar, Fehler als Ausnahme statt Absturz
python3 "$ROOT/tools/patch_cspot_zeroconf.py" "$DST"

# cJSON-Variante meldet sonst „deviceType“ als Gerätetyp – die Spotify-App erwartet SPEAKER
sed -i.bak 's/"deviceType", "deviceType"/"deviceType", "SPEAKER"/' "$DST/src/cspot/LoginBlob.cpp" && rm "$DST/src/cspot/LoginBlob.cpp.bak"

# Erweiterung: Titel von der Uhr selbst laden (wie ein Load-Frame aus der Spotify-App)
sed -i.bak 's/  void loadTrackFromURI(const std::string& uri);/  void loadTrackFromURI(const std::string\& uri);\
  \/\/ Taschenfunk: Warteschlange aus Spotify-URIs laden und ab index abspielen\
  void loadLocal(const std::vector<std::string>\& uris, int index, const std::string\& contextUri);/' "$DST/include/SpircHandler.h"
rm "$DST/include/SpircHandler.h.bak"
cat >> "$DST/src/cspot/SpircHandler.cpp" <<'CPP'

// ---- Taschenfunk: Titel von der Uhr selbst laden. Baut denselben Zustand auf wie ein
// Load-Frame aus der Spotify-App (siehe MessageType_kMessageTypeLoad in handleFrame).
void SpircHandler::loadLocal(const std::vector<std::string>& uris, int index,
                             const std::string& contextUri) {
  if (uris.empty()) return;
  playbackState->remoteTracks.clear();
  for (auto& u : uris) {
    TrackReference ref;
    ref.uri = u;
    ref.context = contextUri;
    ref.decodeURI();
    playbackState->remoteTracks.push_back(ref);
  }
  auto& rf = playbackState->remoteFrame;
  rf.state.context_uri = (char*)realloc(rf.state.context_uri, contextUri.size() + 1);
  strcpy(rf.state.context_uri, contextUri.c_str());
  rf.state.has_playing_track_index = true;
  rf.state.playing_track_index = index;
  rf.state.position_ms = 0;
  rf.position = 0;

  this->trackPlayer->start();
  playbackState->setActive(true);
  playbackState->updatePositionMs(0);
  playbackState->setPlaybackState(PlaybackState::State::Playing);
  playbackState->syncWithRemote();
  trackQueue->updateTracks(0, true);
  this->notify();
  trackPlayer->resetState();
  sendEvent(EventType::FLUSH);
  sendEvent(EventType::PLAY_PAUSE, false);
}
CPP

cat > "$DST/library.json" <<'EOF'
{
  "name": "cspot",
  "version": "0.0.0-philippe44",
  "description": "Spotify Connect (cspot, Fork von philippe44) mit Teilen von bell, nanopb, cJSON, Tremor",
  "license": "GPL-3.0",
  "build": {
    "flags": [
      "-DBELL_ONLY_CJSON", "-DBELL_DISABLE_FMT", "-DBELL_DISABLE_REGEX", "-DPB_ENABLE_MALLOC", "-DPB_FIELD_32BIT",
      "-DUSE_DEFAULT_STDLIB=1", "-DTARGET_OS_IPHONE=0", "-Wno-error=format", "-Wno-format", "-Wno-unused-variable"
    ]
  }
}
EOF
echo "cspot nach $DST übernommen (cspot $CSPOT_REV, bell $BELL_REV)"
