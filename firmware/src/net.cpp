#include "net.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <sys/time.h>
#include "config.h"
#include "settings.h"

// Zuletzt benutztes Netz mit Kanal und Router-Adresse, übersteht den Tiefschlaf: schnelles Neuverbinden
RTC_DATA_ATTR static char rtcSsid[33];
RTC_DATA_ATTR static uint8_t rtcBssid[6];
RTC_DATA_ATTR static int32_t rtcChannel = 0;

enum WifiState : uint8_t { W_IDLE, W_TRYING, W_SCANNING, W_USERSCAN };
static WifiState wst = W_IDLE;
static uint32_t wAt = 0;
static bool wasOnline = false, manual = false, ntpStarted = false, mdnsStarted = false;

bool netOnline() { return WiFi.status() == WL_CONNECTED; }
const char* netSsid() { return rtcSsid; }
String netIp() { return WiFi.localIP().toString(); }
int netRssi() { return WiFi.RSSI(); }
void netManual(bool on) { manual = on; }

int netChannel() {
  uint8_t ch = RADIO_CHANNEL;
  wifi_second_chan_t sc;
  esp_wifi_get_channel(&ch, &sc);
  return ch;
}

bool timeValid() { return time(nullptr) > 1700000000; }

void timeFromPeer(uint32_t epoch) {
  if (timeValid() || epoch < 1700000000) return;
  timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  Serial.println("Uhrzeit von einem Funk-Nachbarn übernommen");
}

// Ohne Verbindung auf dem festen Funkkanal bleiben, damit sich die Geräte per ESP-NOW finden
static void parkChannel() {
  if (WiFi.status() != WL_CONNECTED && wst != W_SCANNING && wst != W_USERSCAN && wst != W_TRYING &&
      netChannel() != RADIO_CHANNEL)
    esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

void netConnect(const char* ssid, const char* pass) {
  WiFi.disconnect();
  delay(20);
  WiFi.begin(ssid, strlen(pass) ? pass : nullptr);
  wst = W_TRYING;
  wAt = millis();
}

static void startScan() {
  WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  wst = W_SCANNING;
  wAt = millis();
}

void netScanStart() {
  if (WiFi.status() != WL_CONNECTED && wst == W_TRYING) WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  wst = W_USERSCAN;
  wAt = millis();
}

int netScanResult(ScanNet* out, int max) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n < 0) return 0;
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) if (s == out[j].ssid) { dup = true; break; }
    if (dup) continue;
    snprintf(out[k].ssid, sizeof(out[k].ssid), "%s", s.c_str());
    out[k].rssi = WiFi.RSSI(i);
    out[k].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    k++;
  }
  // stärkste zuerst
  for (int i = 0; i < k; i++)
    for (int j = i + 1; j < k; j++)
      if (out[j].rssi > out[i].rssi) { ScanNet t = out[i]; out[i] = out[j]; out[j] = t; }
  WiFi.scanDelete();
  wst = W_IDLE;
  wAt = millis();
  return k;
}

void netPoll() {
  uint32_t now = millis();
  if (netOnline()) {
    if (!wasOnline) {
      wasOnline = true;
      wst = W_IDLE;
      snprintf(rtcSsid, sizeof(rtcSsid), "%s", WiFi.SSID().c_str());
      memcpy(rtcBssid, WiFi.BSSID(), 6);
      rtcChannel = WiFi.channel();
      if (!ntpStarted) { configTzTime(TIMEZONE, "pool.ntp.org", "time.cloudflare.com"); ntpStarted = true; }
      if (!mdnsStarted) {
        char host[24];
        snprintf(host, sizeof(host), "taschenfunk-%04x", (uint16_t)(ESP.getEfuseMac() >> 32));
        mdnsStarted = MDNS.begin(host);
      }
      Serial.printf("WLAN: verbunden mit %s, IP %s, Kanal %d\n", rtcSsid, netIp().c_str(), (int)rtcChannel);
    }
    return;
  }
  if (wasOnline) {
    wasOnline = false;
    Serial.println("WLAN: Verbindung verloren");
    const char* pw = knownPass(rtcSsid);
    if (pw) { netConnect(rtcSsid, pw); return; }
  }
  if (wst == W_USERSCAN) {
    if (now - wAt > 20000) { WiFi.scanDelete(); wst = W_IDLE; }
    return;
  }
  if (manual || !knownCount) { parkChannel(); return; }
  if (wst == W_TRYING && now - wAt > 12000) {
    Serial.println("WLAN: kein Erfolg, suche bekannte Netze");
    startScan();
  } else if (wst == W_IDLE && now - wAt > 60000) {
    startScan();
  } else if (wst == W_SCANNING) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - wAt < 15000) return;
    int best = -1, bestRssi = -1000;
    for (int i = 0; i < n; i++)
      if (knownIndex(WiFi.SSID(i).c_str()) >= 0 && WiFi.RSSI(i) > bestRssi) { best = i; bestRssi = WiFi.RSSI(i); }
    if (best >= 0) {
      String ss = WiFi.SSID(best);
      Serial.printf("WLAN: versuche %s (%d dBm)\n", ss.c_str(), bestRssi);
      WiFi.scanDelete();
      netConnect(ss.c_str(), knownPass(ss.c_str()));
    } else {
      WiFi.scanDelete();
      wst = W_IDLE;
      wAt = now;
      parkChannel();
    }
  } else {
    parkChannel();
  }
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);  // Audio-Strom und ESP-NOW vertragen keinen Modem-Schlaf
  setenv("TZ", TIMEZONE, 1);
  tzset();
  if (!knownCount) { parkChannel(); return; }
  const char* pw = rtcChannel ? knownPass(rtcSsid) : nullptr;
  if (pw) {
    WiFi.begin(rtcSsid, strlen(pw) ? pw : nullptr, rtcChannel, rtcBssid);
    Serial.printf("WLAN: schnell zurück zu %s (Kanal %d)\n", rtcSsid, (int)rtcChannel);
  } else {
    WiFi.begin(known[0].ssid, strlen(known[0].pass) ? known[0].pass : nullptr);
    Serial.printf("WLAN: versuche %s\n", known[0].ssid);
  }
  wst = W_TRYING;
  wAt = millis();
}
