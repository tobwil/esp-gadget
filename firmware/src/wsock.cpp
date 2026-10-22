#include "wsock.h"
#include <esp_random.h>
#include <mbedtls/base64.h>

extern const uint8_t crtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crtBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace wsock {

static const size_t MAX_MSG = 768 * 1024;

Client::~Client() {
  close();
  if (buf_) heap_caps_free(buf_);
}

bool Client::grow(size_t need) {
  if (need <= cap_) return true;
  if (need > MAX_MSG) return false;
  size_t n = max(need, cap_ ? cap_ * 2 : (size_t)65536);
  n = min(n, MAX_MSG);
  char* b = (char*)heap_caps_realloc(buf_, n, MALLOC_CAP_SPIRAM);
  if (!b) return false;
  buf_ = b;
  cap_ = n;
  return true;
}

bool Client::connect(const char* host, const String& path, uint32_t timeoutMs) {
  close();
  tls_.setCACertBundle(crtBundleStart, crtBundleEnd - crtBundleStart);
  tls_.setTimeout(timeoutMs / 1000);
  if (!tls_.connect(host, 443, timeoutMs)) return false;
  uint8_t key[16];
  esp_fill_random(key, sizeof(key));
  unsigned char k64[32];
  size_t kl = 0;
  mbedtls_base64_encode(k64, sizeof(k64), &kl, key, sizeof(key));
  k64[kl] = 0;
  String req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
               "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
               (const char*)k64 + "\r\nUser-Agent: Taschenfunk/1.0\r\n\r\n";
  tls_.write((const uint8_t*)req.c_str(), req.length());
  // Antwortkopf bis zur Leerzeile lesen
  String head;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs && !head.endsWith("\r\n\r\n")) {
    int c = tls_.read();
    if (c < 0) { delay(2); continue; }
    head += (char)c;
    if (head.length() > 4096) break;
  }
  if (head.indexOf(" 101") < 0) {
    Serial.printf("WebSocket: Handshake fehlgeschlagen: %s\n", head.substring(0, head.indexOf('\r')).c_str());
    tls_.stop();
    return false;
  }
  open_ = true;
  return true;
}

bool Client::connected() { return open_ && tls_.connected(); }

void Client::close() {
  if (open_ && tls_.connected()) sendFrame(0x8, nullptr, 0);
  open_ = false;
  tls_.stop();
}

bool Client::sendFrame(uint8_t opcode, const uint8_t* data, size_t len) {
  uint8_t h[14];
  size_t hl = 0;
  h[hl++] = 0x80 | opcode;
  if (len < 126) h[hl++] = 0x80 | len;
  else if (len < 65536) { h[hl++] = 0x80 | 126; h[hl++] = len >> 8; h[hl++] = len; }
  else {
    h[hl++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) h[hl++] = (uint64_t)len >> (8 * i);
  }
  uint8_t mask[4];
  esp_fill_random(mask, 4);
  memcpy(h + hl, mask, 4);
  hl += 4;
  if (tls_.write(h, hl) != hl) { open_ = false; return false; }
  // Nutzdaten maskiert in Stücken schicken
  uint8_t chunk[1024];
  for (size_t off = 0; off < len;) {
    size_t n = min(sizeof(chunk), len - off);
    for (size_t i = 0; i < n; i++) chunk[i] = data[off + i] ^ mask[(off + i) & 3];
    size_t w = 0;
    uint32_t t0 = millis();
    while (w < n && millis() - t0 < 5000) {
      int r = tls_.write(chunk + w, n - w);
      if (r > 0) w += r; else delay(2);
    }
    if (w < n) { open_ = false; return false; }
    off += n;
  }
  return true;
}

bool Client::sendText(const char* data, size_t len) { return open_ && sendFrame(0x1, (const uint8_t*)data, len); }

bool Client::readExact(uint8_t* p, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    int r = tls_.read(p + got, n - got);
    if (r > 0) { got += r; t0 = millis(); continue; }
    if (!tls_.connected() || millis() - t0 > timeoutMs) return false;
    delay(1);
  }
  return true;
}

int Client::recv(const char*& msg, uint32_t waitMs) {
  if (!open_) return -1;
  uint32_t t0 = millis();
  while (!tls_.available()) {
    if (!tls_.connected()) { open_ = false; return -1; }
    if (millis() - t0 >= waitMs) return 0;
    delay(1);
  }
  size_t total = 0;
  for (;;) {
    uint8_t h[2];
    if (!readExact(h, 2, 8000)) { open_ = false; return -1; }
    uint8_t op = h[0] & 0x0F;
    bool fin = h[0] & 0x80;
    uint64_t len = h[1] & 0x7F;
    if (len == 126) { uint8_t e[2]; if (!readExact(e, 2, 8000)) { open_ = false; return -1; } len = (e[0] << 8) | e[1]; }
    else if (len == 127) {
      uint8_t e[8];
      if (!readExact(e, 8, 8000)) { open_ = false; return -1; }
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    bool masked = h[1] & 0x80;
    if (masked && !readExact(mask, 4, 8000)) { open_ = false; return -1; }
    if (op >= 0x8) {  // Steuerrahmen
      uint8_t ctl[125];
      size_t n = min((size_t)len, sizeof(ctl));
      if (!readExact(ctl, n, 8000)) { open_ = false; return -1; }
      if (op == 0x8) { open_ = false; tls_.stop(); return -1; }
      if (op == 0x9) sendFrame(0xA, ctl, n);  // Ping → Pong
      if (!total) {  // zwischen zwei Nachrichten: weiter auf Daten warten
        t0 = millis();
        while (!tls_.available()) {
          if (!tls_.connected()) { open_ = false; return -1; }
          if (millis() - t0 >= waitMs) return 0;
          delay(1);
        }
      }
      continue;
    }
    if (!grow(total + len + 1)) { open_ = false; tls_.stop(); return -1; }
    if (!readExact((uint8_t*)buf_ + total, len, 15000)) { open_ = false; return -1; }
    if (masked)
      for (size_t i = 0; i < len; i++) buf_[total + i] ^= mask[i & 3];
    total += len;
    if (fin) break;
  }
  buf_[total] = 0;
  msg = buf_;
  return (int)total;
}

}  // namespace wsock
