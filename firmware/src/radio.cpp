#include "radio.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "audio.h"
#include "config.h"
#include "hw.h"
#include "net.h"
#include "settings.h"

namespace radio {

volatile bool screenOpen = false;

enum PType : uint8_t { P_HELLO = 1, P_VOICE = 2, P_END = 3, P_KNOCK = 4 };

struct __attribute__((packed)) Hdr {
  uint8_t magic[2];  // 'T' 'F'
  uint8_t type;
  uint8_t ver;
  uint32_t from;
  uint32_t to;       // 0 = alle
  uint16_t seq;
};

struct __attribute__((packed)) Hello {
  Hdr h;
  char name[17];
  int8_t battery;
  uint32_t epoch;    // 0 = Uhrzeit unbekannt
};

static const int VOICE_SAMPLES = 448;
struct __attribute__((packed)) VoicePkt {
  Hdr h;
  int16_t pred;
  uint8_t index;
  uint8_t pad;
  uint8_t data[VOICE_SAMPLES / 2];
};
static_assert(sizeof(VoicePkt) <= 250, "ESP-NOW-Paket zu groß");

struct RxPkt { uint8_t len; int8_t rssi; bool lan; uint8_t data[250]; };

static uint32_t me = 0;
static uint16_t seq = 0;
static QueueHandle_t rxq;
static WiFiUDP udp;
static bool udpOn = false;
static IPAddress mcast;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static volatile bool txOn = false, txStop = false;
static volatile uint32_t txTo = 0;
static volatile uint32_t rxFrom = 0, rxLast = 0;
static char rxName[17] = "";

static const int PEER_MAX = 12;
static Peer peerTab[PEER_MAX];
static uint16_t peerSeq[PEER_MAX];
static SemaphoreHandle_t lock;

static Event events[4];
static uint8_t evHead = 0, evTail = 0;

uint32_t myId() { return me; }
bool transmitting() { return txOn; }
bool receiving() { return rxFrom && millis() - rxLast < 600; }
const char* talker() { return rxName; }

bool takeEvent(Event* ev) {
  if (evTail == evHead) return false;
  *ev = events[evTail++ % 4];
  return true;
}

static void pushEvent(EventType t, const char* name) {
  Event& e = events[evHead % 4];
  e.type = t;
  snprintf(e.name, sizeof(e.name), "%s", name);
  evHead++;
}

// ------------------------------------------------------------------ IMA-ADPCM

static const int16_t STEP[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct Adpcm { int pred = 0; int index = 0; };

static uint8_t encodeOne(Adpcm& s, int16_t x) {
  int step = STEP[s.index], diff = x - s.pred, code = 0;
  if (diff < 0) { code = 8; diff = -diff; }
  int vp = step >> 3;
  if (diff >= step) { code |= 4; diff -= step; vp += step; }
  step >>= 1;
  if (diff >= step) { code |= 2; diff -= step; vp += step; }
  step >>= 1;
  if (diff >= step) { code |= 1; vp += step; }
  s.pred += (code & 8) ? -vp : vp;
  s.pred = constrain(s.pred, -32768, 32767);
  s.index = constrain(s.index + IDX[code], 0, 88);
  return code;
}

static int16_t decodeOne(Adpcm& s, uint8_t code) {
  int step = STEP[s.index], vp = step >> 3;
  if (code & 4) vp += step;
  if (code & 2) vp += step >> 1;
  if (code & 1) vp += step >> 2;
  s.pred += (code & 8) ? -vp : vp;
  s.pred = constrain(s.pred, -32768, 32767);
  s.index = constrain(s.index + IDX[code], 0, 88);
  return s.pred;
}

// ------------------------------------------------------------------ Senden

static void fill(Hdr& h, uint8_t type, uint32_t to) {
  h.magic[0] = 'T';
  h.magic[1] = 'F';
  h.type = type;
  h.ver = 1;
  h.from = me;
  h.to = to;
  h.seq = seq++;
}

static void send(const void* p, size_t len) {
  esp_now_send(BCAST, (const uint8_t*)p, len);
  if (udpOn) {
    udp.beginPacket(mcast, RADIO_PORT);
    udp.write((const uint8_t*)p, len);
    udp.endPacket();
  }
}

static void sendHello() {
  Hello p{};
  fill(p.h, P_HELLO, 0);
  snprintf(p.name, sizeof(p.name), "%s", cfg.name);
  p.battery = hwBattery();
  p.epoch = timeValid() ? (uint32_t)time(nullptr) : 0;
  send(&p, sizeof(p));
}

void pttDown(uint32_t to) {
  if (txOn) return;
  txTo = to;
  txStop = false;
  audio::play(audio::FX_PTT_ON);
  txOn = true;
}

void pttUp() {
  if (txOn) txStop = true;
}

void knock(uint32_t to) {
  Hdr h{};
  fill(h, P_KNOCK, to);
  send(&h, sizeof(h));
}

static volatile bool helloNow = false;
void nameChanged() { helloNow = true; }

// ------------------------------------------------------------------ Empfangen

static void onNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(Hdr) || len > 250 || data[0] != 'T' || data[1] != 'F') return;
  RxPkt p;
  p.len = len;
  p.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  p.lan = false;
  memcpy(p.data, data, len);
  xQueueSend(rxq, &p, 0);
}

