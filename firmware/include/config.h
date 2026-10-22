// Voreinstellungen – die meisten Werte lassen sich am Gerät unter „Einstellungen“ ändern und bleiben gespeichert.
#pragma once

// WLAN (optional). Leer lassen = am Gerät einrichten (Einstellungen → WLAN).
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Zeitzone Deutschland (inkl. Sommerzeit)
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// Spracheingabe: Gemini-Modell (Schlüssel in include/secrets.h)
#define GEMINI_MODEL "gemini-flash-lite-latest"

// Display aus nach so vielen Sekunden ohne Bedienung (am Gerät änderbar)
#define SCREEN_TIMEOUT_S 30
// Tiefschlaf nach so vielen Minuten mit dunklem Display (nur im Akkubetrieb, nicht bei Musik/Timer/Funk-Empfang).
// 0 = nie
#define DEEP_SLEEP_MIN 10

#define VOLUME_DEFAULT 60      // %
#define BRIGHTNESS_DEFAULT 80  // %

// Funk (ESP-NOW + UDP-Multicast im WLAN). Ohne WLAN-Verbindung funken alle Geräte auf diesem Kanal.
#define RADIO_CHANNEL 1
#define RADIO_MCAST_IP "239.255.42.99"
#define RADIO_PORT 4299

// Mikrofon (ES7210): Kanal 0 = MIC1, 1 = MIC2; Verstärkung analog 0–14 (10 ≈ 30 dB) und digital
#define MIC_CHANNEL 0
#define MIC_PGA 10
#define MIC_GAIN 3.0f

// Touch-Ausrichtung (wie im Waveshare-Beispiel für das 1.75C)
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 1
#define TOUCH_MIRROR_Y 1

// Balance-Spiel: Richtung der Neigung. Rollt die Kugel verkehrt herum, das Vorzeichen umdrehen.
#define IMU_SIGN_X 1
#define IMU_SIGN_Y 1

#define FW_VERSION "0.2.0"
