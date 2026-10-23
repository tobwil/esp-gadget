# Galerien fürs README: Bildschirmfotos der Uhr rund maskiert auf dunklem Grund, mit Beschriftung
#   python3 tools/gallery.py <Ordner mit Bildschirmfotos> docs/screens
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
SH = sys.argv[1]; OUT = sys.argv[2]
F = "/System/Library/Fonts/Helvetica.ttc"
cap = ImageFont.truetype(F, 22)
BG = (13, 13, 15)

def watch(name, size=300):
    im = Image.open(f"{SH}/{name}.png").convert("RGB").resize((size, size), Image.LANCZOS)
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).ellipse((1, 1, size - 2, size - 2), fill=255)
    t = Image.new("RGB", (size + 16, size + 16), BG)
    ImageDraw.Draw(t).ellipse((0, 0, size + 15, size + 15), fill=(40, 40, 46))  # Rand wie ein Gehäuse
    t.paste(im, (8, 8), m)
    return t

def grid(fname, items, cols, size=300):
    pad, ch = 28, 40
    rows = (len(items) + cols - 1) // cols
    W = cols * (size + 16) + (cols + 1) * pad
    H = rows * (size + 16 + ch) + (rows + 1) * pad - pad // 2
    o = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(o)
    for i, (name, text) in enumerate(items):
        x = pad + (i % cols) * (size + 16 + pad)
        y = pad + (i // cols) * (size + 16 + ch + pad)
        o.paste(watch(name, size), (x, y))
        tw = d.textlength(text, font=cap)
        d.text((x + (size + 16 - tw) / 2, y + size + 22), text, fill=(200, 196, 188), font=cap)
    o.save(f"{OUT}/{fname}", optimize=True)

grid("zifferblaetter.png", [("a01_face_pocket", "Taschenuhr"), ("a02_face_sun", "Daylight"), ("a03_face_stars", "Sternenhimmel"), ("a04_face_text", "Kiesel Text"), ("a05_face_glitch", "Glitch")], 5)
grid("bedienung.png", [("a06_apps", "Apps (hochwischen)"), ("a07_control", "Kontrollzentrum (runter)"), ("a08_tile_weather", "Kacheln (nach links)"), ("a10_music_home", "Musik")], 4)
grid("musik.png", [("a11_radio_home", "Radio"), ("a12_radio_kids", "Kinderradio"), ("a19_radio_playing", "Live mit Songtitel"), ("a20_sleep", "Sleep-Timer"),
                   ("a13_ard_home", "ARD Audiothek"), ("a14_ard_soccer", "Fußball live"), ("a15_ard_kids", "Rubrik Für Kinder"), ("a17_ard_playing", "Folge mit Cover")], 4)
grid("kira.png", [("a30_kira_logo", "Ruhe: KI/"), ("a31_kira_listen", "Hört zu"), ("a32_kira_think", "Denkt nach"), ("a33_kira_speak", "Antwortet")], 4)
grid("werkzeuge.png", [("a21_alarm", "Radiowecker"), ("a22_alarm_src", "Wecken mit …"), ("a23_timer", "Timer"), ("a24_stopwatch", "Stoppuhr"),
                       ("a25_funk", "Funk"), ("a18_search", "Suche per Sprache"), ("a28_keyboard", "Tastatur"), ("a27_web_qr", "QR zur Weboberfläche")], 4)
grid("balance.png", [("a40_game_menu", "Balance"), ("a41_game_wiese", "Wiese"), ("a42_game_strand", "Strand"), ("a43_game_eis", "Eis"), ("a44_game_space", "Weltraum"),
                     ("a45_game_holz", "Holz"), ("a46_game_lava", "Lava"), ("a47_game_sea", "Unterwasser"), ("a48_game_candy", "Zuckerwatte"), ("a26_settings", "Einstellungen")], 5)
grid("kindertag.png", [("k01_wake", "6:30 Aufstehen"), ("k02_breakfast", "Frühstück"), ("k03_kita", "Kita"), ("k04_lunch", "Mittagessen"), ("k05_nap", "Mittagsruhe"),
                       ("k06_play", "Draußen spielen"), ("k07_dinner", "Abendessen"), ("k08_bath", "Baden"), ("k09_story", "Geschichte"), ("k10_sleep", "Schlafenszeit")], 5)
grid("kindermodus.png", [("k11_apps", "Nur erlaubte Apps"), ("k12_music", "Nur Freigegebenes"), ("k13_control", "Ohne Einstellungen")], 3)
grid("wutglas.png", [("k20_angry_idle", "Wütend? Schütteln!"), ("k21_angry_shake", "Fester!"), ("k22_angry_half", "Funken fliegen raus"), ("k24_angry_calm", "Fast alles raus"),
                     ("k23_angry_out", "Ruhe-Glitzer"), ("k25_angry_breathe", "Mitatmen"), ("k26_angry_done", "Ausatmen …")], 4)

# Hero: vier große Uhren
items = [("a02_face_sun", ""), ("a30_kira_logo", ""), ("k02_breakfast", ""), ("a19_radio_playing", "")]
size, pad = 380, 36
o = Image.new("RGB", (4 * (size + 16) + 5 * pad, size + 16 + 2 * pad), BG)
for i, (n, _) in enumerate(items): o.paste(watch(n, size), (pad + i * (size + 16 + pad), pad))
o.save(f"{OUT}/hero.png", optimize=True)

# Weboberfläche: vier Reiter nebeneinander (Bildschirmfotos 390 × 844 im Dunkelmodus, z. B. aus der Handy-Ansicht)
ws = [("web_start", "Übersicht"), ("web_kinder", "Kinder · Tagesablauf"), ("web_musik", "Musik · Fernbedienung"), ("web_geraet", "Gerät")]
w, h, pad, ch = 390, 844, 30, 44
o = Image.new("RGB", (4 * w + 5 * pad, h + 2 * pad + ch), BG)
d = ImageDraw.Draw(o)
for k, (n, c) in enumerate(ws):
    i = Image.open(f"{SH}/{n}.png").convert("RGB").resize((w, h), Image.LANCZOS)
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), 36, fill=255)
    x = pad + k * (w + pad)
    o.paste(i, (x, pad), m)
    d.rounded_rectangle((x, pad, x + w - 1, pad + h - 1), 36, outline=(48, 48, 54), width=2)
    tw = d.textlength(c, font=cap)
    d.text((x + (w - tw) / 2, pad + h + 12), c, fill=(200, 196, 188), font=cap)
o.save(f"{OUT}/weboberflaeche.png", optimize=True)
print("ok")