static int peerFind(uint32_t id) {
  for (int i = 0; i < PEER_MAX; i++) if (peerTab[i].id == id) return i;
  return -1;
}

// Liefert false, wenn das Paket doppelt ist (kam schon über den anderen Weg)
static bool peerSeen(const Hdr& h, int8_t rssi, bool lan) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int i = peerFind(h.from);
  bool fresh = true;
  if (i < 0) {
    i = 0;
    for (int k = 1; k < PEER_MAX; k++) if (peerTab[k].lastSeen < peerTab[i].lastSeen) i = k;
    peerTab[i] = Peer{};
    peerTab[i].id = h.from;
    snprintf(peerTab[i].name, sizeof(peerTab[i].name), "%08X", (unsigned)h.from);
    peerSeq[i] = h.seq - 1;
  }
  if ((int16_t)(h.seq - peerSeq[i]) <= 0) fresh = false;
  else peerSeq[i] = h.seq;
  peerTab[i].lastSeen = millis();
  if (!lan && rssi) peerTab[i].rssi = rssi;
  peerTab[i].viaLan = lan;
  xSemaphoreGive(lock);
  return fresh;
}

static const char* peerName(uint32_t id) {
  int i = peerFind(id);
  return i >= 0 ? peerTab[i].name : "Unbekannt";
}

static Adpcm rxState;

static void handle(const RxPkt& p) {
  const Hdr& h = *(const Hdr*)p.data;
  if (h.from == me) return;
  if (!peerSeen(h, p.rssi, p.lan)) return;
  if (h.to && h.to != me) return;  // an jemand anderen
  bool listen = cfg.radioRx || screenOpen;
  switch (h.type) {
    case P_HELLO: {
      if (p.len < sizeof(Hello)) return;
      const Hello& m = *(const Hello*)p.data;
      xSemaphoreTake(lock, portMAX_DELAY);
      int i = peerFind(h.from);
      if (i >= 0) {
        snprintf(peerTab[i].name, sizeof(peerTab[i].name), "%.*s", 16, m.name);
        peerTab[i].battery = m.battery;
      }
      xSemaphoreGive(lock);
      if (m.epoch) timeFromPeer(m.epoch);
      break;
    }
    case P_VOICE: {
      if (txOn || !listen || p.len < sizeof(VoicePkt)) return;
      const VoicePkt& v = *(const VoicePkt*)p.data;
      if (rxFrom != h.from || millis() - rxLast > 600) {
        // neue Durchsage: kurz vorpuffern gegen Aussetzer
        rxFrom = h.from;
        snprintf(rxName, sizeof(rxName), "%s", peerName(h.from));
        static int16_t silence[1600];
        audio::voiceFlush();
        vTaskDelay(2);
        audio::voiceWrite(silence, 1600);
        pushEvent(EV_TALK, rxName);
      }
      rxLast = millis();
      rxState.pred = v.pred;
      rxState.index = constrain(v.index, 0, 88);
      static int16_t pcm[VOICE_SAMPLES];
      for (int i = 0; i < VOICE_SAMPLES / 2; i++) {
        pcm[2 * i] = decodeOne(rxState, v.data[i] & 0x0F);
        pcm[2 * i + 1] = decodeOne(rxState, v.data[i] >> 4);
      }
      audio::voiceWrite(pcm, VOICE_SAMPLES);
      break;
    }
    case P_END:
      if (rxFrom == h.from && listen && !txOn) audio::play(audio::FX_ROGER);
      if (rxFrom == h.from) rxLast = 0;
      break;
    case P_KNOCK:
      audio::play(audio::FX_KNOCK);
      pushEvent(EV_KNOCK, peerName(h.from));
      break;
  }
}

