#!/usr/bin/env python3
"""Regressionstests auf der Uhr: über USB bedienen (antippen, Texte lesen, Bildschirmfotos, Neigung vortäuschen),
über WLAN die Weboberfläche abfragen, und für die Monsterjagd (mit --sound) Bürstenrauschen aus dem Lautsprecher
des Rechners abspielen.

    ~/.platformio/penv/bin/python tests/device/test_device.py [/dev/cu.usbmodem…] [--sound] [--no-full]

  Leise: die Uhr steht während der Tests auf Lautstärke 0 (Klingeln, Radio, Ansagen sind stumm).
  --sound     zusätzlich die Monsterjagd mit Bürstenrauschen aus dem Lautsprecher des Rechners prüfen –
              das ist hörbar, also nicht nachts
  --no-full   ohne den vollen Zwei-Minuten-Durchlauf
  --only a,b  nur diese Tests (Namen wie in der Ausgabe; boot_und_status läuft immer mit)

Geprüft wird die ganze Uhr, nicht nur das Neue: Weboberfläche (Felder, Umleitung, keine Geheimnisse, Einstellungen
hin und zurück, Update-Schutz), Kinder-Zifferblatt zu allen Tageszeiten, im Erwachsenen-Modus alle Zifferblätter,
Wischen, Kontrollzentrum, Apps, Timer mit Klingeln, Balance-Spiel, ein Radiosender, Funk-Seite – und die Zahnputz-App.

Vorher werden Album, Serie und Rekorde der Zahnputz-App gesichert („brush save“) und danach zurückgeholt
(„brush load“); die Lautstärke steht während der Tests auf 0 und danach wieder wie vorher.
Bildschirmfotos landen in tests/device/out/.
"""
import glob
import json
import math
import os
import re
import ssl
import struct
import subprocess
import sys
import tempfile
import time
import traceback
import urllib.error
import urllib.request
import wave
import zlib

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
SOUND = "--sound" in sys.argv
TEST_VOLUME = 0
FULL = "--no-full" not in sys.argv
ONLY = next((a.split("=", 1)[1].split(",") for a in sys.argv if a.startswith("--only=")), None)


def rgb565(c):
    return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F)


# ------------------------------------------------------------------ Uhr über USB

class Watch:
    def __init__(self, path):
        self.port = serial.Serial(path, 115200, timeout=0.2)
        time.sleep(0.3)
        self.port.reset_input_buffer()

    def send(self, line, pause=0.15):
        self.port.write((line + "\n").encode())
        time.sleep(pause)

    def lines(self, secs):
        out, end = [], time.time() + secs
        while time.time() < end:
            ln = self.port.readline()
            if ln:
                out.append(ln.decode(errors="replace").rstrip())
        return out

    def ask(self, line, prefix, secs=3):
        self.port.reset_input_buffer()
        self.send(line, 0)
        end = time.time() + secs
        while time.time() < end:
            ln = self.port.readline().decode(errors="replace").strip()
            if ln.startswith(prefix):
                return ln
        return None

    def status(self):
        ln = self.ask("status", "WLAN")
        if not ln:
            return None
        m = re.search(r"Heap (\d+)(?: · Laufzeit (\d+) s)?", ln)  # ältere Firmware: ohne Laufzeit
        ip = re.match(r"WLAN an (\S+)", ln)
        up = int(m.group(2)) if m and m.group(2) else None
        return {"heap": int(m.group(1)), "up": up, "ip": ip.group(1) if ip else None, "line": ln} if m else None

    def labels(self):
        self.port.reset_input_buffer()
        self.send("dump", 0)
        out = []
        for ln in self.lines(0.9):
            m = re.search(r"\[(-?\d+),(-?\d+) (\d+)x(\d+)\]( versteckt)? „(.*)“", ln)
            if m:
                x, y, w, h = map(int, m.groups()[:4])
                if not m.group(5) and w > 0 and h > 0:
                    out.append((x, y, w, h, m.group(6)))
        return out

    def texts(self):
        return [l[4] for l in self.labels()]

    def has(self, text):
        return any(text in t for t in self.texts())

    def wait_for(self, text, secs=10):
        end = time.time() + secs
        while time.time() < end:
            if self.has(text):
                return True
            time.sleep(0.4)
        return False

    def tap(self, text):
        for x, y, w, h, t in self.labels():
            if t == text or text in t:
                self.send(f"tap {x + w // 2} {y + h // 2}", 0.6)
                return True
        raise AssertionError(f"„{text}“ nicht auf dem Bildschirm (zu sehen: {self.texts()})")

    def shot(self, name=None):
        for _ in range(3):
            self.port.reset_input_buffer()
            self.send("shot", 0)
            buf, end = b"", time.time() + 8
            while b"\nFB" not in buf and time.time() < end:
                buf += self.port.read(4096)
            if b"\nFB" not in buf:
                continue
            buf = buf.split(b"\nFB", 1)[1]
            while b"\n" not in buf and time.time() < end:
                buf += self.port.read(16)
            size, buf = buf.split(b"\n", 1)
            W = int(size)
            end = time.time() + 20
            while len(buf) < W * W * 2 and time.time() < end:
                buf += self.port.read(W * W * 2 - len(buf))
            if len(buf) >= W * W * 2:
                img = Shot(W, buf[:W * W * 2])
                if name:
                    img.save(os.path.join(OUT, name + ".png"))
                return img
        raise AssertionError("kein Bildschirmfoto")


