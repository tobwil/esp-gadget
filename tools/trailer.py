# Trailer aus Bildschirmfotos: Hochformat 1080×1920, Sprecherin über Gemini-TTS, Übergänge, Bildfolgen.
#   K=<Gemini-Schlüssel> SHOTS=<Ordner mit Bildschirmfotos> python3 tools/trailer.py erwachsene|kinder docs/videos/….mp4
# Die Bildschirmfotos (Namen siehe Drehbücher unten) nimmt tools/remote.py auf; braucht Pillow und ffmpeg.
import array, base64, hashlib, json, math, os, subprocess, sys, urllib.request, wave
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SH = os.environ.get("SHOTS", "shots")
CACHE = os.environ.get("TTS_CACHE", os.path.join(SH, "..", "tts"))
os.makedirs(CACHE, exist_ok=True)
W, H, FPS, SR = 1080, 1920, 30, 24000
FB = "/System/Library/Fonts/Avenir Next.ttc"
fTitle = ImageFont.truetype(FB, 76, index=2)   # Demi Bold
fSub = ImageFont.truetype(FB, 40, index=0)
fBrand = ImageFont.truetype(FB, 34, index=2)
fBig = ImageFont.truetype(FB, 120, index=2)

# ------------------------------------------------------------------ Sprecherin (Gemini-TTS, zwischengespeichert)

def tts(text, voice, style):
    key = hashlib.sha1((voice + style + text).encode()).hexdigest()[:16]
    path = os.path.join(CACHE, key + ".pcm")
    if not os.path.exists(path):
        body = {"contents": [{"parts": [{"text": text}]}],
                "generationConfig": {"responseModalities": ["AUDIO"],
                                     "speechConfig": {"voiceConfig": {"prebuiltVoiceConfig": {"voiceName": voice}}}}}
        req = urllib.request.Request(
            "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash-tts:generateContent",
            data=json.dumps(body).encode(), headers={"Content-Type": "application/json", "x-goog-api-key": os.environ["K"]})
        d = json.load(urllib.request.urlopen(req, timeout=120))
        pcm = base64.b64decode(d["candidates"][0]["content"]["parts"][0]["inlineData"]["data"])
        open(path, "wb").write(pcm)
    return open(path, "rb").read()

# ------------------------------------------------------------------ Bildbausteine

def watch(name, size):
    im = Image.open(os.path.join(SH, name + ".png")).convert("RGB").resize((size, size), Image.LANCZOS)
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).ellipse((2, 2, size - 3, size - 3), fill=255)
    pad = size // 22
    t = Image.new("RGBA", (size + 2 * pad, size + 2 * pad), (0, 0, 0, 0))
    d = ImageDraw.Draw(t)
    d.ellipse((0, 0, size + 2 * pad - 1, size + 2 * pad - 1), fill=(44, 44, 50, 255))   # Gehäuse
    d.ellipse((pad // 3, pad // 3, size + 2 * pad - pad // 3, size + 2 * pad - pad // 3), fill=(22, 22, 26, 255))
    t.paste(im, (pad, pad), m)
    return t

def phone(name, width):
    im = Image.open(os.path.join(SH, name + ".png")).convert("RGB")
    im = im.crop((0, 0, im.width, int(im.width * 1.75))).resize((width, int(width * 1.75)), Image.LANCZOS)
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, im.width - 1, im.height - 1), 44, fill=255)
    t = Image.new("RGBA", (im.width + 24, im.height + 24), (0, 0, 0, 0))
    ImageDraw.Draw(t).rounded_rectangle((0, 0, t.width - 1, t.height - 1), 56, fill=(44, 44, 50, 255))
    t.paste(im, (12, 12), m)
    return t

