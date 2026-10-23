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
# Zähneputzen: Auswahl, Summ-Ballon, Album und Sticker; darunter die neun Abschnitte nach KAI
B = OUT.replace("kinderwelten", "zahnputzen") if "kinderwelten" in OUT else OUT.replace(".png", "_zahnputzen.png")
secs = ["Kaufl. oben re.", "Kaufl. oben li.", "Kaufl. unten li.", "Kaufl. unten re.", "Außen rechts", "Außen vorne", "Außen links", "Innen oben", "Innen unten"]
sz = 200
hh = int(sz * 96 / 116)
rows = [
    [("pick", "Wer putzt mit? (Kronen)"), ("b0", "Summ-Ballon: hoch summen"), ("b1", "Fips im Ballon"), ("b2", "Stille: sinkt"), ("st11", "Neuer Sticker")],
    [("a3", "Album: 6 von 26"), ("a0", "Album Seite 1"), ("a1", "Album Seite 2"), ("st24", "Pokal: 7 Tage"), ("st25", "Team-Herzen")],
    [(f"g{i}_0", secs[i]) for i in range(5)],
    [(f"g{i}_0", secs[i]) for i in range(5, 9)] + [("g0_1", "Monster grinsen")],
]
o = Image.new("RGB", (5 * (sz + pad) + pad, len(rows) * (hh + pad + 30) + pad), (13, 13, 15))
d = ImageDraw.Draw(o)
for r, row in enumerate(rows):
    for k, (n, t) in enumerate(row):
        x, y = pad + k * (sz + pad), pad + r * (hh + pad + 30)
        im = Image.open(f"{T}/{n}.ppm").convert("RGB").crop((0, 0, 116, 96)).resize((sz, hh), Image.NEAREST)
        o.paste(im, (x, y))
        tw = d.textlength(t, font=F)
        d.text((x + (sz - tw) / 2, y + hh + 4), t, fill=(200, 196, 188), font=F)
o.save(B, optimize=True)
print(B)
PY
rm -rf "$T"
