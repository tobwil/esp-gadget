// Weboberfläche zum Einrichten vom PC oder Handy aus: https://<IP der Uhr> (http:// leitet um).
// Gerätename, WLAN, Kindermodus, KI (Gemini, n8n), Favoriten, Infos.
// Schlüssel werden nie ausgelesen, nur gesetzt. Nur im eigenen WLAN erreichbar.
#pragma once
#include <esp_http_server.h>

namespace webui {

void begin();
void poll();                     // aus der Hauptschleife: führt die anstehende Anfrage aus
esp_err_t handle(httpd_req_t* r);  // vom HTTPS-Server (phone.cpp) für alles außer /funk und /ws

}  // namespace webui
