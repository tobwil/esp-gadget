# Stimmen für das Zähneputzen: Pferd Luna und Reh Fips sprechen Ansagen (KAI) und ihre Hörgeschichten.
#   K=<Gemini-Schlüssel> python3 tools/brush_voice.py
# Erzeugt mit Gemini-TTS (zwischengespeichert in tools/.tts-cache), kodiert als MP3 (16 kHz, mono, 16 kbit/s) und packt
# alles in firmware/brushvoice.bin, die in die Firmware eingebettet wird. Aufbau der Datei:
#   "BV1" + uint8 Anzahl + je Eintrag (char id[20], uint32 Versatz, uint32 Länge) + MP3-Daten (Little Endian)
import base64, hashlib, json, os, struct, subprocess, sys, tempfile, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", ".tts-cache")
OUT = os.path.join(ROOT, "firmware", "brushvoice.bin")
VOICE = {"luna": "Aoede", "fips": "Puck"}
# Das Kontingent gilt je Modell (100 Anfragen am Tag) – daher spricht jede Figur über ihr eigenes Modell
MODEL = {"Aoede": "gemini-3.8-flash-tts", "Puck": "gemini-3.8-flash-lite-tts"}

# Ansagen im KAI-Takt (gleicher Text, Stimme der Figur): Kauflächen, außen, innen, Schluss
CUES = {
    "k1": "Backenzähne oben rechts. Hin und her schrubben!",
    "k2": "Oben links!",
    "k3": "Unten links!",
    "k4": "Unten rechts!",
    "a1": "Jetzt Zähne zusammenbeißen. Außen rechts, kleine Kreise!",
    "a2": "Vorne, kleine Kreise!",
    "a3": "Außen links!",
    "i1": "Mund weit auf! Innen oben, von Rot nach Weiß.",
    "i2": "Innen unten, von Rot nach Weiß.",
    "spit": "Fertig! Jetzt ausspucken, aber nicht ausspülen.",
    "parent": "Und jetzt darf Mama oder Papa noch einmal nachputzen.",
    "hum": "Summ mal! Dann fliegt der Ballon nach oben.",
    "team": "Deine Schwester putzt auch gerade. Zusammen schafft ihr das!",
    "sticker": "Du hast einen neuen Sticker bekommen!",
    "streak": "Sieben Tage hintereinander geputzt! Du bist ein Zahnputz-Profi!",
}

