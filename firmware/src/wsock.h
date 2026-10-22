// Schlanker WebSocket-Client über TLS (für Gemini Live). Ein Task benutzt ihn, kein Threading.
#pragma once
#include <Arduino.h>
#include <NetworkClientSecure.h>

namespace wsock {

class Client {
 public:
  ~Client();
  bool connect(const char* host, const String& path, uint32_t timeoutMs = 10000);
  bool connected();
  void close();
  bool sendText(const char* data, size_t len);
  bool sendText(const String& s) { return sendText(s.c_str(), s.length()); }
  // Eine ganze Nachricht lesen (Fragmente zusammengesetzt, Ping beantwortet).
  // > 0: Länge, msg zeigt auf den nullterminierten Inhalt (gültig bis zum nächsten Aufruf)
  // 0: nichts da (innerhalb waitMs), −1: Verbindung zu
  int recv(const char*& msg, uint32_t waitMs);

 private:
  NetworkClientSecure tls_;
  char* buf_ = nullptr;
  size_t cap_ = 0;
  bool open_ = false;
  bool readExact(uint8_t* p, size_t n, uint32_t timeoutMs);
  bool sendFrame(uint8_t opcode, const uint8_t* data, size_t len);
  bool grow(size_t need);
};

}  // namespace wsock