// ------------------------------------------------------------------ Aufgabe

static void task(void*) {
  Adpcm tx;
  static int16_t pcm[VOICE_SAMPLES];
  int have = 0;
  uint32_t helloAt = 0, txStart = 0;
  bool micRunning = false;
  for (;;) {
    RxPkt p;
    while (xQueueReceive(rxq, &p, txOn ? 0 : pdMS_TO_TICKS(10)) == pdTRUE) handle(p);

    // UDP im WLAN
    bool online = netOnline();
    if (online && !udpOn) udpOn = udp.beginMulticast(mcast, RADIO_PORT);
    else if (!online && udpOn) { udp.stop(); udpOn = false; }
    if (udpOn) {
      int n;
      while ((n = udp.parsePacket()) > 0) {
        if (n > 250) { udp.flush(); continue; }
        p.len = udp.read(p.data, n);
        p.rssi = 0;
        p.lan = true;
        if (p.len >= sizeof(Hdr) && p.data[0] == 'T' && p.data[1] == 'F') handle(p);
      }
    }

    if (helloNow || millis() - helloAt > 3000) { helloNow = false; helloAt = millis(); sendHello(); }

    // Senden: Mikrofon erst nach dem Start-Piep öffnen
    if (txOn && !micRunning) { txStart = millis(); micRunning = true; have = -1; }
    if (micRunning && have < 0 && millis() - txStart > 120) { audio::micStart(); have = 0; tx = Adpcm(); }
    if (micRunning && have >= 0) {
      have += audio::micRead(pcm + have, VOICE_SAMPLES - have);
      if (have == VOICE_SAMPLES) {
        VoicePkt v{};
        fill(v.h, P_VOICE, txTo);
        v.pred = tx.pred;
        v.index = tx.index;
        for (int i = 0; i < VOICE_SAMPLES / 2; i++)
          v.data[i] = encodeOne(tx, pcm[2 * i]) | (encodeOne(tx, pcm[2 * i + 1]) << 4);
        send(&v, sizeof(v));
        have = 0;
      } else {
        vTaskDelay(pdMS_TO_TICKS(4));
      }
    }
    if (txStop && micRunning) {
      audio::micStop();
      Hdr h{};
      fill(h, P_END, txTo);
      for (int i = 0; i < 2; i++) { send(&h, sizeof(h)); h.seq = seq++; vTaskDelay(pdMS_TO_TICKS(15)); }
      audio::play(audio::FX_PTT_OFF);
      micRunning = false;
      txOn = false;
      txStop = false;
    }
  }
}

int peers(Peer* out, int max) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = 0;
  for (int i = 0; i < PEER_MAX && n < max; i++)
    if (peerTab[i].id && millis() - peerTab[i].lastSeen < 12000) out[n++] = peerTab[i];
  xSemaphoreGive(lock);
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (strcasecmp(out[j].name, out[i].name) < 0) { Peer t = out[i]; out[i] = out[j]; out[j] = t; }
  return n;
}

void begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  me = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
  seq = esp_random();
  mcast.fromString(RADIO_MCAST_IP);
  lock = xSemaphoreCreateMutex();
  rxq = xQueueCreate(24, sizeof(RxPkt));
  if (esp_now_init() != ESP_OK) { Serial.println("Funk: ESP-NOW-Start fehlgeschlagen"); return; }
  esp_now_register_recv_cb(onNow);
  esp_now_peer_info_t pi{};
  memcpy(pi.peer_addr, BCAST, 6);
  pi.channel = 0;  // aktueller Kanal
  pi.ifidx = WIFI_IF_STA;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
  xTaskCreatePinnedToCore(task, "radio", 6144, nullptr, 8, nullptr, 0);
  Serial.printf("Funk: bereit, ID %08X\n", (unsigned)me);
}

}  // namespace radio