# Hörgeschichten: je Kapitel ein Titel und neun kurze Stücke – eines nach jeder Ansage
STORY = {
    "luna": [
        ("Die Schatzkarte", ["Im Stall raschelt es im Stroh.", "Unter dem Heu liegt ein altes Papier.", "Ich puste den Staub weg. Pfff!",
                             "Es ist eine Karte! Mit einem großen roten X.", "Das X liegt hinter dem Hügel, ganz weit weg.",
                             "Ein Schatz? Für mich? Mein Schweif wedelt vor Aufregung.", "Ich trabe los, über die Wiese. Klipp klapp, klipp klapp.",
                             "Aber oh nein: Dornenbüsche versperren den Weg! Hier komme ich nicht durch.",
                             "Morgen suche ich einen anderen Weg. Putzt du dann wieder mit mir?"]),
        ("Am Bach", ["Heute nehme ich den Weg am Bach entlang.", "Das Wasser plätschert und glitzert.", "Aber der Bach ist breit. Wie komme ich rüber?",
                     "Da! Steine im Wasser, wie eine Treppe.", "Ich setze vorsichtig einen Huf auf den ersten Stein.",
                     "Er ist glitschig! Ich wackle. Huiii!", "Noch ein Stein, und noch einer.",
                     "Geschafft! Am anderen Ufer schüttle ich mich trocken. Pitsch, patsch!",
                     "Vor mir beginnt ein großer, dunkler Wald. Was wohl darin wartet?"]),
        ("Die Eule", ["Im Wald ist es dämmrig und still.", "Nur die Blätter rascheln.", "Plötzlich: Huhu! Wer ruft da?",
                      "Auf einem Ast sitzt eine Eule mit großen gelben Augen.", "Wohin so spät, kleines Pferd?, fragt sie.",
                      "Ich zeige ihr meine Schatzkarte.", "Die Eule dreht ihren Kopf ganz weit herum. Das kann ich nicht!",
                      "Folge den roten Pilzen, sagt sie. Die führen dich zum Hügel.", "Danke, Eule! Morgen folge ich den Pilzen."]),
        ("Die roten Pilze", ["Ich suche den ersten roten Pilz.", "Da ist er! Rot mit weißen Punkten.", "Und da der nächste, und da noch einer!",
                             "Pilz für Pilz gehe ich tiefer in den Wald.", "Oh, jetzt teilt sich der Weg. Drei Wege!", "Links, rechts oder geradeaus?",
                             "Ich schaue genau hin: Nur auf dem mittleren Weg stehen rote Pilze.",
                             "Also geradeaus! Und da wird es hell. Eine Lichtung!", "Hinter der Lichtung sehe ich den Hügel. Bald bin ich da!"]),
        ("Das Gewitter", ["Heute ziehen dunkle Wolken auf.", "Es wird windig, meine Mähne flattert.", "Und dann: Blitz!", "Und Donner! Rumms!",
                          "Schnell unter die große Tanne.", "Der Regen trommelt auf die Zweige. Tropf, tropf, tropf.", "Ich bleibe ganz ruhig und warte.",
                          "Endlich hört der Regen auf. Die Sonne kommt raus, und ich sehe einen Regenbogen!",
                          "Und genau unter dem Regenbogen: der Hügel mit dem X!"]),
        ("Das große X", ["Ich bin am Hügel angekommen!", "Wo genau ist das X?", "Da: ein großer grauer Stein.", "Ich schiebe mit meiner Nase. Uff!",
                         "Er bewegt sich nicht. Noch einmal, mit aller Kraft!", "Uuuund er rollt zur Seite!", "Darunter glitzert etwas Goldenes.",
                         "Eine Kiste! Ich öffne den Deckel. Sie ist voller goldener Hufeisen!", "So viele! Die bringe ich morgen nach Hause."]),
        ("Das Fest", ["Mit der Kiste trabe ich zurück zum Hof.", "Die Hühner gackern: Was hat Luna da?", "Alle Tiere kommen angelaufen.",
                      "Ich verteile die goldenen Hufeisen.", "Eins für die Kuh, eins für das Schaf.", "Und eins für die kleinen Küken.",
                      "Alle freuen sich und tanzen über die Wiese.",
                      "Und weißt du, was ich bekomme? Den größten, rotesten Apfel vom ganzen Hof! Mampf!",
                      "Das war ein tolles Abenteuer. Mit dir als Putz-Freundin!"]),
    ],
    "fips": [
        ("Das Glühwürmchen", ["Es ist Abend im Wald.", "Zwischen den Bäumen blinkt ein kleines Licht.", "Blink, blink. Was ist das?",
                              "Ich schleiche ganz leise hin.", "Es ist ein Glühwürmchen! Es heißt Lumi.", "Aber Lumi weint.",
                              "Ich habe meine Freunde verloren, schluchzt Lumi.",
                              "Ich verspreche ihr: Wir suchen sie zusammen! Ich habe schnelle Beine und gute Ohren.", "Morgen geht die Suche los!"]),
        ("Der Igel und die Frösche", ["Wir fragen zuerst den Igel am Bach.", "Er rollt sich auf: Lichter? Die flogen zum Moor!",
                                      "Also zum Moor! Hopp, hopp, hopp.", "Im Moor ist der Boden weich und matschig.", "Quak! Quak! Überall sitzen Frösche.",
                                      "Ein dicker Frosch hüpft auf ein Seerosenblatt.", "Lichter? Ja!, quakt er.",
                                      "Sie flogen zu dem alten Baum mit der kleinen Tür.", "Ein Baum mit einer Tür? Das schauen wir uns morgen an!"]),
        ("Der alte Baum", ["Da steht er: der dicke, alte Baum.", "Und wirklich, unten ist eine kleine Tür!", "Ich klopfe dreimal. Klopf, klopf, klopf.",
                           "Knarr. Die Tür geht auf.", "Drinnen wohnt eine Maus mit einer runden Brille.", "Ihr sucht Glühwürmchen?, piepst sie.",
                           "Sie holt eine Karte vom ganzen Wald.", "Sie sind bestimmt beim Silbersee. Dort ist es nachts am schönsten.",
                           "Danke, Maus! Morgen gehen wir zum Silbersee."]),
        ("Die Hängebrücke", ["Der Weg zum Silbersee führt über eine Schlucht.", "Darüber hängt eine Brücke aus Seilen.", "Sie schaukelt im Wind. Hui!",
                             "Ich bin ein bisschen ängstlich.", "Lumi leuchtet mir den Weg.", "Ein Schritt. Noch ein Schritt.",
                             "Nicht nach unten schauen, nur nach vorne!", "Geschafft! Wir sind drüben, und da glitzert der Silbersee!",
                             "Ich bin so stolz! Du warst bestimmt auch schon mal mutig, oder?"]),
        ("Der Biber", ["Am Silbersee sitzt ein Biber.", "Er sieht traurig aus.", "Mein Damm ist kaputt, sagt er.", "Jetzt läuft mein Teich aus.",
                       "Da helfen wir! Ich trage Stöcke.", "Lumi leuchtet, damit wir etwas sehen.", "Stock auf Stock, Stein auf Stein.",
                       "Der Damm hält! Der Biber klatscht mit seinem Schwanz aufs Wasser. Platsch!",
                       "Zum Dank verrät er uns: In der Höhle am Hügel habe ich Lichter gesehen."]),
        ("Die dunkle Höhle", ["Wir gehen zur Höhle.", "Drinnen ist es stockdunkel.", "Ich sehe nicht mal meine eigene Nase.", "Lumi leuchtet, so hell sie kann.",
                              "Da! Ein kleines Licht! Und noch eins!", "Ganz viele kleine Lichter an der Decke!", "Das sind Lumis Freunde!",
                              "Sie hatten sich vor dem Gewitter versteckt. Jetzt sind alle wieder zusammen!", "Lumi strahlt vor Freude, heller als je zuvor."]),
        ("Der leuchtende Wald", ["Heute Nacht fliegen alle Glühwürmchen nach Hause.", "Sie tanzen über der Wiese.", "Hoch und runter, hin und her.",
                                 "Der ganze Wald leuchtet.", "Die Eule staunt, der Igel staunt.", "Sogar der Biber kommt vorbei.",
                                 "Lumi fliegt zu mir und setzt sich auf meine Nase.", "Danke, Fips, sagt sie. Du bist der beste Freund der Welt.",
                                 "Und du hast so toll mitgeputzt! Bis zum nächsten Mal!"]),
    ],
}
INTRO = {"luna": "Hallo, ich bin Luna! Kapitel {n}: {t}. Mund auf, los geht's!",
         "fips": "Hey, ich bin Fips! Kapitel {n}: {t}. Bereit? Los geht's!"}
