#!/usr/bin/env python3
"""YT-Music-Brücke für den Taschenfunk.

Der ESP32 kann YouTube Music nicht selbst abspielen (Signatur-Entschlüsselung, Opus/AAC). Diese Brücke läuft auf
einem Rechner im selben WLAN (Mac, Raspberry Pi, NAS …) und übernimmt das:

  /api/browse?path=…    Listen für die Uhr (Startmenü, Suche, Playlists, Alben, Radio …) als JSON
  /stream/<videoId>     Ton als 32 kHz / 16 Bit / mono PCM (yt-dlp + ffmpeg)
  /art/<videoId>        Cover als rundes RGB565-Bild
  /api/remote/…         Fernbedienung: Befehle von der Weboberfläche an die Uhr
  /                     Weboberfläche fürs Handy (suchen, abspielen, Lautstärke)

Start:  python3 server.py            (Port 8765, meldet sich per mDNS als _ytbridge._tcp)
Anmeldung (optional, für Bibliothek & Lieblingssongs): siehe README.md
"""
import argparse
import io
import json
import os
import queue
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import certifi
from PIL import Image, ImageDraw
from ytmusicapi import YTMusic
import yt_dlp

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 32000
# Kleiner Lautsprecher: Bass weg (verzerrt nur), etwas Kompression für mehr Lautheit
AUDIO_FILTER = "highpass=f=140,acompressor=threshold=-20dB:ratio=3:attack=5:release=120:makeup=4dB,alimiter=limit=0.93"

SSL = ssl.create_default_context(cafile=certifi.where())
yt_lock = threading.Lock()
yt = None
authed = False

# videoId -> Infos (Titel, Interpret, Cover-URL), wird beim Auflisten gefüllt
track_info = {}
# videoId -> (Audio-URL, Ablaufzeit)
url_cache = {}
art_cache = {}
shelf_cache = []


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ---------------------------------------------------------------------------------------------- YouTube Music


def ytm():
    global yt, authed
    if yt is None:
        for name in ("browser.json", "oauth.json"):
            path = os.path.join(HERE, name)
            if os.path.exists(path):
                try:
                    yt = YTMusic(path, language="de", location="DE")
                    authed = True
                    log("Angemeldet über", name)
                    break
                except Exception as e:  # noqa: BLE001
                    log("Anmeldung mit", name, "fehlgeschlagen:", e)
        if yt is None:
            yt = YTMusic(language="de", location="DE")
            log("Ohne Anmeldung (nur Suche, Radio, Startseite)")
    return yt


def artists_of(it):
    a = it.get("artists") or []
    if isinstance(a, list):
        return ", ".join(x.get("name", "") for x in a if x.get("name"))
    return it.get("artist") or ""


def thumb_of(it):
    th = it.get("thumbnails") or it.get("thumbnail") or []
    if isinstance(th, dict):
        th = th.get("thumbnails") or []
    if isinstance(th, list) and th:
        return th[-1].get("url")
    return None


def secs(it):
    if it.get("duration_seconds"):
        return int(it["duration_seconds"])
    d = it.get("duration") or it.get("length") or ""
    try:
        parts = [int(p) for p in str(d).split(":")]
        s = 0
        for p in parts:
            s = s * 60 + p
        return s
    except ValueError:
        return 0


def track(it):
    vid = it.get("videoId")
    if not vid:
        return None
    t = {"kind": "track", "id": vid, "title": it.get("title") or "?", "sub": artists_of(it), "dur": secs(it)}
    track_info[vid] = {"title": t["title"], "artist": t["sub"], "thumb": thumb_of(it)}
    return t


def lst(id_, title, sub=""):
    return {"kind": "list", "id": id_, "title": title, "sub": sub}


def item_from(it):
    """Ein Eintrag aus Suche/Startseite → Titel oder Liste."""
    if it.get("videoId"):
        return track(it)
    if it.get("playlistId") and it.get("resultType") != "album" and not it.get("browseId", "").startswith("MPRE"):
        return lst("playlist:" + it["playlistId"], it.get("title", "Playlist"), it.get("description") or artists_of(it) or "Playlist")
    bid = it.get("browseId") or ""
    if bid.startswith("MPRE") or it.get("resultType") == "album" or it.get("type") in ("Album", "Single", "EP"):
        return lst("album:" + bid, it.get("title", "Album"), artists_of(it) or it.get("type") or "Album")
    if bid.startswith("UC") or it.get("resultType") == "artist":
        a = (it.get("artists") or [{}])[0]
        bid = bid or a.get("id") or ""
        if not bid:
            return None
        return lst("artist:" + bid, it.get("artist") or a.get("name") or it.get("title") or "Interpret", "Interpret")
    if bid.startswith("VL"):
        return lst("playlist:" + bid[2:], it.get("title", "Playlist"), "Playlist")
    return None


