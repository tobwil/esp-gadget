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
PY
rm -rf "$T"