def background(accent):
    bg = Image.new("RGB", (W, H), (11, 11, 14))
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    ImageDraw.Draw(glow).ellipse((W // 2 - 620, 640, W // 2 + 620, 1880), fill=accent)
    glow = glow.filter(ImageFilter.GaussianBlur(220))
    return Image.blend(bg, glow, 0.38)

def centered(d, y, text, font, fill):
    tw = d.textlength(text, font=font)
    d.text(((W - tw) / 2, y), text, font=font, fill=fill)

def wrap(d, text, font, maxw):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=font) > maxw and cur: lines.append(cur); cur = w
        else: cur = t
    return lines + ([cur] if cur else [])

# ------------------------------------------------------------------ Szenen

class Scene:
    def __init__(self, title, sub, imgs, say, accent=(70, 70, 90), kind="watch", hold=0.8):
        self.title, self.sub, self.imgs, self.say, self.accent, self.kind, self.hold = title, sub, imgs, say, accent, kind, hold

def render(scenes, out, voice, style, brand):
    # Sprache holen und Dauer je Szene festlegen
    audio = array.array("h")
    plan = []
    t = 0.0
    for s in scenes:
        pcm = tts(s.say, voice, style) if s.say else b""
        dur = max(2.6, len(pcm) / 2 / SR + s.hold + 0.5)
        plan.append((s, t, dur))
        start = int((t + 0.35) * SR)
        a = array.array("h"); a.frombytes(pcm)
        if len(audio) < start: audio.extend([0] * (start - len(audio)))
        audio.extend(a)
        t += dur
    total = t
    audio.extend([0] * max(0, int(total * SR) - len(audio)))
    wav = out + ".wav"
    with wave.open(wav, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(audio.tobytes())

    # Standbilder je Szene vorbereiten (teuer: einmal)
    pre = []
    for s, t0, dur in plan:
        bg = background(s.accent)
        d = ImageDraw.Draw(bg)
        y = 170
        for line in wrap(d, s.title, fTitle, 940):
            centered(d, y, line, fTitle, (245, 242, 236)); y += 92
        y += 8
        for line in wrap(d, s.sub, fSub, 900):
            centered(d, y, line, fSub, (190, 186, 178)); y += 54
        centered(d, H - 120, brand, fBrand, (150, 146, 140))
        if s.kind == "title":
            pics = []
        elif s.kind == "phone":
            pics = [phone(n, 600) for n in s.imgs]
        else:
            pics = [watch(n, 760) for n in s.imgs]
        pre.append((bg, pics, max(y, 420)))

    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-i", wav, "-c:v", "libx264", "-preset", "medium", "-crf", "20",
                           "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-shortest",
                           "-movflags", "+faststart", out], stdin=subprocess.PIPE)
    frames = int(total * FPS)
    fade = 0.45

    def frame_of(k, t):
        s, t0, dur = plan[k]
        bg, pics, top = pre[k]
        f = bg.copy()
        lt = t - t0
        if s.kind == "title":
            d = ImageDraw.Draw(f)
            a = min(1, lt / 0.8)
            col = tuple(int(c * a) for c in (245, 242, 236))
            centered(d, 820, s.imgs[0], fBig, col)
            if len(s.imgs) > 1: centered(d, 980, s.imgs[1], fSub, tuple(int(c * a) for c in (200, 196, 188)))
            return f
        n = len(pics)
        per = dur / n
        i = min(n - 1, int(lt / per))
        p = pics[i]
        # weich zum nächsten Bild der Folge überblenden
        within = lt - i * per
        if i > 0 and within < 0.3:
            p = Image.blend(pics[i - 1], p, within / 0.3)
        z = 1.0 + 0.045 * (lt / dur)            # langsames Heranfahren
        pw, ph = int(p.width * z), int(p.height * z)
        q = p.resize((pw, ph), Image.BILINEAR)
        cy = top + (H - 160 - top) // 2
        f.paste(q, ((W - pw) // 2, cy - ph // 2), q)
        return f

    for fi in range(frames):
        t = fi / FPS
        k = max(i for i, (_, t0, _) in enumerate(plan) if t0 <= t + 1e-6)
        img = frame_of(k, t)
        s, t0, dur = plan[k]
        if k + 1 < len(plan) and t > t0 + dur - fade:   # Übergang zur nächsten Szene
            img = Image.blend(img, frame_of(k + 1, t0 + dur + 0.001), (t - (t0 + dur - fade)) / fade)
        if t < 0.5: img = Image.blend(Image.new("RGB", (W, H)), img, t / 0.5)
        if t > total - 0.7: img = Image.blend(img, Image.new("RGB", (W, H)), (t - (total - 0.7)) / 0.7)
        ff.stdin.write(img.tobytes())
    ff.stdin.close()
    ff.wait()
    os.remove(wav)
    print(out, f"{total:.1f} s")

# ------------------------------------------------------------------ Drehbücher

STYLE = "Sag auf Deutsch, warm, natürlich und mit leiser Begeisterung"

ADULT = [
    Scene("", "", ["Taschenfunk", "Eine Uhr, die alles selbst kann."], "Das ist Taschenfunk. Eine Uhr, die alles selbst kann – ohne Handy in der Tasche.", kind="title"),
    Scene("Fünf Zifferblätter", "Taschenuhr · Daylight · Sternenhimmel · Kiesel · Glitch",
          ["a01_face_pocket", "a02_face_sun", "a03_face_stars", "a04_face_text", "a05_face_glitch"],
          "Fünf Zifferblätter – von der klassischen Taschenuhr bis zum echten Sternenhimmel über deinem Standort.", (180, 140, 70)),
    Scene("Bedient wie eine Smartwatch", "Hoch: Apps · Runter: Kontrollzentrum · Seitlich: Kacheln",
          ["a06_apps", "a07_control", "a08_tile_weather"],
          "Bedient wird sie wie eine Smartwatch: hoch für die Apps, runter fürs Kontrollzentrum, zur Seite für die Kacheln.", (70, 110, 180)),
    Scene("Radio aus aller Welt", "Live, mit Songtitel – suchen per Stimme",
          ["a11_radio_home", "a18_search", "a19_radio_playing"],
          "Tausende Radiosender, live und mit Songtitel. Gesucht wird einfach per Stimme.", (200, 70, 60)),
    Scene("ARD Audiothek & Fußball live", "Hörspiele, Wissen, alle ARD-Sender – und die Sportschau",
          ["a13_ard_home", "a14_ard_soccer", "a17_ard_playing"],
          "Die ARD Audiothek ist eingebaut – mit allen ARD-Sendern und der Sportschau. Fußball live, direkt am Handgelenk.", (200, 90, 60)),
    Scene("KIra", "Die KI-Assistentin von KI/sruptiv",
          ["a30_kira_logo", "a31_kira_listen", "a32_kira_think", "a33_kira_speak"],
          "Und dann ist da Kira – die KI-Assistentin von KI-sruptiv. Halten, fragen, loslassen. Sie sucht im Netz und stellt Timer und Wecker.", (60, 170, 200)),
    Scene("Deine n8n-Workflows", "Auf Zuruf – aber nur, was du freigibst",
          ["web_ki"], "Und sie startet auf Zuruf deine n8n-Workflows und Agenten. Sicher: nur, was du freigibst.", (60, 140, 200), kind="phone"),
    Scene("Radiowecker & Sleep-Timer", "Wecken mit Sender oder Nachrichten · abends sanft ausblenden",
          ["a21_alarm", "a22_alarm_src", "a20_sleep"],
          "Der Radiowecker weckt dich mit deinem Sender oder den neuesten Nachrichten. Und abends blendet der Sleep-Timer sanft aus.", (140, 100, 200)),
    Scene("Funk & Balance", "Walkie-Talkie von Uhr zu Uhr · ein Spiel mit acht Welten",
          ["a25_funk", "a41_game_wiese", "a44_game_space", "a46_game_lava", "a48_game_candy"],
          "Dazu ein Walkie-Talkie von Uhr zu Uhr – und ein Balance-Spiel mit acht Welten.", (60, 170, 110)),
    Scene("Eingerichtet vom Handy", "Weboberfläche mit QR-Code",
          ["web_start"], "Eingerichtet wird alles bequem vom Handy aus.", (180, 140, 70), kind="phone"),
    Scene("", "", ["Taschenfunk", "gebaut von KI/sruptiv"], "Taschenfunk. Gebaut von KI-sruptiv.", kind="title", hold=1.4),
]

KIDS = [
    Scene("", "", ["Taschenfunk", "für Kinder"], "Taschenfunk für Kinder: eine Uhr, die Spaß macht – und Eltern die Kontrolle lässt.", kind="title"),
    Scene("Der Tag als Pixel-Geschichte", "Aufstehen · Frühstück · Kita · Spielen · Baden · Schlafen",
          ["k01_wake", "k02_breakfast", "k03_kita", "k04_lunch", "k06_play", "k07_dinner", "k08_bath", "k09_story", "k10_sleep"],
          "Das Zifferblatt erzählt den Tag als kleine Pixel-Geschichte: aufstehen, frühstücken, Kita, spielen, baden – bis zur Gute-Nacht-Geschichte.", (230, 160, 70)),
    Scene("Wie lange noch?", "Der Ring zeigt den ganzen Tag",
          ["k06_play", "k09_story"],
          "Der bunte Ring zeigt, wie viel vom Tag schon geschafft ist. So wird die Frage nach der Schlafenszeit ganz leicht.", (120, 90, 210)),
    Scene("Nur, was ihr freigebt", "Radio Teddy · Ohrenbär · Märchen der ARD",
          ["k12_music", "a12_radio_kids", "a15_ard_kids"],
          "Musik und Hörspiele? Nur, was ihr freigebt: Radio Teddy, der Ohrenbär, die Märchen der ARD.", (230, 90, 140)),
    Scene("Sicher eingestellt", "Keine Einstellungen · keine freie Suche · leise genug",
          ["k11_apps", "k13_control"],
          "Keine Einstellungen, keine freie Suche, keine KI. Und die Lautstärke hat eine Obergrenze.", (90, 150, 210)),
    Scene("Spielen mit Gefühl", "Kugeln balancieren durch acht Welten",
          ["a41_game_wiese", "a43_game_eis", "a47_game_sea", "a48_game_candy"],
          "Spielen geht natürlich auch: Kugeln balancieren – über die Wiese, durchs Eis und durch Zuckerwatte.", (90, 190, 120)),
    Scene("Der Wutmodus", "Wenn die Gefühle zu groß werden",
          ["k20_angry_idle", "k21_angry_shake", "k22_angry_half", "k24_angry_calm"],
          "Und wenn die Gefühle mal zu groß werden, gibt es den Wutmodus. Die Uhr ganz fest schütteln – bis alle roten Funken draußen sind.", (210, 60, 50)),
    Scene("Und dann: Ruhe", "Glitzer sinkt · ein Kreis atmet mit",
          ["k23_angry_out", "k25_angry_breathe", "k26_angry_done"],
          "Dann wird es ruhig. Glitzer sinkt, und ein Kreis atmet mit. Ein … und aus.", (50, 120, 200), hold=1.2),
    Scene("Eltern behalten den Überblick", "Tagesablauf · Freigaben · Apps · Eltern-PIN",
          ["web_kinder"], "Alles steuert ihr vom Handy: Tagesablauf, Freigaben und erlaubte Apps – auf Wunsch mit Eltern-PIN.", (230, 120, 60), kind="phone"),
    Scene("", "", ["Taschenfunk", "für Kinder gemacht"], "Taschenfunk. Für Kinder gemacht.", kind="title", hold=1.4),
]

if __name__ == "__main__":
    which, out = sys.argv[1], sys.argv[2]
    if which == "erwachsene": render(ADULT, out, "Kore", STYLE, "TASCHENFUNK  ·  KI/sruptiv")
    else: render(KIDS, out, "Leda", "Sag auf Deutsch, sanft, freundlich und warm, wie für Familien", "TASCHENFUNK  ·  Kindermodus")