def browse(path):
    y = ytm()
    kind, _, arg = path.partition(":")
    with yt_lock:
        if path == "home":
            items = [lst("@search", "Suchen", "Titel, Interpreten, Alben")]
            if authed:
                items += [lst("liked", "Lieblingssongs", "Deine Likes"), lst("library", "Playlists", "Aus deiner Bibliothek"),
                          lst("history", "Zuletzt gehört", "Verlauf")]
            items += [lst("mixes", "Entdecken", "Mixe und Empfehlungen")]
            return "Musik", items
        if kind == "search":
            res = y.search(arg, limit=20)
            songs = [track(r) for r in res if r.get("resultType") == "song"]
            songs += [track(r) for r in res if r.get("resultType") == "video"]
            others = [item_from(r) for r in res if r.get("resultType") in ("album", "playlist", "artist")]
            return f"„{arg}“", [i for i in songs + others if i][:60]
        if kind == "playlist":
            pl = y.get_playlist(arg, limit=100)
            return pl.get("title", "Playlist"), [t for t in (track(x) for x in pl.get("tracks", [])) if t]
        if kind == "album":
            al = y.get_album(arg)
            thumb = thumb_of(al)
            out = []
            for x in al.get("tracks", []):
                t = track(x)
                if t:
                    if not t["sub"]:
                        t["sub"] = artists_of(al)
                    if thumb and not track_info[t["id"]]["thumb"]:
                        track_info[t["id"]]["thumb"] = thumb
                    out.append(t)
            return al.get("title", "Album"), out
        if kind == "artist":
            ar = y.get_artist(arg)
            out = []
            if ar.get("radioId"):
                out.append(lst("mix:" + ar["radioId"], "Radio", "Endlos ähnliche Titel"))
            songs = ar.get("songs") or {}
            if songs.get("browseId"):
                out.append(lst("playlist:" + songs["browseId"].removeprefix("VL"), "Alle Titel", ar.get("name", "")))
            out += [t for t in (track(x) for x in songs.get("results", [])) if t]
            for key in ("albums", "singles"):
                for a in (ar.get(key) or {}).get("results", []):
                    out.append(lst("album:" + a.get("browseId", ""), a.get("title", "Album"), a.get("year", "") or key))
            return ar.get("name", "Interpret"), out
        if kind == "mix":
            wp = y.get_watch_playlist(playlistId=arg, limit=50)
            return "Radio", [t for t in (track(x) for x in wp.get("tracks", [])) if t]
        if kind == "radio":
            wp = y.get_watch_playlist(videoId=arg, radio=True, limit=50)
            return "Radio", [t for t in (track(x) for x in wp.get("tracks", [])) if t]
        if path == "liked":
            pl = y.get_liked_songs(limit=200)
            return "Lieblingssongs", [t for t in (track(x) for x in pl.get("tracks", [])) if t]
        if path == "library":
            pls = y.get_library_playlists(limit=50)
            return "Playlists", [lst("playlist:" + p["playlistId"], p.get("title", "Playlist"), p.get("count", "") and f"{p['count']} Titel")
                                 for p in pls]
        if path == "history":
            return "Zuletzt gehört", [t for t in (track(x) for x in y.get_history()) if t][:100]
        if path == "mixes":
            shelf_cache[:] = y.get_home(limit=8)
            return "Entdecken", [lst(f"shelf:{i}", s.get("title", "Empfehlungen"), f"{len(s.get('contents') or [])} Einträge")
                                 for i, s in enumerate(shelf_cache)]
        if kind == "shelf":
            i = int(arg)
            if i >= len(shelf_cache):
                shelf_cache[:] = y.get_home(limit=8)
            s = shelf_cache[i]
            return s.get("title", ""), [x for x in (item_from(c) for c in s.get("contents") or [] if c) if x]
    raise ValueError("Unbekannter Pfad: " + path)


# ---------------------------------------------------------------------------------------------- Audio und Cover


