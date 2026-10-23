#!/bin/sh
# Vorschau der Kinder-Welten: kompiliert die Zeichnung für den Rechner und setzt eine Übersicht zusammen.
#   sh tools/kidart_preview.sh [Ausgabe.png]
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
c++ -std=c++17 -O2 -Ifirmware/src firmware/src/kidart.cpp tools/kidart_preview.cpp -o "$T/kp"
"$T/kp" "$T"
python3 - "$T" "${1:-docs/screens/kinderwelten.png}" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont
T, OUT = sys.argv[1], sys.argv[2]
names = ["wake", "breakfast", "school", "lunch", "play", "bath", "story", "sleep", "adventure"]
caps = ["Aufstehen", "Frühstück", "Kita", "Mittag", "Spielen", "Baden", "Geschichte", "Schlafen", "Abenteuer"]
worlds = ["Zuhause", "Weltraum", "Bauernhof", "Unterwasser", "Märchenland"]
size, pad, lw = 220, 18, 150
F = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 22)
o = Image.new("RGB", (lw + len(names) * (size + pad) + pad, len(worlds) * (size + pad) + pad + 34), (13, 13, 15))
d = ImageDraw.Draw(o)
for i, c in enumerate(caps):
    tw = d.textlength(c, font=F)
    d.text((lw + pad + i * (size + pad) + (size - tw) / 2, 8), c, fill=(200, 196, 188), font=F)
for w, wn in enumerate(worlds):
    y = 34 + pad + w * (size + pad)
    d.text((14, y + size / 2 - 12), wn, fill=(232, 182, 96), font=F)
    for i, n in enumerate(names):
        im = Image.open(f"{T}/w{w}_{n}.ppm").convert("RGB").crop((0, 0, 116, 96)).resize((size, int(size * 96 / 116)), Image.NEAREST)
        o.paste(im, (lw + pad + i * (size + pad), y))
o.save(OUT, optimize=True)
print(OUT)
# Zahnputz-Geschichten: Luna und Fips je sieben Kapitel à vier Bilder, dazu die Rätsel
B = OUT.replace("kinderwelten", "zahnputzen") if "kinderwelten" in OUT else OUT.replace(".png", "_zahnputzen.png")
days = ["Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"]
rows = [(f"{'Luna' if h == 0 else 'Fips'} · {days[d][:2]}", [f"s{h}_{d}_{b}" for b in range(4)]) for h in range(2) for d in range(7)]
rows.append(("Rätsel", ["q0", "q1", "q2", "q3"]))
rows.append(("", ["q4", "q5"]))
sz = 180
o = Image.new("RGB", (lw + 4 * (sz + pad) + pad, len(rows) * (int(sz * 96 / 116) + pad) + pad + 34), (13, 13, 15))
d = ImageDraw.Draw(o)
for i, c in enumerate(["oben links", "oben rechts", "unten links", "unten rechts"]):
    tw = d.textlength(c, font=F)
    d.text((lw + pad + i * (sz + pad) + (sz - tw) / 2, 8), c, fill=(200, 196, 188), font=F)
for r, (t, names) in enumerate(rows):
    y = 34 + pad + r * (int(sz * 96 / 116) + pad)
    d.text((14, y + 30), t, fill=(232, 182, 96), font=F)
    for i, n in enumerate(names):
        im = Image.open(f"{T}/{n}.ppm").convert("RGB").crop((0, 0, 116, 96)).resize((sz, int(sz * 96 / 116)), Image.NEAREST)
        o.paste(im, (lw + pad + i * (sz + pad), y))
o.save(B, optimize=True)
print(B)
PY
rm -rf "$T"