class Shot:
    def __init__(self, W, raw):
        self.W, self.raw = W, raw
        self.px = struct.unpack(f"<{W * W}H", raw)

    def where(self, colors, x0=0, y0=0, x1=None, y1=None):
        """Pixel mit einer der Farben (24-Bit-Werte wie in kidart) im Bereich: Anzahl und Mittelpunkt/Begrenzung."""
        want = {rgb565(c) for c in colors}
        x1, y1 = x1 or self.W, y1 or self.W
        n = sx = sy = 0
        top, bottom = 1 << 30, -1
        for y in range(y0, y1):
            row = y * self.W
            for x in range(x0, x1):
                if self.px[row + x] in want:
                    n += 1
                    sx += x
                    sy += y
                    top, bottom = min(top, y), max(bottom, y)
        return {"n": n, "x": sx / n if n else None, "y": sy / n if n else None, "top": top if n else None, "bottom": bottom if n else None}

    def save(self, path):
        W, rows = self.W, bytearray()
        for y in range(W):
            rows.append(0)
            for x in range(W):
                v = self.px[y * W + x]
                rows += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, W, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


# ------------------------------------------------------------------ WLAN

CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE  # die Uhr hat ein selbst ausgestelltes Zertifikat


def http(url, data=None, method=None):
    req = urllib.request.Request(url, data=json.dumps(data).encode() if data is not None else None, method=method,
                                 headers={"Content-Type": "application/json"} if data is not None else {})
    try:
        with urllib.request.urlopen(req, timeout=15, context=CTX) as r:
            return r.status, r.read(), dict(r.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read(), dict(e.headers)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


# ------------------------------------------------------------------ Töne aus dem Rechner

def tone(path, secs, f=None, noise=False, amp=0.5):
    """WAV: Summen (Grundton + Obertöne) oder Bürstenrauschen."""
    sr, n = 44100, int(secs * 44100)
    seed, prev = 7, 0.0
    frames = bytearray()
    for i in range(n):
        if noise:
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            x = seed / 0x3FFFFFFF - 1
            v = x * 0.7  # breitbandig: der Mikrofonweg der Uhr reicht nur bis 8 kHz
        else:
            t = i / sr
            v = (math.sin(2 * math.pi * f * t) + 0.5 * math.sin(4 * math.pi * f * t + 1) + 0.3 * math.sin(6 * math.pi * f * t + 2)) / 1.8
        env = min(1, i / 2000, (n - i) / 2000)
        frames += struct.pack("<h", int(max(-1, min(1, v * amp * env)) * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(frames))


SOUNDS = {}


def play(name, wait=True):
    p = subprocess.Popen(["afplay", SOUNDS[name]])
    if wait:
        p.wait()
    return p


# ------------------------------------------------------------------ Tests

results = []


def test(fn):
    results.append(fn)
    return fn


def score(w):
    for t in w.texts():
        m = re.search(r"· (\d+) (Monster|Treffer|Sterne|Schätze)", t)
        if m:
            return int(m.group(1))
    return None


def open_game(w, game, hero="Pferd Luna"):
    w.send("home", 0.8)
    w.send("page brush", 1.2)
    assert w.wait_for("Wer putzt mit?", 5), "Zahnputz-App startet nicht"
    w.tap(hero)
    assert w.wait_for("Album", 4), "Spielauswahl fehlt"
    assert w.has(game), f"{game} fehlt in der Spielauswahl"
    w.tap(game)
    assert w.wait_for(game, 3), f"{game} startet nicht"


def wait_section(w, secs=25):
    assert w.wait_for("Oben rechts", secs), f"erster Abschnitt beginnt nicht (zu sehen: {w.texts()})"


@test
def boot_und_status(w, ctx):
    s = w.status()
    assert s, "keine Antwort auf „status“"
    assert s["ip"], f"kein WLAN: {s['line']}"
    ctx["ip"] = s["ip"]


@test
def weboberflaeche(w, ctx):
    ip = ctx["ip"]
    code, body, _ = http(f"https://{ip}/api/state")
    assert code == 200, f"/api/state: {code}"
    st = json.loads(body)
    for k in ("name", "battery", "kids", "music"):
        assert k in st, f"/api/state ohne „{k}“"
    br = st["kids"].get("brush", {})
    for k in ("streak", "best", "monster", "ball", "catcher", "meadow", "total", "names"):
        assert k in br, f"Zahnputz-Werte ohne „{k}“"
    for k in ("pitch", "balloon"):  # Summ-Spiele sind entfernt
        assert k not in br, f"Zahnputz-Werte noch mit „{k}“"
    assert br["total"] == 26, f"{br['total']} Sticker statt 26"
    assert "brushParent" in st["kids"] and "brushHard" not in st["kids"], "Einstellung Nachputz-Hinweis fehlt"
    for k in ("tempBat", "tempCpu"):  # Ladechip und Prozessor
        assert k in st["power"] and 10 <= st["power"][k] <= 80, f"Temperatur {k} fehlt oder unplausibel: {st['power'].get(k)}"
    code, body, _ = http(f"https://{ip}/")
    page = body.decode(errors="replace")
    assert code == 200 and "Nachputz-Hinweis" in page and "kbrushpar" in page, "Seite ohne Zahnputz-Karte"
    assert "kbrush\"" not in page and "Rätsel" not in page, "alte Rätsel-Einstellung noch da"
    code, _, _ = http(f"https://{ip}/api/gibtsnicht")
    assert code == 404, f"unbekannte Adresse: {code} statt 404"
    opener = urllib.request.build_opener(NoRedirect)
    try:
        opener.open(f"http://{ip}/", timeout=10)
        assert False, "http ohne Umleitung"
    except urllib.error.HTTPError as e:
        assert e.code in (301, 302, 307, 308) and e.headers.get("Location", "").startswith("https://"), f"http: {e.code}"
    # Einstellung umschalten und zurück (ohne PIN nicht erlaubt – dann nur prüfen, dass es sauber abgelehnt wird)
    was = st["kids"]["brushParent"]
    code, _, _ = http(f"https://{ip}/api/kids", {"brushParent": not was})
    if code == 200:
        st2 = json.loads(http(f"https://{ip}/api/state")[1])
        assert st2["kids"]["brushParent"] == (not was), "Nachputz-Hinweis lässt sich nicht umschalten"
        http(f"https://{ip}/api/kids", {"brushParent": was})
        st3 = json.loads(http(f"https://{ip}/api/state")[1])
        assert st3["kids"]["brushParent"] == was, "Nachputz-Hinweis nicht zurückgestellt"
    else:
        assert code in (401, 403), f"/api/kids: {code}"


@test
def seiten_oeffnen(w, ctx):
    before = w.status()
    for p in ("apps", "music", "timer", "stopwatch", "alarm", "radio", "settings", "game"):
        w.send("home", 0.8)
        w.send(f"page {p}", 1.5)
        t = w.texts()
        assert t, f"Seite {p}: leer"
        w.shot(f"seite_{p}")
    w.send("home", 1)
    after = w.status()
    assert after["up"] >= before["up"], "Uhr hat neu gestartet"


@test
def zahnputz_menue(w, ctx):
    w.send("home", 0.8)
    w.send("page brush", 1.2)
    assert w.wait_for("Wer putzt mit?", 5)
    assert w.has("Pferd Luna") and w.has("Reh Fips")
    w.shot("brush_auswahl")
    w.tap("Reh Fips")
    assert w.wait_for("Album", 4), "Spielauswahl fehlt"
    games = ["Monsterjagd", "Zahnpasta-Kugel", "Sterne fangen", "Wiesenlauf"]
    for g in games:
        assert w.has(g), f"{g} fehlt in der Spielauswahl"
    for gone in ("Summ-Ballon", "Hoch oder tief", "Summ-Spiele", "Kipp-Spiele"):
        assert not w.has(gone), f"„{gone}“ sollte weg sein"
    w.shot("brush_spiele")
    w.tap("Album")
    assert w.wait_for("Mein Album", 3) and w.has("von 26 Stickern")
    w.tap("Weiter")
    assert w.wait_for("Seite 2/3", 3), "Album blättert nicht"
    w.shot("brush_album")
    w.tap("Zurück")
    assert w.wait_for("Wiesenlauf", 3), "Zurück aus dem Album führt nicht zur Spielauswahl"
    w.send("home", 1)


@test
def sterne_fangen(w, ctx):
    BASKET = [0xe8bf72]
    open_game(w, "Sterne fangen")
    w.send("tilt -0.7 0", 0.8)
    left = w.shot("fangen_links").where(BASKET, y0=200, y1=330)
    w.send("tilt 0.7 0", 0.8)
    right = w.shot("fangen_rechts").where(BASKET, y0=200, y1=330)
    assert left["n"] > 50 and right["n"] > 50, "Korb nicht gefunden"
    assert right["x"] - left["x"] > 200, f"Korb folgt der Neigung nicht ({left['x']:.0f} → {right['x']:.0f})"
    wait_section(w)
    # dem tiefsten fallenden Stern bzw. Apfel hinterher
    end = time.time() + 25
    while time.time() < end:
        s = w.shot()
        best = None
        for y in range(240, 20, -8):
            hit = s.where([0xffd23f, 0xe74c3c], x0=20, y0=y, x1=446, y1=y + 8)
            if hit["n"] > 6:
                best = hit["x"]
                break
        tx = 0 if best is None else max(-0.45, min(0.45, ((best - 1) / 8 - 29) / 55))
        w.send(f"tilt {tx:.2f} 0", 0.1)
    sc = score(w)
    w.shot("fangen_ende")
    assert sc is not None and sc >= 2, f"nur {sc} gefangen"
    ctx["note_fangen"] = sc


@test
def zahnpasta_kugel(w, ctx):
    open_game(w, "Zahnpasta-Kugel")
    wait_section(w)
    end, last = time.time() + 30, None
    while time.time() < end and (score(w) or 0) < 2:
        s = w.shot()
        ball = s.where([0xff6b9a], y0=10, y1=390)  # nur der rosa Punkt: hellblau leuchten auch die Zähne
        star = s.where([0xffd23f, 0xfff27a], y0=10, y1=390)
        if not ball["n"] or not star["n"]:
            continue
        dx, dy = (star["x"] - ball["x"]) / 8, (star["y"] - ball["y"]) / 8
        dist = max(1e-3, (dx * dx + dy * dy) ** 0.5)
        push = min(0.5, 0.12 + dist * 0.02)
        w.send(f"tilt {dx / dist * push:.2f} {dy / dist * push:.2f}", min(0.9, 0.15 + dist * 0.03))
        w.send("tilt 0 0", 0.7)  # ausrollen lassen, bevor das nächste Foto kommt (der Stern wechselt alle 10 s)
    sc = score(w)
    w.shot("kugel_ende")
    assert sc is not None and sc >= 2, f"Kugel nur {sc}-mal am Stern"


@test
def wiesenlauf(w, ctx):
    LUNA = [0xb5703a]
    open_game(w, "Wiesenlauf")
    w.send("tilt 0 -0.7", 0.9)
    up = w.shot("wiese_oben").where(LUNA, y0=30, y1=380)
    w.send("tilt 0 0.7", 0.9)
    down = w.shot("wiese_unten").where(LUNA, y0=30, y1=380)
    assert up["n"] > 30 and down["n"] > 30, "Luna nicht gefunden"
    assert down["y"] - up["y"] > 80, f"Luna folgt der Neigung nicht ({up['y']:.0f} → {down['y']:.0f})"
    w.send("tilt off", 0.2)


def heard(w, sound, kind):
    """Ton abspielen und mitschreiben, was die Uhr gehört hat; Rückgabe: Anzahl Prüfungen mit „TON“ bzw. „laut“."""
    play("wake")  # Lautsprecher des Rechners aufwecken, er schläft nach ein paar Sekunden Stille wieder ein
    w.send("ear on", 0)
    p = play(sound, wait=False)
    lines = []
    while p.poll() is None:
        lines += w.lines(0.3)
    lines += w.lines(0.3)
    w.send("ear off", 0.2)
    return sum(1 for l in lines if l.startswith("ohr ") and (kind in l or (kind == "laut" and "TON" in l)))


def listening(w, secs=10):
    """Warten, bis die Uhr wieder zuhört (nach einer Ansage): „ear on“ schreibt dann je Prüfung eine Zeile."""
    w.send("ear on", 0)
    end = time.time() + secs
    ok = False
    while time.time() < end and not ok:
        ok = any(l.startswith("ohr ") for l in w.lines(0.5))
    w.send("ear off", 0.2)
    assert ok, "die Uhr hört nicht zu"


@test
def monsterjagd_hoert_putzen(w, ctx):
    if not SOUND:
        return "übersprungen"
    # Der Lautsprecher des Rechners setzt manchmal verspätet ein: bis zu drei Versuche, gewertet wird nur,
    # wenn die Uhr das Rauschen nachweislich gehört hat (Ohr-Protokoll)
    open_game(w, "Monsterjagd")
    wait_section(w)
    for attempt in range(3):
        listening(w)  # Ansage ausreden lassen
        s0 = score(w) or 0
        n = heard(w, "brush", "laut")
        sc = score(w) or 0
        if n >= 20:
            break
    w.shot("monster_ende")
    assert n >= 20, f"die Uhr hat das Rauschen in drei Versuchen höchstens {n}-mal gehört (Lautsprecher des Rechners?)"
    assert sc - s0 >= 2, f"Bürstenrauschen {n}-mal gehört, aber nur {sc - s0} Monster weg"


def secrets():
    """Geheime Werte aus firmware/include/secrets.h (nur zum Vergleichen, nie ausgeben)."""
    path = os.path.join(HERE, "..", "..", "firmware", "include", "secrets.h")
    if not os.path.exists(path):
        return []
    return [v for v in re.findall(r'"([^"]{8,})"', open(path).read()) if not v.startswith("http")]


@test
def keine_geheimnisse(w, ctx):
    sec = secrets()
    assert sec, "secrets.h nicht gefunden"
    ip = ctx["ip"]
    for path in ("/api/state", "/api/backup", "/api/models", "/api/pm", "/", "/funk"):
        code, body, _ = http(f"https://{ip}{path}")
        assert code == 200, f"{path}: {code}"
        text = body.decode(errors="replace")
        leaked = sum(1 for v in sec if v in text)
        assert not leaked, f"{path} enthält {leaked} geheime(n) Wert(e)"  # Werte selbst nicht ausgeben
    bk = json.loads(http(f"https://{ip}/api/backup")[1])
    ctx["backup"] = bk


@test
def einstellungen_hin_und_zurueck(w, ctx):
    ip = ctx["ip"]
    st = json.loads(http(f"https://{ip}/api/state")[1])
    d, p = st["display"], st["power"]
    assert http(f"https://{ip}/api/display", {"brightness": d["brightness"], "screenOff": d["screenOff"]})[0] == 200
    assert http(f"https://{ip}/api/power", {"mode": p["mode"], "deep": p["deep"], "nightKids": p["nightKids"],
                                             "nightAdult": p["nightAdult"], "nightFrom": p["nightFrom"], "nightTo": p["nightTo"]})[0] == 200
    st2 = json.loads(http(f"https://{ip}/api/state")[1])
    for k in ("brightness", "screenOff"):
        assert st2["display"][k] == d[k], f"Anzeige {k}: {d[k]} → {st2['display'][k]}"
    for k in ("mode", "deep", "nightKids", "nightAdult", "nightFrom", "nightTo"):
        assert st2["power"][k] == p[k], f"Strom {k}: {p[k]} → {st2['power'][k]}"
    code, body, _ = http(f"https://{ip}/api/display", data=None, method="POST")
    assert code == 400, f"leere Anfrage: {code} statt 400"


@test
def update_schutz(w, ctx):
    """Kaputte Datei und Firmware der eckigen Uhr müssen abgelehnt werden – ohne Neustart."""
    up0 = w.status()["up"]
    other = os.path.join(HERE, "..", "..", "firmware", ".pio", "build", "amoled-216", "firmware.bin")
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(b"\xe9kein echtes Abbild" * 2000)
        junk = f.name
    for name, path, expect in (("Datenmüll", junk, ""), ("Firmware der eckigen Uhr", other, "anderes Board")):
        if not os.path.exists(path):
            raise AssertionError(f"{path} fehlt – vorher bauen (sh tests/run_host.sh)")
        r = subprocess.run(["curl", "-sk", "--max-time", "200", "-F", f"file=@{path}", f"https://{ctx['ip']}/api/update"],
                           capture_output=True, text=True)
        assert "installiert" not in r.stdout, f"{name} wurde installiert!"
        assert expect in r.stdout, f"{name}: unerwartete Antwort {r.stdout[:120]}"
    os.unlink(junk)
    time.sleep(2)
    s = w.status()
    assert s and s["up"] >= up0, "Uhr hat nach dem abgelehnten Update neu gestartet"


@test
def kinder_zifferblatt(w, ctx):
    st = json.loads(http(f"https://{ctx['ip']}/api/state")[1])
    if not st["kids"]["on"]:
        return "übersprungen (Kindermodus aus)"
    try:
        for t in ("6:45", "7:10", "8:00", "12:00", "13:30", "16:00", "18:30", "19:15", "20:00", "23:00"):
            w.send(f"kidtime {t}", 0.2)
            w.send("home", 1.0)
            s = w.shot(f"kind_{t.replace(':', '')}")
            lit = sum(1 for v in s.px[::7] if v)
            assert lit > 2000, f"Kinder-Zifferblatt um {t} fast leer"
    finally:
        w.send("kidtime -1", 0.2)
        w.send("home", 0.8)


def find_key(o, key):
    if isinstance(o, dict):
        if key in o:
            return o[key]
        for v in o.values():
            r = find_key(v, key)
            if r is not None:
                return r
    return None


@test
def erwachsenen_modus(w, ctx):
    """Alles, was Kinder nicht sehen: Zifferblätter, Wischen, Kontrollzentrum, Apps, Timer, Balance, Radio."""
    ip = ctx["ip"]
    st = json.loads(http(f"https://{ip}/api/state")[1])
    kids_on = st["kids"]["on"]
    face = find_key(ctx.get("backup") or json.loads(http(f"https://{ip}/api/backup")[1]), "face")
    assert face is not None, "Zifferblatt nicht in der Sicherung"
    level0, ms0 = st["game"]["level"], st["game"]["ms"]
    try:
        w.send("kids off", 1.5)
        # Zifferblätter
        for f in range(5):
            w.send(f"face {f}", 1.2)
            s = w.shot(f"zifferblatt_{f}")
            assert sum(1 for v in s.px[::7] if v) > 150, f"Zifferblatt {f} leer"  # manche zeigen nur Text auf Schwarz
        w.send(f"face {face}", 1.2)
        # Wischen hin und zurück: links Kacheln/Musik (früher kam man von dort nicht zurück), oben Apps,
        # unten Kontrollzentrum – jeweils mit der Gegenrichtung wieder aufs Zifferblatt
        # (verglichen werden die Texte des Zifferblatts – Datum, Akku, Wetter; die Uhrzeit-Wörter dürfen sich ändern)
        w.send("home", 2)
        home = set(w.texts())
        w.shot("wischen_start")
        for there, back in (("l", "r"), ("u", "d"), ("d", "u")):
            w.send("wake", 0.6)  # das Display geht nach 30 s aus – dann weckt ein Wisch nur (so gewollt)
            w.send(f"swipe {there}", 1.3)
            moved = set(w.texts())
            w.shot(f"wischen_{there}")
            assert len(moved - home) >= 2, f"Wischen {there}: nichts passiert"
            w.send(f"swipe {back}", 1.5)
            again = set(w.texts())
            w.shot(f"wischen_{there}_zurueck")
            assert len(home - again) <= 3 and not (again & (moved - home)), \
                f"Zurückwischen ({back}) führt nicht zum Zifferblatt: {sorted(again - home)[:4]}"
            w.send("home", 0.8)
        # Kontrollzentrum (von oben) und Apps (von unten)
        w.send("wake", 0.6)
        w.send("swipe d", 1.2)
        assert any("%" in t for t in w.texts()), f"Kontrollzentrum ohne Akku-Anzeige: {w.texts()}"
        w.shot("kontrollzentrum")
        w.send("home", 0.8)
        w.send("wake", 0.6)
        w.send("swipe u", 1.2)
        w.shot("apps")
        assert w.has("Zähneputzen") or w.has("Balance"), f"App-Liste unvollständig: {w.texts()[:12]}"
        w.send("home", 0.8)
        # Timer: klingelt und lässt sich stoppen
        w.send("wake", 0.6)
        w.send("timer 3", 0.5)
        assert w.wait_for("Stopp", 8), "Timer klingelt nicht"
        w.shot("timer_klingelt")
        w.tap("Stopp")
        time.sleep(1)
        assert not w.has("Stopp"), "Klingeln lässt sich nicht stoppen"
        # Stoppuhr und Wecker-Seite
        for p in ("stopwatch", "alarm"):
            w.send("home", 0.8)
            w.send(f"page {p}", 1.2)
            assert w.texts(), f"{p}: leer"
        # Balance: Spiel startet, Kugel reagiert auf Neigung, Rekorde bleiben
        w.send("home", 0.8)
        w.send("page game", 1.2)
        w.tap("Ins Ziel")
        time.sleep(1)
        w.send("tilt 0.3 0.2", 2.5)
        w.shot("balance")
        w.send("tilt off", 0.2)
        w.send("home", 1)
        st2 = json.loads(http(f"https://{ip}/api/state")[1])
        assert (st2["game"]["level"], st2["game"]["ms"]) == (level0, ms0), "Balance-Rekorde verändert"
        # Radio: ein Sender aus der Suche, leise, dann wieder aus
        code, body, _ = http(f"https://{ip}/api/search?q=Deutschlandfunk&src=radio")
        items = [i for i in json.loads(body).get("items", []) if i["path"].startswith("radio:")]
        assert code == 200 and items, "Radiosuche ohne Ergebnis"
        it = items[0]
        assert http(f"https://{ip}/api/music/play", {"items": [{"kind": 0, "id": it["path"], "title": it["title"], "sub": it["sub"]}],
                                                       "index": 0})[0] == 200
        playing = False
        for _ in range(15):
            time.sleep(1)
            m = json.loads(http(f"https://{ip}/api/state")[1])["music"]
            if m["playing"]:
                playing = True
                break
        w.shot("radio")
        http(f"https://{ip}/api/music/ctl", {"cmd": "stop"})
        time.sleep(1.5)
        m = json.loads(http(f"https://{ip}/api/state")[1])["music"]
        assert playing, "Radiosender spielt nicht"
        assert not m["playing"], "Radio lässt sich nicht stoppen"
        # Funk-Seite öffnet (gesendet wird nicht – die andere Uhr könnte im Kinderzimmer liegen)
        w.send("home", 0.8)
        w.send("page radio", 1.2)
        assert w.texts(), "Funk-Seite leer"
        w.shot("funk")
    finally:
        w.send("tilt off", 0.2)
        http(f"https://{ip}/api/music/ctl", {"cmd": "stop"})
        w.send(f"face {face}", 0.5)
        if kids_on:
            w.send("kids on", 1.5)
        w.send("home", 0.8)
    st3 = json.loads(http(f"https://{ip}/api/state")[1])
    assert st3["kids"]["on"] == kids_on, "Kindermodus nicht wiederhergestellt"
    assert find_key(json.loads(http(f"https://{ip}/api/backup")[1]), "face") == face, "Zifferblatt nicht wiederhergestellt"


@test
def voller_durchlauf(w, ctx):
    if not FULL:
        return "übersprungen"
    st0 = json.loads(http(f"https://{ctx['ip']}/api/state")[1])["kids"]["brush"]
    open_game(w, "Sterne fangen", hero="Reh Fips")
    sections = ["Oben rechts", "Oben links", "Unten links", "Unten rechts", "Außen rechts", "Vorne", "Außen links", "Innen oben", "Innen unten"]
    seen, t0 = [], time.time()
    while time.time() - t0 < 170:
        t = w.texts()
        for s in sections:
            if s in t and (not seen or seen[-1] != s):
                seen.append(s)
        if any("Super geputzt" in x for x in t):
            break
        w.send(f"tilt {math.sin(time.time()) * 0.4:.2f} 0", 0.6)
    took = time.time() - t0
    w.shot("durchlauf_ende")
    assert any("Super geputzt" in x for x in w.texts()), f"kein Ende nach {took:.0f} s (Abschnitte: {seen})"
    assert seen == sections, f"Abschnitte nicht in KAI-Reihenfolge: {seen}"
    assert 120 <= took <= 160, f"Durchlauf dauerte {took:.0f} s (soll ≈ 2 min + Erklärung)"
    assert w.has("Neuer Sticker"), "kein Sticker am Ende"
    st1 = json.loads(http(f"https://{ctx['ip']}/api/state")[1])["kids"]["brush"]
    assert len(st1["names"]) >= len(st0["names"]), "Album ist geschrumpft"
    assert st1["streak"] >= 1, "Serie nicht gezählt"
    w.tap("Fertig")
    time.sleep(1)
    ctx["note_durchlauf"] = f"{took:.0f} s"


@test
def stabil(w, ctx):
    s = w.status()
    assert s["up"] >= ctx["start"]["up"], "Uhr hat während der Tests neu gestartet"
    assert s["heap"] > ctx["start"]["heap"] - 40000, f"interner Speicher {ctx['start']['heap']} → {s['heap']} Byte"
    # zu wenig interner RAM: Weboberfläche bricht beim Radiohören ab, die Uhr startet neu (main hat ≈ 85 KB frei)
    assert s["heap"] >= 60000, f"nur {s['heap']} Byte interner Speicher frei (mindestens 60 KB nötig)"
    assert "Musik aus" in s["line"] or "Musik Pause" in s["line"], f"Musik läuft: {s['line']}"


def main():
    os.makedirs(OUT, exist_ok=True)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0] if args else (glob.glob("/dev/cu.usbmodem*") or [None])[0]
    if not path:
        sys.exit("keine Uhr am USB")
    w = Watch(path)
    tmp = tempfile.mkdtemp()
    for name, kw in {"brush": dict(secs=7, noise=True, amp=0.5)}.items():
        SOUNDS[name] = os.path.join(tmp, name + ".wav")
        tone(SOUNDS[name], **kw)
    SOUNDS["wake"] = os.path.join(tmp, "wake.wav")
    tone(SOUNDS["wake"], secs=1.0, f=440, amp=0.001)
    if SOUND:
        play("wake")  # weckt die Audioausgabe des Rechners
    ctx = {"start": w.status()}
    assert ctx["start"], "Uhr antwortet nicht über USB"
    vol = None
    try:
        st = json.loads(http(f"https://{ctx['start']['ip']}/api/state")[1])
        vol = st["music"]["volume"]
    except Exception:
        pass
    assert w.ask("brush save", "ok"), "Sichern von Album/Serie/Rekorden fehlgeschlagen"
    w.send(f"vol {TEST_VOLUME}")
    passed = failed = 0
    try:
        for fn in results:
            if ONLY and fn.__name__ not in ONLY and fn.__name__ != "boot_und_status":
                continue
            t0 = time.time()
            try:
                note = fn(w, ctx)
                passed += 1
                print(f"ok      {fn.__name__} ({time.time() - t0:.0f} s){' – ' + note if note else ''}")
            except Exception as e:
                failed += 1
                print(f"FEHLER  {fn.__name__}: {e}")
                if not isinstance(e, AssertionError):
                    traceback.print_exc()
                try:
                    w.shot(f"fehler_{fn.__name__}")
                except Exception:
                    pass
            finally:
                w.send("tilt off", 0.1)
    finally:
        w.send("tilt off")
        w.send("home", 0.8)
        ok = w.ask("brush load", "ok")
        if vol is not None:  # über die Weboberfläche: die speichert auch (die Tests speichern zwischendurch Einstellungen)
            w.send(f"vol {vol}")
            http(f"https://{ctx['start']['ip']}/api/music/ctl", {"volume": vol})
        print(f"aufgeräumt: Album/Serie/Rekorde {'zurückgeholt' if ok else 'NICHT zurückgeholt'}, Lautstärke {vol}")
    print(f"{passed} bestanden, {failed} fehlgeschlagen · Bildschirmfotos: {OUT}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
