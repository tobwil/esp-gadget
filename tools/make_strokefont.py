#!/usr/bin/env python3
"""Übernimmt die Monoline-Strichschrift aus dem FaceLab-Projekt (Garmin-Watchfaces) als C-Header.

    python3 tools/make_strokefont.py ~/Projekte/garminwatchfaces/garminwatchface/web/js/engine/fontdata.js

Ergebnis: firmware/src/faces/strokefont.h
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "firmware", "src", "faces", "strokefont.h")


def array(src, name):
    m = re.search(name + r":\s*(?:new\s+\w+\()?\[([^\]]*)\]", src)
    if not m:
        sys.exit(f"{name} nicht gefunden")
    return [int(v) for v in m.group(1).replace("\n", "").split(",") if v.strip()]


def main():
    src = open(sys.argv[1], encoding="utf-8").read()
    parts = {k: array(src, k) for k in ("strokeMap", "strokeData", "strokeOffs", "strokeW")}
    types = {"strokeMap": "uint8_t", "strokeData": "uint8_t", "strokeOffs": "uint16_t", "strokeW": "uint8_t"}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("// Erzeugt von tools/make_strokefont.py aus FaceLab (fontdata.js) – nicht von Hand ändern.\n")
        f.write("// Monoline-Strichschrift: Koordinaten in Viertel-Einheiten, Versalhöhe 80.\n#pragma once\n#include <stdint.h>\n\n")
        for k, v in parts.items():
            if k == "strokeW" and max(v) > 255:
                types[k] = "uint16_t"
            f.write(f"static const {types[k]} SF_{k[6:].upper()}[{len(v)}] = {{")
            for i, x in enumerate(v):
                f.write(("\n  " if i % 24 == 0 else "") + f"{x},")
            f.write("\n};\n\n")
    print(OUT, {k: len(v) for k, v in parts.items()})


if __name__ == "__main__":
    main()