def audio_url(vid):
    hit = url_cache.get(vid)
    if hit and hit[1] > time.time():
        return hit[0]
    opts = {"format": "bestaudio/best", "quiet": True, "no_warnings": True, "noplaylist": True}
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info("https://music.youtube.com/watch?v=" + vid, download=False)
    url = info["url"]
    url_cache[vid] = (url, time.time() + 3 * 3600)
    if info.get("duration"):
        track_info.setdefault(vid, {})["dur"] = int(info["duration"])
    if vid not in track_info or not track_info[vid].get("thumb"):
        th = info.get("thumbnails") or []
        squares = [t for t in th if t.get("width") and t.get("width") == t.get("height")]
        track_info.setdefault(vid, {"title": info.get("title"), "artist": info.get("uploader")})
        track_info[vid]["thumb"] = (squares or th or [{}])[-1].get("url")
    return url


def prefetch(vid):
    threading.Thread(target=lambda: _safe(audio_url, vid), daemon=True).start()


def _safe(fn, *a):
    try:
        fn(*a)
    except Exception:  # noqa: BLE001
        pass


def make_art(vid, size):
    key = (vid, size)
    if key in art_cache:
        return art_cache[key]
    url = (track_info.get(vid) or {}).get("thumb")
    if url and "googleusercontent" in url:
        url = url.split("=w")[0] + f"=w{size * 2}-h{size * 2}-l90-rj"
    if not url:
        url = f"https://i.ytimg.com/vi/{vid}/hqdefault.jpg"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    data = urllib.request.urlopen(req, timeout=10, context=SSL).read()
    im = Image.open(io.BytesIO(data)).convert("RGB")
    w, h = im.size
    if "ytimg.com" in url and abs(w / h - 4 / 3) < 0.05:  # Video-Vorschau 4:3 mit Balken → Mitte ausschneiden
        side = int(w * 9 / 16)
        im = im.crop(((w - side) // 2, (h - side) // 2, (w + side) // 2, (h + side) // 2))
    else:
        side = min(w, h)
        im = im.crop(((w - side) // 2, (h - side) // 2, (w + side) // 2, (h + side) // 2))
    im = im.resize((size, size), Image.LANCZOS)
    # runde Maske, weich gezeichnet
    mask = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    bg = Image.new("RGB", (size, size), (0, 0, 0))
    bg.paste(im, (0, 0), mask)
    raw = bytearray(size * size * 2)
    px = bg.load()
    i = 0
    for yy in range(size):
        for xx in range(size):
            r, g, b = px[xx, yy]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            raw[i] = v & 0xFF
            raw[i + 1] = v >> 8
            i += 2
    art_cache[key] = bytes(raw)
    if len(art_cache) > 200:
        art_cache.pop(next(iter(art_cache)))
    return art_cache[key]


# ---------------------------------------------------------------------------------------------- Fernbedienung


class Device:
    def __init__(self, dev):
        self.dev = dev
        self.q = queue.Queue()
        self.status = {}
        self.seen = 0


devices = {}
dev_lock = threading.Lock()


def device(dev):
    with dev_lock:
        if dev not in devices:
            devices[dev] = Device(dev)
        return devices[dev]


# ---------------------------------------------------------------------------------------------- HTTP


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "YTBridge/1.0"

    def log_message(self, fmt, *args):
        pass

    def send(self, code, body=b"", ctype="application/json", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if body and self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        try:
            if u.path in ("/", "/index.html"):
                with open(os.path.join(HERE, "remote.html"), "rb") as f:
                    return self.send(200, f.read(), "text/html; charset=utf-8")
            if u.path == "/api/ping":
                return self.send(200, {"ok": True, "auth": authed, "rate": RATE})
            if u.path == "/api/browse":
                path = q.get("path", "home")
                t0 = time.time()
                try:
                    title, items = browse(path)
                except Exception as e:  # noqa: BLE001
                    log("browse", path, "Fehler:", e)
                    return self.send(200, {"error": "YouTube Music: " + str(e)[:80]})
                log(f"browse {path} → {len(items)} Einträge ({time.time() - t0:.1f} s)")
                tracks = [i for i in items if i["kind"] == "track"]
                if tracks:
                    prefetch(tracks[0]["id"])
                return self.send(200, {"title": title, "items": items})
            if u.path.startswith("/stream/"):
                return self.stream(u.path.split("/")[2], int(float(q.get("start", "0"))))
            if u.path.startswith("/art/"):
                vid = u.path.split("/")[2]
                size = max(32, min(400, int(q.get("size", "220"))))
                try:
                    return self.send(200, make_art(vid, size), "application/octet-stream")
                except Exception as e:  # noqa: BLE001
                    log("Cover", vid, "Fehler:", e)
                    return self.send(404, {"error": "kein Cover"})
            if u.path == "/api/remote/wait":
                return self.remote_wait(q)
            if u.path == "/api/devices":
                now = time.time()
                with dev_lock:
                    out = [dict(d.status, dev=d.dev, online=now - d.seen < 40) for d in devices.values() if now - d.seen < 600]
                for d in out:
                    info = track_info.get(d.get("id") or "", {})
                    d["title"], d["artist"] = info.get("title", ""), info.get("artist", "")
                return self.send(200, out)
            return self.send(404, {"error": "nicht gefunden"})
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}")
        if u.path == "/api/remote/send":
            d = device(q.get("dev", ""))
            d.q.put(body)
            log("Fernbedienung →", d.dev, body.get("cmd"))
            return self.send(200, {"ok": True})
        return self.send(404, {"error": "nicht gefunden"})

    def remote_wait(self, q):
        d = device(q.get("dev", "?"))
        d.seen = time.time()
        d.status = {k: q.get(k) for k in ("name", "state", "id", "pos", "vol", "bat")}
        try:
            cmd = d.q.get(timeout=25)
        except queue.Empty:
            return self.send(204)
        d.seen = time.time()
        return self.send(200, cmd)

    def stream(self, vid, start):
        t0 = time.time()
        try:
            url = audio_url(vid)
        except Exception as e:  # noqa: BLE001
            log("Stream", vid, "nicht verfügbar:", e)
            return self.send(502, {"error": str(e)[:100]})
        cmd = ["ffmpeg", "-nostdin", "-loglevel", "error", "-reconnect", "1", "-reconnect_streamed", "1",
               "-reconnect_delay_max", "4", "-ss", str(start), "-i", url, "-vn", "-ac", "1", "-ar", str(RATE)]
        if AUDIO_FILTER:
            cmd += ["-af", AUDIO_FILTER]
        cmd += ["-f", "s16le", "-"]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        info = track_info.get(vid, {})
        log(f"▶ {info.get('artist', '')} – {info.get('title', vid)} ab {start} s (bereit nach {time.time() - t0:.1f} s)")
        self.send_response(200)
        self.send_header("Content-Type", f"audio/L16;rate={RATE};channels=1")
        if info.get("dur"):
            self.send_header("X-Duration", str(max(0, info["dur"] - start)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        sent = 0
        try:
            while True:
                chunk = p.stdout.read(8192)
                if not chunk:
                    break
                self.wfile.write(chunk)
                sent += len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            p.kill()
            p.wait()
        log(f"■ {info.get('title', vid)}: {sent / RATE / 2:.0f} s gesendet")


# ---------------------------------------------------------------------------------------------- Start


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def announce(port):
    try:
        from zeroconf import ServiceInfo, Zeroconf
    except ImportError:
        log("zeroconf fehlt – die Uhr findet die Brücke dann nur über die eingetragene Adresse")
        return None
    ip = lan_ip()
    host = "".join(c if c.isalnum() else "-" for c in socket.gethostname().split(".")[0]).lower()
    # eigener Servername, damit zeroconf selbst den A-Eintrag beantwortet; die IP steht zusätzlich im TXT-Eintrag
    info = ServiceInfo("_ytbridge._tcp.local.", f"YT-Bruecke {host}._ytbridge._tcp.local.",
                       addresses=[socket.inet_aton(ip)], port=port, properties={"v": "1", "ip": ip},
                       server=f"ytbridge-{host}.local.")
    zc = Zeroconf()
    zc.register_service(info)
    log(f"mDNS: _ytbridge._tcp auf {ip}:{port}")
    return zc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-filter", action="store_true", help="Ton unverändert (ohne Hochpass/Kompressor)")
    args = ap.parse_args()
    global AUDIO_FILTER
    if args.no_filter:
        AUDIO_FILTER = ""
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg fehlt (macOS: brew install ffmpeg, Debian/Raspberry Pi: sudo apt install ffmpeg)")
    ytm()
    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    srv.daemon_threads = True
    zc = announce(args.port)
    log(f"Brücke läuft: http://{lan_ip()}:{args.port}  (Weboberfläche im Browser öffnen)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if zc:
            zc.close()


if __name__ == "__main__":
    main()
