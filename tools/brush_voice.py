# Stimmen für das Zähneputzen: Pferd Luna und Reh Fips sprechen die Ansagen (KAI, Spiele, Schluss).
#   K=<Gemini-Schlüssel> python3 tools/brush_voice.py           # nur Ansagen – das kommt auf die Uhr
#   K=<Gemini-Schlüssel> python3 tools/brush_voice.py --story   # dazu die Hörgeschichten (≈ 1,2 MB, derzeit nicht auf der Uhr)
# Erzeugt mit Gemini-TTS (zwischengespeichert in tools/.tts-cache), kodiert als MP3 (16 kHz, mono, 16 kbit/s) und packt
# alles in firmware/brushvoice.bin, die in die Firmware eingebettet wird. Aufbau der Datei:
#   "BV1" + uint8 Anzahl + je Eintrag (char id[20], uint32 Versatz, uint32 Länge) + MP3-Daten (Little Endian)
import base64, hashlib, json, os, struct, subprocess, sys, tempfile, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", ".tts-cache")
OUT = os.path.join(ROOT, "firmware", "brushvoice.bin")
VOICE = {"luna": "Aoede", "fips": "Puck"}
# Das Kontingent gilt je Modell (100 Anfragen am Tag) – daher spricht jede Figur über ihr eigenes Modell
MODEL = {"Aoede": "gemini-3.1-flash-tts-preview", "Puck": "gemini-2.5-flash-preview-tts"}

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
    "monster": "Monsterjagd! Putz schön kräftig, dann verschwinden die Zahnmonster. Wenn du aufhörst, kommen sie zurück!",
    "pitch": "Hoch oder tief! Kommt der Schmetterling, summst du hoch. Kommt der Fisch, summst du tief. Summ erst einmal ganz normal.",
    "team": "Deine Schwester putzt auch gerade. Zusammen schafft ihr das!",
    "sticker": "Du hast einen neuen Sticker bekommen!",
    "streak": "Sieben Tage hintereinander geputzt! Du bist ein Zahnputz-Profi!",
}

