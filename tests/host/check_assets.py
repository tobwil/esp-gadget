#!/usr/bin/env python3
"""Prüfungen am Rechner ohne Uhr:
  - Sprachdatei (firmware/brushvoice.bin): Aufbau, jede Ansage, die die Firmware abspielt, ist für Luna und Fips da,
    jedes Stück lässt sich dekodieren, Längen passen zu den Putz-Abschnitten
  - Weboberfläche: JavaScript ist gültig, jede angesprochene Element-ID gibt es im HTML
  - Vorschau der Kinder-Zeichnungen lässt sich bauen
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("  FEHLER", msg)


def clips():
    data = open(os.path.join(ROOT, "firmware", "brushvoice.bin"), "rb").read()
    check(data[:3] == b"BV1", "brushvoice.bin: Kennung fehlt")
    n = data[3]
    out = {}
    for i in range(n):
        e = data[4 + i * 28: 4 + (i + 1) * 28]
        cid = e[:20].rstrip(b"\0").decode()
        off, ln = struct.unpack("<II", e[20:28])
        check(off + ln <= len(data), f"{cid}: zeigt hinter das Dateiende")
        check(cid not in out, f"{cid}: doppelt")
        out[cid] = data[off:off + ln]
    return data, out


def duration(mp3):
    with tempfile.NamedTemporaryFile(suffix=".mp3") as f:
        f.write(mp3)
        f.flush()
        r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", f.name],
                           capture_output=True, text=True)
        d = subprocess.run(["ffmpeg", "-v", "error", "-i", f.name, "-f", "null", "-"], capture_output=True, text=True)
        check(d.returncode == 0 and not d.stderr.strip(), f"MP3 fehlerhaft: {d.stderr.strip()[:80]}")
        return float(r.stdout.strip() or 0)


def test_voice():
    print("Sprachdatei")
    data, cl = clips()
    check(len(data) < 600 * 1024, f"brushvoice.bin zu groß ({len(data) // 1024} KB) – Geschichten gehören nicht auf die Uhr")
    src = open(os.path.join(ROOT, "firmware", "src", "ui", "ui_brush.cpp")).read()
    needed = sorted(set(re.findall(r'"(c/[a-z0-9]+)"', src)))
    check(len(needed) >= 18, f"nur {len(needed)} Ansagen in ui_brush.cpp gefunden")  # 9 KAI, 5 Schluss/Team, 4 Spiele
    for hero in ("luna", "fips"):
        for c in needed:
            check(f"{hero}/{c}" in cl, f"Ansage {hero}/{c} fehlt in brushvoice.bin")
    # Längen: Abschnitts-Ansagen müssen in ihren Abschnitt passen (Überlänge erlaubt: 3 s)
    secs = {c: int(n) for n, c in re.findall(r'\{kidart::M_\w+, \d, (\d+), "(c/[a-z]\d)"', src)}
    check(len(secs) == 9, f"{len(secs)} statt 9 Abschnitte erkannt")
    for cid, mp3 in sorted(cl.items()):
        d = duration(mp3)
        intro = cid.split("/")[-1] in ("monster", "pitch", "hum", "ball", "catch", "run")
        check(0.4 < d < (14 if intro else 8), f"{cid}: {d:.1f} s – zu lang (Erklärungen ≤ 14 s, sonst ≤ 8 s)")
        c = cid.split("/", 1)[1]
        if c in secs:
            check(d < secs[c] - 2, f"{cid}: {d:.1f} s – zu lang für den {secs[c]}-s-Abschnitt")
    print(f"  {len(cl)} Stücke, {len(data) // 1024} KB, {len(needed)} Ansagen je Figur")


def test_webui():
    print("Weboberfläche")
    s = open(os.path.join(ROOT, "firmware", "src", "webui.cpp")).read()
    m = re.search(r'R"rawliteral\((.*?)\)rawliteral"', s, re.S) or re.search(r'R"HTML\((.*?)\)HTML"', s, re.S)
    check(m is not None, "HTML der Weboberfläche nicht gefunden")
    if not m:
        return
    html = m.group(1)
    js = "\n".join(re.findall(r"<script>(.*?)</script>", html, re.S))
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(js)
    r = subprocess.run(["node", "--check", f.name], capture_output=True, text=True)
    os.unlink(f.name)
    check(r.returncode == 0, f"JavaScript ungültig: {r.stderr.strip()[:200]}")
    ids = set(re.findall(r'id="([^"]+)"', html)) | set(re.findall(r"id='([^']+)'", html))
    used = set(re.findall(r"\$\('([A-Za-z0-9_-]+)'\)", js))
    missing = sorted(u for u in used if u not in ids and not re.search(r"id=[\"']" + re.escape(u), js))
    check(not missing, f"IDs im Skript ohne Element: {', '.join(missing)}")
    print(f"  {len(used)} IDs im Skript, {len(ids)} im HTML")


def test_preview():
    print("Vorschau")
    with tempfile.TemporaryDirectory() as t:
        r = subprocess.run(["sh", os.path.join(ROOT, "tools", "kidart_preview.sh"), os.path.join(t, "kinderwelten.png")],
                           capture_output=True, text=True)
        check(r.returncode == 0, f"kidart_preview.sh: {r.stderr.strip()[:200]}")
        check(os.path.exists(os.path.join(t, "zahnputzen.png")), "zahnputzen.png wurde nicht erzeugt")


def test_stack():
    print("Stapel")
    # Hauptschleife und LVGL laufen mit 8 KB Stapel: große lokale Felder darin stürzen ab (so geschehen mit drawSticker)
    for rel in ("kidart.cpp", "hearing.cpp", "clips.cpp", "ui/ui_brush.cpp", "ui/ui_kidface.cpp", "ui/ui_game.cpp"):
        src = open(os.path.join(ROOT, "firmware", "src", rel)).read()
        sizes = {"S": 116, "N": 640, "hearing::N": 640}
        for m in re.finditer(r"^\s+(?!static)(?:const\s+)?(u?int(?:8|16|32)_t|float|char|int)\s+\w+\[([^\]]+)\]", src, re.M):
            expr = m.group(2)
            for k, v in sizes.items():
                expr = re.sub(rf"\b{re.escape(k)}\b", str(v), expr)
            try:
                n = eval(expr, {"__builtins__": {}})
            except Exception:
                continue
            width = {"uint8_t": 1, "int8_t": 1, "char": 1, "uint16_t": 2, "int16_t": 2}.get(m.group(1), 4)
            line = src[:m.start()].count("\n") + 1
            check(n * width <= 2048, f"{rel}:{line}: lokales Feld mit {n * width} Byte auf dem Stapel")


test_stack()
test_voice()
test_webui()
test_preview()
print("ok" if not fails else f"{len(fails)} Fehler")
sys.exit(1 if fails else 0)
