# Taschenfunk

Firmware für das **Waveshare ESP32-S3-Touch-AMOLED-1.75C** (rundes 466 × 466 AMOLED, Touch, Lautsprecher,
zwei Mikrofone, Akku): ein YouTube-Music-Player, eine Taschenuhr, Timer, Stoppuhr, Wecker und ein Walkie-Talkie
zwischen mehreren Geräten.

![Bildschirme](docs/screens.png)

*Bildschirmfotos vom echten Gerät (`tools/remote.py`): Uhr, Apps, Musik, Einstellungen, Timer, Stoppuhr, Funk, Tastatur.*

```
firmware/   PlatformIO-Projekt (Arduino-Core 3.3, LVGL 9.2)
bridge/     YT-Music-Brücke (Python) – läuft auf einem Rechner im WLAN
tools/      Fernsteuerung/Bildschirmfotos über USB, Schrift-Generator
```

## Was es kann

| App | Funktion |
|---|---|
| **Uhr** (Startseite) | Fünf Zifferblätter, **lange drücken** zum Wechseln: **Taschenuhr** (Blattzeiger, kleine Sekunde, Komplikation für Timer/Stoppuhr/Titel) sowie vier Designs aus [FaceLab](https://github.com/tobwil/garminwatchface) – **Daylight** (echter Sonnenbogen, Himmelsfarbe, Goldene/Blaue Stunde, Mondphase, Temperatur), **Sternenhimmel** (der echte Himmel über dem Standort mit Sternbildern, Milchstraße, Sonne und Mond), **Kiesel Text** („zehn / zweiund / vierzig“) und **Glitch** (zerfällt im Takt der Musik). Bei jedem Aufwachen läuft das Intro (Ziffern schreiben sich, der Himmel dreht sich ein). |
| **Musik** | Bibliothek der Brücke: Suchen, Entdecken (Mixe der Startseite), mit Anmeldung auch Lieblingssongs, Playlists, Verlauf. Interpreten, Alben, Playlists, Radio zu jedem Titel (Titel lange drücken). Wiedergabe mit rundem Cover und Fortschritt am Rand. **Lautstärke:** Finger am Rand entlang ziehen (im Uhrzeigersinn lauter, eine Umdrehung = 0 … 100 %; geht auch auf dem Zifferblatt, solange Musik läuft) oder Lautsprecher-Knopf bzw. Cover antippen für den großen Regler mit − / +. Die Lautstärke gleitet weich (Software-Rampe im Mischer, gehörrichtige Kurve). Läuft die Warteschlange aus, geht es mit einem Radio zum letzten Titel weiter. |
| **Funk** | Walkie-Talkie: großen Knopf halten und sprechen. In der Nähe per **ESP-NOW** (ohne Router), im selben WLAN zusätzlich per UDP-Multicast. Empfänger wählbar (alle oder ein Gerät), „Anklopfen“ spielt beim anderen einen Ton. Die Uhrzeit wird von Nachbarn übernommen, wenn ein Gerät kein WLAN hat. |
| **Timer** | Minuten/Sekunden wählen, Restzeit als Ring am Rand, klingelt auch aus dem Display-Aus heraus. |
| **Stoppuhr** | Zehntelsekunden, Runden, Sekundenring. |
| **Wecker** | Uhrzeit + an/aus, „+5 Min“ zum Schlummern; weckt das Gerät auch aus dem Tiefschlaf. |
| **Einstellungen** | Sieben Punkte: WLAN · Helligkeit · Lautstärke · Display aus · Funk (Name, im Hintergrund hören) · Musik-Brücke · Über das Gerät (Akku, IP, Neustart, Ausschalten). |

## Bedienung

Aufgebaut wie bei Pixel Watch (Wear OS) und Apple Watch: das Zifferblatt ist die Mitte, alles andere liegt
eine Wischbewegung entfernt.

| Wo | Geste / Taste | Wirkung |
|---|---|---|
| Zifferblatt | ↑ hochwischen | App-Liste (Musik, Funk, Timer, Stoppuhr, Wecker, Wetter, Zifferblatt, Einstellungen) |
| Zifferblatt | ↓ runterwischen | Kontrollzentrum: Lautstärke, Helligkeit, Funk-Empfang, Taschenlampe, Display aus, Einstellungen; oben Uhrzeit, Akku, WLAN |
| Zifferblatt | ← nach links wischen | Kacheln: Musik · Wetter · Timer · Funk (seitlich blättern, mit Direktbedienung) |
| Zifferblatt | lange drücken | Zifferblatt wählen |
| Zifferblatt | am Rand entlang ziehen | Lautstärke (solange Musik läuft) |
| überall | → nach rechts wischen | zurück (Kontrollzentrum auch ↑) |
| überall | **BOOT** kurz (wie die Krone) | auf dem Zifferblatt: Apps · sonst: zurück zum Zifferblatt · Klingeln: Stopp |
| überall | **BOOT** halten | Funk an alle (Push-to-Talk), auch bei dunklem Display |
| überall | **PWR** kurz | Display an/aus |
| überall | **PWR** lang (1,5 s) | Ausschalten / Neustart (6 s halten schaltet hart aus) |

Das Display geht nach der eingestellten Zeit aus; Antippen, BOOT oder PWR wecken es. Im Akkubetrieb geht das
Gerät 10 Minuten nach dem Display-Aus in den Tiefschlaf, wenn keine Musik, kein Timer und kein Funk läuft
(`DEEP_SLEEP_MIN` in `firmware/include/config.h`). Am USB-Kabel schläft es nie.

## 1. Musik-Brücke starten

YouTube Music lässt sich auf einem ESP32 nicht direkt abspielen (Stream-Signaturen, Opus/AAC). Die Brücke
erledigt das auf einem Rechner im selben WLAN und schickt der Uhr fertigen Ton (32 kHz PCM) und runde Cover.

```bash
brew install ffmpeg            # Raspberry Pi/Debian: sudo apt install ffmpeg python3-venv
cd bridge
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python server.py
```

Die Brücke meldet sich per mDNS (`_ytbridge._tcp`), die Uhr findet sie von selbst. Sonst unter
Einstellungen → Musik-Brücke die Adresse eintragen (z. B. `http://192.168.178.20:8765`).

**Handy als Fernbedienung:** die Adresse der Brücke im Handy-Browser öffnen – suchen, Titel antippen, die Uhr
spielt ihn. Lautstärke, Weiter/Zurück und Radio gehen auch.

**Anmeldung (optional)** für Lieblingssongs, Playlists und Verlauf:

```bash
cd bridge
.venv/bin/ytmusicapi browser   # Anfrage-Header aus music.youtube.com einfügen (Anleitung von ytmusicapi)
```

Das legt `bridge/browser.json` an (steht in `.gitignore`). Ohne Anmeldung gehen Suche, Radio, Interpreten,
Alben und „Entdecken“.

Hinweis: Die Brücke nutzt `yt-dlp` und `ytmusicapi`, beides inoffizielle Schnittstellen. Für den privaten
Gebrauch gedacht; wenn YouTube etwas ändert, hilft meist `.venv/bin/pip install -U yt-dlp ytmusicapi`.

## 2. Firmware aufspielen

```bash
cd firmware
pio run -t upload        # per USB-C
pio device monitor       # Log
```

Erkennt der Rechner das Board nicht: **BOOT** halten, kurz **PWR** drücken (bzw. USB neu einstecken), BOOT
loslassen und erneut hochladen.

Danach am Gerät: Apps → Einstellungen → WLAN → Netz antippen → Passwort. Bis zu 6 Netze werden gemerkt, das
stärkste bekannte wird automatisch gewählt.

## Wetter und Standort

Wetter kommt von [Open-Meteo](https://open-meteo.com) (kostenlos, ohne Schlüssel), der Standort beim ersten
Mal per IP von ip-api.com – beides über HTTP, damit kein Speicher für TLS nötig ist. Aktualisiert wird alle
15 Minuten. Sonne, Mond und Sterne berechnet die Uhr selbst aus Standort und Uhrzeit.

## Funk mit mehreren Geräten

Alle Geräte mit dieser Firmware finden sich automatisch. Damit ESP-NOW funktioniert, müssen sie auf demselben
WLAN-Kanal funken: Geräte im selben WLAN tun das ohnehin (und reden zusätzlich übers Netz), Geräte ohne WLAN
bleiben auf Kanal 1 (`RADIO_CHANNEL`). Ein Gerät im Heim-WLAN auf Kanal 6 hört ein Gerät unterwegs auf
Kanal 1 also nicht – dafür im Router Kanal 1 einstellen oder `RADIO_CHANNEL` anpassen.

Ton: 16 kHz, IMA-ADPCM, ca. 28 ms pro Paket; Halbduplex wie ein echtes Funkgerät, am Ende ein Roger-Piep.
Musik wird leiser, solange jemand spricht.

## Testen über USB

```bash
~/.platformio/penv/bin/python tools/remote.py /dev/cu.usbmodem* tap 233 233 wait 1 shot bild.png
```

Befehle im seriellen Monitor: `tap X Y`, `hold X Y`, `swipe l|r|u|d`, `boot`, `pwr`, `ptt down` / `ptt up`,
`shot`, `page apps|music|timer|stopwatch|alarm|radio|settings`, `home`, `browse search:Coldplay`,
`wifi SSID PASSWORT`, `face 0…4`, `bezel GRAD`, `dump`, `bridge http://…` bzw. `bridge auto`, `toggle`, `next`, `vol 60`, `timer 10`,
`status`, `mem`.

## Technik

- **Display:** CO5300 über QSPI (Arduino_GFX 1.6.9), LVGL 9.2 mit Teil-Puffern im internen RAM, LVGL-Speicher
  im PSRAM. Zifferblatt einmal in einen PSRAM-Puffer gerendert, die Zeiger werden pro Bild darüber gezeichnet.
- **Schrift:** Inter (Text, Ziffern), Cormorant Garamond (Zifferblatt), Material Icons Round – erzeugt mit
  `tools/make_fonts.py` (lv_font_conv), Umlaute und Icons in allen Textschriften.
- **Audio:** ES8311 + ES7210 im Vollduplex an einem I2S-Bus mit 32 kHz; ein Mischer-Task auf Kern 0 mischt
  Musik, Funk-Stimme und Klänge, nimmt das Mikrofon auf 16 kHz auf und schaltet den Verstärker nur bei Bedarf.
- **Akku/PWR:** AXP2101 per I2C (Ladezustand, PWR kurz/lang, Ausschalten).
- **Module:** `hw` (Display/Touch/Tasten/Akku/USB-Fernsteuerung), `audio`, `net` (WLAN, NTP), `music`
  (Brücken-Client, Strom, Warteschlange, Fernbedienung), `radio` (ESP-NOW/UDP, ADPCM), `clockwork`
  (Timer/Stoppuhr/Wecker), `weather`, `faces/` (Zeichen-Kontext im Garmin-Stil mit Strichschrift und Astronomie, die vier FaceLab-Designs), `ui/` (Seiten).

## Stand

Auf dem Gerät geprüft: Display, Schriften, alle Seiten, Touch-Eingaben (über USB eingespielt), Tastatur,
WLAN-Suche, Akku-Anzeige, Timer-Klingeln, Funk-Senden ohne Absturz. Brücke am Mac geprüft: Suche,
Interpreten, Alben, Playlists, Radio, Entdecken, Stream (bereit nach ~1,5 s), Cover.

Noch offen: Musik Ende-zu-Ende auf dem Gerät (braucht WLAN), Funk mit einem zweiten Gerät, Wischrichtung
mit dem Finger (`TOUCH_MIRROR_X/Y` in `config.h`, falls verkehrt), Akkulaufzeit.