# Hörgeschichten (nur mit --story): je Kapitel ein Titel und neun Stücke, eines je KAI-Abschnitt. Der Putz-Hinweis steckt in der Geschichte
# (keine eigene Ansage), damit sie rund durchläuft: Kauflächen oben rechts, oben links, unten links, unten rechts –
# außen rechts, vorne, links (Zähne zusammen, kleine Kreise) – innen oben, innen unten (von Rot nach Weiß).
# Länge: Kauflächen ≈ 6 s (Abschnitt 10 s), außen ≈ 8 s (13 s), innen ≈ 13 s (20 s) – Zeit zum Putzen bleibt.
STORY = {
    "luna": [
        ("Die Schatzkarte", [
            "Im Stall scharre ich im Stroh, hin und her, hin und her. Scharr mit: Backenzähne oben rechts!",
            "Da, unter dem Heu liegt ein altes Papier! Weiterschrubben, jetzt oben links.",
            "Ich puste den Staub weg. Pfff! Und du schrubbst unten links, hin und her.",
            "Es ist eine Schatzkarte, mit einem großen roten X! Jetzt unten rechts.",
            "Beiß die Zähne zusammen, wie ich, wenn ich ganz gespannt bin, und mal außen rechts kleine Kreise. Das X liegt hinter dem Hügel!",
            "Ein Schatz, für mich? Mein Schweif wedelt im Kreis, und deine Bürste macht vorne kleine Kreise.",
            "Jetzt außen links kreisen. Ich trabe los über die Wiese, klipp klapp, klipp klapp.",
            "Oh nein, Dornenbüsche versperren den Weg! Mach den Mund weit auf und putz innen oben, von Rot nach Weiß, so wie ich die Dornen zur Seite streiche.",
            "Und jetzt innen unten, von Rot nach Weiß. Hier komme ich heute nicht durch. Morgen suche ich einen anderen Weg. Kommst du dann wieder mit?"]),
        ("Am Bach", [
            "Heute nehme ich den Weg am Bach. Das Wasser plätschert hin und her. Schrubb mit, oben rechts!",
            "Der Bach ist breit. Wie komme ich bloß rüber? Schrubb weiter, oben links.",
            "Da, Steine im Wasser, wie eine Treppe! Jetzt unten links, hin und her.",
            "Ich setze einen Huf auf den ersten Stein. Und du putzt unten rechts.",
            "Zähne zusammenbeißen, ich muss mich konzentrieren! Außen rechts kleine Kreise. Der Stein ist glitschig, ich wackle. Huiii!",
            "Vorne kleine Kreise, so rund wie die Wellen im Wasser. Noch ein Stein, und noch einer!",
            "Jetzt außen links kreisen. Geschafft, ich schüttle mich trocken. Pitsch, patsch!",
            "Mund weit auf, so weit, wie ich gähne, wenn ich müde bin. Innen oben putzt du von Rot nach Weiß, immer vom Zahnfleisch weg. Vor mir beginnt ein großer, dunkler Wald.",
            "Und jetzt innen unten, von Rot nach Weiß. Im Wald rascheln die Blätter. Was wohl darin wartet? Das erzähle ich dir beim nächsten Putzen!"]),
        ("Die Eule", [
            "Im Wald ist es dämmrig und still. Ich scharre mit dem Huf, hin und her. Du auch: oben rechts!",
            "Plötzlich ruft jemand: Huhu! Schrubb weiter, oben links.",
            "Auf einem Ast sitzt eine Eule mit großen gelben Augen. Jetzt unten links.",
            "Wohin so spät, kleines Pferd?, fragt sie. Unten rechts schrubben!",
            "Zähne zusammen und außen rechts kleine Kreise. Ich zeige der Eule meine Schatzkarte, ganz stolz.",
            "Die Eule dreht ihren Kopf rundherum, im Kreis! Mach es ihr nach, mit kleinen Kreisen vorne.",
            "Und außen links weiterkreisen. Das kann ich nicht mit meinem Kopf, ich bin ja ein Pferd!",
            "Mund weit auf, wie die Eule beim Huhu! Innen oben putzt du von Rot nach Weiß. Die Eule flüstert: Folge den roten Pilzen, die führen dich zum Hügel.",
            "Jetzt innen unten, von Rot nach Weiß. Danke, liebe Eule! Morgen folge ich den Pilzen. Gute Nacht, Wald!"]),
        ("Die roten Pilze", [
            "Ich suche den ersten roten Pilz und schaue hin und her. Du schrubbst oben rechts.",
            "Da ist er, rot mit weißen Punkten! Jetzt oben links.",
            "Und da der nächste, und noch einer! Schrubb unten links.",
            "Pilz für Pilz gehe ich tiefer in den Wald. Unten rechts, hin und her.",
            "Zähne zusammenbeißen! Außen rechts kleine Kreise. Oh, der Weg teilt sich. Drei Wege!",
            "Links, rechts oder geradeaus? Ich drehe mich im Kreis. Mal du vorne kleine Kreise.",
            "Außen links weiterkreisen. Ich schaue genau hin: Nur auf dem mittleren Weg stehen rote Pilze.",
            "Also geradeaus! Mund weit auf, innen oben von Rot nach Weiß. Es wird heller und heller, und da ist eine Lichtung voller Blumen.",
            "Innen unten, von Rot nach Weiß. Hinter der Lichtung sehe ich den Hügel. Bald bin ich da, ich kann es kaum erwarten!"]),
        ("Das Gewitter", [
            "Heute ziehen dunkle Wolken auf. Der Wind weht hin und her. Schrubb oben rechts!",
            "Meine Mähne flattert im Wind. Jetzt oben links.",
            "Und dann: ein Blitz! Schnell, unten links schrubben.",
            "Und Donner! Rumms! Unten rechts, hin und her.",
            "Zähne zusammen, ich bin mutig! Außen rechts kleine Kreise. Ich stelle mich unter die große Tanne.",
            "Vorne kleine Kreise. Der Regen trommelt auf die Zweige. Tropf, tropf, tropf.",
            "Außen links kreisen. Ich bleibe ganz ruhig und warte, bis das Gewitter vorbei ist.",
            "Endlich hört der Regen auf! Mund weit auf, innen oben von Rot nach Weiß. Die Sonne kommt heraus, und am Himmel leuchtet ein Regenbogen.",
            "Innen unten, von Rot nach Weiß. Und weißt du, was genau unter dem Regenbogen liegt? Der Hügel mit dem X! Morgen bin ich dort."]),
        ("Das große X", [
            "Ich bin am Hügel angekommen und suche hin und her. Schrubb mit, oben rechts.",
            "Wo genau ist das X? Oben links weiter.",
            "Da, ein großer grauer Stein! Jetzt unten links.",
            "Ich schiebe mit meiner Nase. Uff! Unten rechts schrubben.",
            "Zähne zusammenbeißen und kräftig sein! Außen rechts kleine Kreise. Der Stein bewegt sich nicht.",
            "Noch einmal, mit aller Kraft! Vorne kleine Kreise. Uuuund er rollt zur Seite!",
            "Außen links kreisen. Unter dem Stein glitzert etwas Goldenes.",
            "Eine Kiste! Mund weit auf vor Staunen, innen oben von Rot nach Weiß. Ich klappe den Deckel auf, ganz langsam.",
            "Innen unten, von Rot nach Weiß. Die Kiste ist voller goldener Hufeisen! So viele! Die bringe ich beim nächsten Mal nach Hause."]),
        ("Das Fest", [
            "Mit der Kiste trabe ich zurück zum Hof, klipp klapp. Schrubb oben rechts.",
            "Die Hühner gackern: Was hat Luna da? Oben links.",
            "Alle Tiere kommen angelaufen. Jetzt unten links.",
            "Ich verteile die goldenen Hufeisen. Unten rechts, hin und her.",
            "Zähne zusammen und außen rechts kleine Kreise. Eins für die Kuh, eins für das Schaf.",
            "Vorne kleine Kreise. Und eins für die kleinen Küken, piep, piep!",
            "Außen links kreisen. Alle tanzen im Kreis über die Wiese.",
            "Mund weit auf, innen oben von Rot nach Weiß. Und weißt du, was ich bekomme? Den größten, rotesten Apfel vom ganzen Hof!",
            "Innen unten, von Rot nach Weiß. Das war ein tolles Abenteuer, mit dir zusammen. Danke, dass du mitgekommen bist!"]),
    ],
    "fips": [
        ("Das Glühwürmchen", [
            "Es ist Abend im Wald. Ich wackle mit den Ohren, hin und her. Du schrubbst oben rechts.",
            "Zwischen den Bäumen blinkt ein kleines Licht. Oben links weiter.",
            "Blink, blink. Was ist das? Jetzt unten links.",
            "Ich schleiche ganz leise hin. Unten rechts, hin und her.",
            "Zähne zusammen und außen rechts kleine Kreise. Es ist ein Glühwürmchen, es heißt Lumi.",
            "Vorne kleine Kreise, so wie Lumi im Kreis fliegt. Aber Lumi ist traurig.",
            "Außen links kreisen. Ich habe meine Freunde verloren, schluchzt Lumi.",
            "Mund weit auf, innen oben von Rot nach Weiß. Ich verspreche Lumi: Wir suchen sie zusammen! Ich habe schnelle Beine und gute Ohren.",
            "Innen unten, von Rot nach Weiß. Lumi leuchtet schon ein bisschen heller. Morgen geht die Suche los. Hilfst du uns wieder?"]),
        ("Der Igel und die Frösche", [
            "Wir fragen den Igel am Bach. Er schnuppert hin und her. Schrubb oben rechts!",
            "Lichter?, sagt der Igel. Die flogen zum Moor! Oben links.",
            "Also zum Moor, hopp, hopp, hopp! Jetzt unten links.",
            "Der Boden ist weich und matschig. Unten rechts schrubben.",
            "Zähne zusammen, außen rechts kleine Kreise. Quak, quak! Überall sitzen Frösche.",
            "Vorne kleine Kreise. Ein dicker Frosch hüpft auf ein Seerosenblatt. Platsch!",
            "Außen links kreisen. Lichter? Ja!, quakt der Frosch.",
            "Mund weit auf, so weit wie ein Froschmaul! Innen oben von Rot nach Weiß. Der Frosch sagt: Sie flogen zu dem alten Baum mit der kleinen Tür.",
            "Innen unten, von Rot nach Weiß. Ein Baum mit einer Tür? Das schauen wir uns beim nächsten Mal an!"]),
        ("Der alte Baum", [
            "Da steht er, der dicke, alte Baum. Ich laufe hin und her. Schrubb oben rechts.",
            "Und wirklich, unten ist eine kleine Tür! Oben links.",
            "Ich klopfe dreimal. Klopf, klopf, klopf. Unten links.",
            "Knarr. Die Tür geht auf. Unten rechts, hin und her.",
            "Zähne zusammen und außen rechts kleine Kreise. Drinnen wohnt eine Maus mit einer runden Brille.",
            "Vorne kleine Kreise, rund wie ihre Brille. Ihr sucht Glühwürmchen?, piepst sie.",
            "Außen links kreisen. Sie holt eine große Karte vom ganzen Wald.",
            "Mund weit auf, innen oben von Rot nach Weiß. Die Maus zeigt auf die Karte: Bestimmt sind sie beim Silbersee. Dort ist es nachts am schönsten.",
            "Innen unten, von Rot nach Weiß. Danke, liebe Maus! Beim nächsten Putzen gehen wir zum Silbersee."]),
        ("Die Hängebrücke", [
            "Der Weg zum Silbersee führt über eine tiefe Schlucht. Schrubb oben rechts, hin und her.",
            "Darüber hängt eine Brücke aus Seilen. Oben links.",
            "Sie schaukelt im Wind, hin und her. Unten links.",
            "Ich bin ein bisschen ängstlich. Unten rechts schrubben.",
            "Zähne zusammen, ganz mutig! Außen rechts kleine Kreise. Lumi leuchtet mir den Weg.",
            "Vorne kleine Kreise. Ein Schritt, noch ein Schritt.",
            "Außen links kreisen. Nicht nach unten schauen, nur nach vorne!",
            "Mund weit auf, innen oben von Rot nach Weiß. Geschafft, wir sind drüben! Und vor uns glitzert der Silbersee im Mondlicht.",
            "Innen unten, von Rot nach Weiß. Ich bin so stolz! Du bist bestimmt auch manchmal mutig, oder?"]),
        ("Der Biber", [
            "Am Silbersee sitzt ein Biber. Er schaut hin und her. Schrubb oben rechts.",
            "Er sieht traurig aus. Oben links.",
            "Mein Damm ist kaputt, sagt er. Unten links.",
            "Jetzt läuft mein Teich aus! Unten rechts schrubben.",
            "Da helfen wir! Zähne zusammen und außen rechts kleine Kreise. Ich trage Stöcke herbei.",
            "Vorne kleine Kreise. Lumi leuchtet, damit wir etwas sehen.",
            "Außen links kreisen. Stock auf Stock, Stein auf Stein.",
            "Mund weit auf, innen oben von Rot nach Weiß. Der Damm hält! Der Biber klatscht mit seinem Schwanz aufs Wasser. Platsch!",
            "Innen unten, von Rot nach Weiß. Zum Dank verrät er uns: In der Höhle am Hügel habe ich Lichter gesehen."]),
        ("Die dunkle Höhle", [
            "Wir gehen zur Höhle. Ich tapse vorsichtig hin und her. Schrubb oben rechts.",
            "Drinnen ist es stockdunkel. Oben links.",
            "Ich sehe nicht mal meine eigene Nase! Unten links.",
            "Lumi leuchtet, so hell sie kann. Unten rechts schrubben.",
            "Zähne zusammen und außen rechts kleine Kreise. Da, ein kleines Licht! Und noch eins!",
            "Vorne kleine Kreise. Ganz viele Lichter an der Decke!",
            "Außen links kreisen. Das sind Lumis Freunde!",
            "Mund weit auf, innen oben von Rot nach Weiß. Sie hatten sich vor dem Gewitter versteckt. Jetzt sind alle wieder zusammen!",
            "Innen unten, von Rot nach Weiß. Lumi strahlt vor Freude, heller als je zuvor. Beim nächsten Mal feiern wir ein Fest!"]),
        ("Der leuchtende Wald", [
            "Heute Nacht fliegen alle Glühwürmchen nach Hause, hin und her. Schrubb oben rechts.",
            "Sie tanzen über der Wiese. Oben links.",
            "Hoch und runter, hin und her. Unten links.",
            "Der ganze Wald leuchtet. Unten rechts schrubben.",
            "Zähne zusammen und außen rechts kleine Kreise. Die Eule staunt, der Igel staunt.",
            "Vorne kleine Kreise. Sogar der Biber kommt vorbei.",
            "Außen links kreisen. Lumi fliegt zu mir und setzt sich auf meine Nase.",
            "Mund weit auf, innen oben von Rot nach Weiß. Danke, Fips, sagt Lumi. Du bist der beste Freund der Welt.",
            "Innen unten, von Rot nach Weiß. Und du hast so toll mitgeputzt! Bis zum nächsten Mal!"]),
    ],
}
INTRO = {"luna": "Hallo, ich bin Luna! Kapitel {n}: {t}. Nimm deine Zahnbürste, wir fangen ganz hinten an.",
         "fips": "Hey, ich bin Fips! Kapitel {n}: {t}. Zahnbürste bereit? Wir fangen ganz hinten an."}
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
                pcm = base64.b64decode(d["candidates"][0]["content"]["parts"][0]["inlineData"]["data"])  # fehlt manchmal
                break
            except Exception as e:
                if attempt == 7:
                    raise
                info = e.read().decode(errors="replace")[:300] if hasattr(e, "read") else ""
                wait = 65 if getattr(e, "code", 0) == 429 else 5  # 429: Minutenkontingent; sonst kurz
                print(f"  nochmal in {wait} s:", e, info.replace("\n", " ")[:200], file=sys.stderr, flush=True)
                time.sleep(wait)
        open(path, "wb").write(pcm)
    return path


def mp3(pcm_path):  # Gemini liefert 24 kHz, 16 Bit, mono
    with tempfile.NamedTemporaryFile(suffix=".mp3") as t:
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "s16le", "-ar", "24000", "-ac", "1", "-i", pcm_path,
                        "-af", "silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,"
                        "afade=t=in:d=0.04,areverse,afade=t=in:d=0.03,apad=pad_dur=0.12",  # weiche Ränder, kurze Stille hinten: kein Knacken
                        "-ar", "16000", "-ac", "1", "-b:a", "16k", t.name], check=True)
        return open(t.name, "rb").read()


def main():
    items = []
    for hero, voice in VOICE.items():
        for cid, text in CUES.items():
            items.append((f"{hero}/c/{cid}", text, voice))
        for ch, (title, parts) in enumerate(STORY[hero] if "--story" in sys.argv else []):
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