NUM = ["eins", "zwei", "drei", "vier", "fünf", "sechs", "sieben"]


def tts(text, voice):
    os.makedirs(CACHE, exist_ok=True)
    model = MODEL[voice]
    tag = voice if model == "gemini-3.8-flash-tts" else model + "|" + voice  # alte Einträge im Zwischenspeicher bleiben gültig
    key = hashlib.sha1((tag + "|" + text).encode()).hexdigest()[:16]
    path = os.path.join(CACHE, key + ".pcm")
    if not os.path.exists(path):
        body = {"contents": [{"parts": [{"text": text}]}],
                "generationConfig": {"responseModalities": ["AUDIO"],
                                     "speechConfig": {"voiceConfig": {"prebuiltVoiceConfig": {"voiceName": voice}}}}}
        req = urllib.request.Request(
            f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
            data=json.dumps(body).encode(), headers={"Content-Type": "application/json", "x-goog-api-key": os.environ["K"]})
        for attempt in range(8):  # das Kontingent erlaubt nur wenige Anfragen je Minute: bei 429 warten
            try:
                d = json.load(urllib.request.urlopen(req, timeout=120))
                break
            except Exception as e:
                if attempt == 7:
                    raise
                info = e.read().decode(errors="replace")[:300] if hasattr(e, "read") else ""
                wait = 65 if getattr(e, "code", 0) == 429 else 5
                print(f"  nochmal in {wait} s:", e, info.replace("\n", " ")[:200], file=sys.stderr, flush=True)
                time.sleep(wait)
        pcm = base64.b64decode(d["candidates"][0]["content"]["parts"][0]["inlineData"]["data"])
        open(path, "wb").write(pcm)
    return path


def mp3(pcm_path):  # Gemini liefert 24 kHz, 16 Bit, mono
    with tempfile.NamedTemporaryFile(suffix=".mp3") as t:
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "s16le", "-ar", "24000", "-ac", "1", "-i", pcm_path,
                        "-af", "silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse",
                        "-ar", "16000", "-ac", "1", "-b:a", "16k", t.name], check=True)
        return open(t.name, "rb").read()


def main():
    items = []
    for hero, voice in VOICE.items():
        for cid, text in CUES.items():
            items.append((f"{hero}/c/{cid}", text, voice))
        for ch, (title, parts) in enumerate(STORY[hero]):
            items.append((f"{hero}/{ch}/in", INTRO[hero].format(n=NUM[ch], t=title), voice))
            for k, text in enumerate(parts):
                items.append((f"{hero}/{ch}/{k}", text, voice))
    blobs = []
    for n, (cid, text, voice) in enumerate(items):
        print(f"[{n + 1}/{len(items)}] {cid}", flush=True)
        blobs.append((cid, mp3(tts(text, voice))))
    head = b"BV1" + struct.pack("<B", len(blobs))
    off = len(head) + len(blobs) * 28
    table, data = b"", b""
    for cid, b in blobs:
        assert len(cid) < 20, cid
        table += cid.encode().ljust(20, b"\0") + struct.pack("<II", off + len(data), len(b))
        data += b
    open(OUT, "wb").write(head + table + data)
    print(f"{OUT}: {len(blobs)} Stücke, {len(head + table + data) / 1024:.0f} KB")


if __name__ == "__main__":
    main()
