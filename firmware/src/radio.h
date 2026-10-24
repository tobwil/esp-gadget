// Funk: Walkie-Talkie zwischen Geräten mit dieser Firmware.
//   - in der Nähe per ESP-NOW (Broadcast, ohne Router)
//   - im selben WLAN zusätzlich per UDP-Multicast
// Sprache: 16 kHz, IMA-ADPCM (4 Bit), 28 ms pro Paket. Halbduplex wie ein echtes Funkgerät.
#pragma once
#include <Arduino.h>

namespace radio {

struct Peer {
  uint32_t id;
  char name[17];
  int8_t rssi;       // dBm (nur ESP-NOW), 0 = unbekannt
  int8_t battery;
  bool viaLan;
  uint32_t lastSeen; // millis
};

enum EventType : uint8_t { EV_KNOCK, EV_TALK };
struct Event { EventType type; char name[17]; };

void begin();
// Funk an? Im Kindermodus ohne die App „Funk“ ist er ganz aus: kein Senden (auch kein „Hallo“ – die Uhr ist für
// andere unsichtbar), kein Empfang, kein Zähneputzen im Team; die Funk-Aufgabe schläft dann und spart Akku.
bool enabled();
void pttDown(uint32_t to = 0);  // 0 = an alle
void pttUp();
void knock(uint32_t to);        // „Anklopfen“: kurzer Ton beim anderen
void nameChanged();

bool transmitting();
bool receiving();
const char* talker();           // Name, wer gerade spricht
int peers(Peer* out, int max);  // aktuelle Nachbarn, zuletzt gesehene zuerst
bool takeEvent(Event* ev);

// Zähneputzen im Team: jede Uhr meldet ihren Stand, die andere zeigt ihn an
struct BrushMate { uint32_t id; char name[17]; uint8_t hero, mode, section, score; bool done; uint32_t seen; };
void brushSend(uint8_t hero, uint8_t mode, uint8_t section, uint8_t score, bool done);
bool brushMate(BrushMate* out);  // jemand in der Nähe putzt gerade (in den letzten 6 s gehört)
uint32_t myId();

extern volatile bool screenOpen;  // Funk-Bildschirm offen: auch ohne „Empfang im Hintergrund“ hören

}  // namespace radio
