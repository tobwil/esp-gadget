#!/usr/bin/env python3
"""Erzeugt die LVGL-Schriften in firmware/src/fonts/ (braucht Node.js für lv_font_conv).

    python3 tools/make_fonts.py

Text: Inter (OFL), Zifferblatt: Cormorant Garamond (OFL), Symbole: Material Icons Round (Apache 2.0).
Die Symbole werden in die Textschriften eingemischt, damit Labels Text und Icons kombinieren können.
"""
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "fonts")
OUT = os.path.join(ROOT, "firmware", "src", "fonts")

INTER_500 = os.path.join(SRC, "inter-latin-500-normal.woff")
INTER_600 = os.path.join(SRC, "inter-latin-600-normal.woff")
INTER_800 = os.path.join(SRC, "inter-latin-800-normal.woff")
INTER_300 = os.path.join(SRC, "inter-latin-300-normal.woff")
SERIF = os.path.join(SRC, "cormorant-garamond-latin-600-normal.woff")
ICONS = os.path.join(SRC, "MaterialIconsRound-Regular.otf")

# Latin-1 plus typografische Zeichen („“ – … •)
LATIN = "0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201A,0x201C,0x201D,0x201E,0x2022,0x2026,0x20AC"

ICON_NAMES = {
    "MUSIC": 0xE405, "TIMER": 0xE425, "ALARM": 0xE855, "SETTINGS": 0xE8B8, "WIFI": 0xE63E, "WIFI_OFF": 0xE648,
    "PLAY": 0xE037, "PAUSE": 0xE034, "NEXT": 0xE044, "PREV": 0xE045, "MIC": 0xE029, "VOL_UP": 0xE050,
    "VOL_DOWN": 0xE04D, "VOL_OFF": 0xE04F, "RADIO": 0xEBBA, "SEARCH": 0xE8B6, "LIBRARY": 0xE030,
    "QUEUE": 0xE03D, "HEART": 0xE87D, "SHUFFLE": 0xE043, "BRIGHT": 0xE518, "SLEEP": 0xEF44, "INFO": 0xE88E,
    "POWER": 0xE8AC, "RESTART": 0xF053, "CHECK": 0xE5CA, "CLOSE": 0xE5CD, "LEFT": 0xE5CB, "RIGHT": 0xE5CC,
    "PLUS": 0xE145, "MINUS": 0xE15B, "LOCK": 0xE897, "HOURGLASS": 0xEA5C, "WATCH": 0xE334, "BELL": 0xE7F4,
    "PERSON": 0xE7FD, "GROUP": 0xE7EF, "BACKSPACE": 0xE14A, "ENTER": 0xE31B, "SERVER": 0xE875,
    "STOP": 0xE047, "REPLAY": 0xE042, "BATTERY": 0xE1A4, "CHARGING": 0xE1A3, "WAVE": 0xE1B8,
    "STOPWATCH": 0xE01B, "HISTORY": 0xE889, "SHIFT": 0xE318, "SEND": 0xE163, "ALBUM": 0xE019,
    "REFRESH": 0xE5D5, "BACK": 0xE5C4, "ALARM_ON": 0xE858, "ALARM_OFF": 0xE857, "SNOOZE": 0xE046,
    "MORE": 0xEAE1, "TUNE": 0xE429, "FLAG": 0xE153, "RADIO_WAVES": 0xE1E2, "BOLT": 0xEA0B,
    "SUN": 0xE430, "CLOUD": 0xE42D, "RAIN": 0xF1AD, "SNOW": 0xEB3B, "STORM": 0xEBDB, "DROP": 0xE798,
    "NIGHT": 0xEA46, "WIND": 0xEFD8, "LOCATION": 0xE0C8, "TORCH": 0xF00B, "SCREEN_OFF": 0xE51C, "APPS": 0xE5C3,
    "BRIGHT_LOW": 0xE1AD, "BATTERY_LOW": 0xE19C, "HEARING": 0xE023, "HEARING_OFF": 0xF104, "FOG": 0xE3EA,
}
ICON_RANGE = ",".join(hex(c) for c in sorted(set(ICON_NAMES.values())))

FONTS = [
    # Name, Größe, [(Datei, Bereich), ...]
    ("font_text_20", 20, [(INTER_500, LATIN), (ICONS, ICON_RANGE)]),
    ("font_text_24", 24, [(INTER_600, LATIN), (ICONS, ICON_RANGE)]),
    ("font_text_32", 32, [(INTER_600, LATIN), (ICONS, ICON_RANGE)]),
    ("font_num_44", 44, [(INTER_600, "0x20,0x2D,0x2E,0x30-0x3A")]),
    ("font_num_80", 80, [(INTER_600, "0x20,0x2D,0x2E,0x30-0x3A")]),
    ("font_serif_44", 44, [(SERIF, "0x30-0x39,0x49,0x56,0x58")]),
    # Kiesel Text (Wortuhr): Stunde fett, Minuten leicht – nur Kleinbuchstaben
    ("font_word_bold_64", 64, [(INTER_800, "0x20,0x61-0x7A,0xDF,0xE4,0xF6,0xFC")]),
    ("font_word_light_54", 54, [(INTER_300, "0x20,0x61-0x7A,0xDF,0xE4,0xF6,0xFC")]),
    ("font_icon_40", 40, [(ICONS, ICON_RANGE)]),
    ("font_icon_64", 64, [(ICONS, ICON_RANGE)]),
]


def utf8(cp):
    return "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, size, parts in FONTS:
        cmd = ["npx", "--yes", "lv_font_conv@1.5.3", "--bpp", "4", "--size", str(size), "--format", "lvgl",
               "--no-compress", "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", os.path.join(OUT, name + ".c")]
        for path, rng in parts:
            cmd += ["--font", path, "-r", rng]
        print(name)
        subprocess.run(cmd, check=True)

    with open(os.path.join(OUT, "fonts.h"), "w") as f:
        f.write("// Erzeugt von tools/make_fonts.py – nicht von Hand ändern.\n#pragma once\n#include <lvgl.h>\n\n")
        for name, _, _ in FONTS:
            f.write(f"LV_FONT_DECLARE({name});\n")
        f.write("\n// Material Icons Round als UTF-8 (in allen Text- und Icon-Schriften enthalten)\n")
        for k, cp in ICON_NAMES.items():
            f.write(f'#define I_{k} "{utf8(cp)}"\n')


if __name__ == "__main__":
    main()
