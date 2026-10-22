#include "phone.h"
#include <Preferences.h>
#include <esp_https_server.h>
#include <esp_random.h>
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include "audio.h"
#include "hw.h"
#include "kids.h"
#include "net.h"
#include "settings.h"
#include "webui.h"

namespace phone {

static httpd_handle_t srv = nullptr;
static volatile uint32_t lastAudio = 0;
static volatile bool open_ = false;
static volatile int clientFd = -1;
static volatile uint32_t lastSeen = 0;
static volatile bool up = false;  // Uhr spricht ans Handy
static String cert, key;

bool talking() { return open_ && millis() - lastAudio < 600; }
bool connected() { return srv && clientFd >= 0 && millis() - lastSeen < 15000; }

static void sendText(const char* t) {
  if (!connected()) return;
  httpd_ws_frame_t f = {};
  f.type = HTTPD_WS_TYPE_TEXT;
  f.payload = (uint8_t*)t;
  f.len = strlen(t);
  httpd_ws_send_frame_async(srv, clientFd, &f);
}

void talk(bool on) {
  if (on == up) return;
  if (on && !connected()) return;
  up = on;
  if (on) audio::micStart(); else audio::micStop();
  sendText(on ? "talk:1" : "talk:0");
}
String url() { return srv ? "https://" + netIp() + "/funk" : ""; }

// ------------------------------------------------------------------ Zertifikat (einmal erzeugt, im NVS)

static int rng(void*, unsigned char* out, size_t n) { esp_fill_random(out, n); return 0; }

static bool makeCert() {
  Preferences p;
  p.begin("tls", true);
  cert = p.getString("crt", "");
  key = p.getString("key", "");
  p.end();
  if (cert.length() && key.length()) return true;
  Serial.println("Funk vom Handy: erzeuge Zertifikat …");
  mbedtls_pk_context pk;
  mbedtls_x509write_cert crt;
  mbedtls_pk_init(&pk);
  mbedtls_x509write_crt_init(&crt);
  bool ok = false;
  unsigned char* buf = (unsigned char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  do {
    if (!buf || mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), rng, nullptr)) break;
    char subj[96];
    snprintf(subj, sizeof(subj), "CN=%s.local,O=Taschenfunk", netHost());
    mbedtls_x509write_crt_set_subject_key(&crt, &pk);
    mbedtls_x509write_crt_set_issuer_key(&crt, &pk);
    mbedtls_x509write_crt_set_subject_name(&crt, subj);
    mbedtls_x509write_crt_set_issuer_name(&crt, subj);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    unsigned char serial[8];
    esp_fill_random(serial, sizeof(serial));
    serial[0] &= 0x7F;
    mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
    mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20460101000000");
    mbedtls_x509write_crt_set_basic_constraints(&crt, 1, -1);
    if (mbedtls_x509write_crt_pem(&crt, buf, 4096, rng, nullptr)) break;
    cert = (const char*)buf;
    if (mbedtls_pk_write_key_pem(&pk, buf, 4096)) break;
    key = (const char*)buf;
    ok = true;
  } while (0);
  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&pk);
  if (buf) heap_caps_free(buf);
  if (!ok) { Serial.println("Funk vom Handy: Zertifikat fehlgeschlagen"); return false; }
  p.begin("tls", false);
  p.putString("crt", cert);
  p.putString("key", key);
  p.end();
  return true;
}

// ------------------------------------------------------------------ Seite und Sprachkanal

static const char PAGE[] = R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"><title>Funk</title>
<style>body{margin:0;height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:24px;
background:#0d0d0f;color:#f4f1ea;font:17px -apple-system,system-ui,sans-serif;-webkit-user-select:none;user-select:none}
#b{width:220px;height:220px;border-radius:50%;border:0;background:#3ddc97;color:#000;font-size:22px;font-weight:600;touch-action:none}
#b.on{background:#ff453a;color:#fff;transform:scale(1.06)}#s{color:#8e8a82;min-height:1.4em;text-align:center;padding:0 20px}
a{color:#e0b25e}</style></head><body><h2 id="n">Funk</h2><button id="b">Halten<br>und sprechen</button><div id="s">Verbinde …</div>
<div id="u" style="color:#3ddc97;min-height:1.4em"></div><a href="/">zurück zur Weboberfläche</a>
<script>
let ws,ctx,src,proc,on=false,pin='',playAt=0;
function audio(){if(!ctx)ctx=new (window.AudioContext||window.webkitAudioContext)();if(ctx.state=='suspended')ctx.resume()}
document.addEventListener('pointerdown',audio,{once:true});
setInterval(()=>{if(ws&&ws.readyState==1)ws.send('ping')},5000);
// Stimme von der Uhr (16 kHz) abspielen
function play(buf){if(!ctx)return;const x=new Int16Array(buf),b=ctx.createBuffer(1,x.length,16000),c=b.getChannelData(0);
 for(let i=0;i<x.length;i++)c[i]=x[i]/32768;const s=ctx.createBufferSource();s.buffer=b;s.connect(ctx.destination);
 playAt=Math.max(playAt,ctx.currentTime+0.05);s.start(playAt);playAt+=b.duration}try{pin=localStorage.getItem('pin')||''}catch(e){}
const s=t=>document.getElementById('s').textContent=t,b=document.getElementById('b');
function connect(){ws=new WebSocket('wss://'+location.host+'/ws');ws.binaryType='arraybuffer';
 ws.onopen=()=>{ws.send('pin:'+pin);s('Bereit')};ws.onclose=()=>{s('Getrennt – verbinde neu …');setTimeout(connect,1500)};
 ws.onmessage=e=>{if(typeof e.data!='string')return play(e.data);
  if(e.data=='talk:1')document.getElementById('u').textContent=ctx?'Die Uhr spricht …':'Die Uhr spricht – einmal tippen, um sie zu hören';
  if(e.data=='talk:0')document.getElementById('u').textContent='';
  if(e.data=='pin?'){pin=prompt('Eltern-PIN')||'';try{localStorage.setItem('pin',pin)}catch(x){}ws.send('pin:'+pin)}}}
connect();
async function mic(){if(proc)return;const st=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true,autoGainControl:true}});
 audio();src=ctx.createMediaStreamSource(st);proc=ctx.createScriptProcessor(2048,1,1);
 let ph=0;const r=ctx.sampleRate/16000;
 proc.onaudioprocess=e=>{if(!on||ws.readyState!=1)return;const x=e.inputBuffer.getChannelData(0),out=new Int16Array(Math.floor(x.length/r)+1);let k=0;
  for(;ph<x.length;ph+=r){const v=Math.max(-1,Math.min(1,x[Math.floor(ph)]));out[k++]=v*32767}ph-=x.length;ws.send(out.subarray(0,k).buffer)};
 src.connect(proc);proc.connect(ctx.destination)}
async function down(e){e.preventDefault();try{await mic()}catch(x){s('Mikrofon nicht erlaubt');return}
 if(ctx.state=='suspended')ctx.resume();on=true;b.classList.add('on');s('Du sprichst …');ws.send('start')}
function up(e){if(!on)return;e.preventDefault();on=false;b.classList.remove('on');s('Bereit');ws.send('stop')}
b.addEventListener('pointerdown',down);['pointerup','pointercancel','pointerleave'].forEach(t=>b.addEventListener(t,up));
</script></body></html>)HTML";

static esp_err_t pageHandler(httpd_req_t* r) {
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  return httpd_resp_send(r, PAGE, HTTPD_RESP_USE_STRLEN);
}

static bool authed = false;

static esp_err_t wsHandler(httpd_req_t* r) {
  if (r->method == HTTP_GET) {  // Handshake: dieses Handy ist jetzt der Gesprächspartner
    authed = !kids::cfg().pin[0];
    clientFd = httpd_req_to_sockfd(r);
    lastSeen = millis();
    return ESP_OK;
  }
  lastSeen = millis();
  clientFd = httpd_req_to_sockfd(r);  // das Handy, das zuletzt etwas geschickt hat, ist der Gesprächspartner
  httpd_ws_frame_t f = {};
  if (httpd_ws_recv_frame(r, &f, 0) != ESP_OK || !f.len || f.len > 16384) return ESP_FAIL;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(f.len + 1, MALLOC_CAP_SPIRAM);
  if (!buf) return ESP_FAIL;
  f.payload = buf;
  esp_err_t e = httpd_ws_recv_frame(r, &f, f.len);
  if (e == ESP_OK) {
    if (f.type == HTTPD_WS_TYPE_TEXT) {
      buf[f.len] = 0;
      const char* t = (const char*)buf;
      if (!strncmp(t, "pin:", 4)) {
        authed = !kids::cfg().pin[0] || !strcmp(t + 4, kids::cfg().pin);
        if (!authed) {
          httpd_ws_frame_t q = {};
          q.type = HTTPD_WS_TYPE_TEXT;
          q.payload = (uint8_t*)"pin?";
          q.len = 4;
          httpd_ws_send_frame(r, &q);
        }
      } else if (!strcmp(t, "start") && authed) {
        open_ = true;
        lastAudio = millis();
      } else if (!strcmp(t, "stop")) {
        open_ = false;
      }
    } else if (f.type == HTTPD_WS_TYPE_BINARY && authed) {
      audio::voiceWrite((const int16_t*)buf, f.len / 2);  // 16 kHz mono, wie der Funk
      lastAudio = millis();
      open_ = true;
    }
  }
  heap_caps_free(buf);
  return e;
}

void poll() {
  // Uhr spricht: Mikrofon in 40-ms-Paketen ans Handy
  if (up) {
    static int16_t mic[640];
    static size_t have = 0;
    have += audio::micRead(mic + have, 640 - have);
    if (have == 640) {
      httpd_ws_frame_t f = {};
      f.type = HTTPD_WS_TYPE_BINARY;
      f.payload = (uint8_t*)mic;
      f.len = sizeof(mic);
      if (!connected() || httpd_ws_send_frame_async(srv, clientFd, &f) != ESP_OK) talk(false);
      have = 0;
    }
  }
  static uint32_t triedAt = 0;
  if (srv || !netOnline() || (triedAt && millis() - triedAt < 60000)) return;
  triedAt = millis();
  if (!makeCert()) return;
  httpd_ssl_config_t c = HTTPD_SSL_CONFIG_DEFAULT();
  c.servercert = (const uint8_t*)cert.c_str();
  c.servercert_len = cert.length() + 1;
  c.prvtkey_pem = (const uint8_t*)key.c_str();
  c.prvtkey_len = key.length() + 1;
  c.httpd.max_open_sockets = 5;
  c.httpd.lru_purge_enable = true;
  c.httpd.uri_match_fn = httpd_uri_match_wildcard;
  c.httpd.stack_size = 8192;
  c.httpd.ctrl_port = 32769;  // der Umleitungs-Server auf Port 80 belegt den Standard
  if (httpd_ssl_start(&srv, &c) != ESP_OK) { srv = nullptr; Serial.println("Funk vom Handy: HTTPS-Start fehlgeschlagen"); return; }
  httpd_uri_t page = {.uri = "/funk", .method = HTTP_GET, .handler = pageHandler, .user_ctx = nullptr};
  httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = wsHandler, .user_ctx = nullptr, .is_websocket = true};
  httpd_uri_t pg = {.uri = "/*", .method = HTTP_GET, .handler = webui::handle, .user_ctx = nullptr};  // Weboberfläche
  httpd_uri_t pp = {.uri = "/*", .method = HTTP_POST, .handler = webui::handle, .user_ctx = nullptr};
  httpd_register_uri_handler(srv, &page);
  httpd_register_uri_handler(srv, &ws);
  httpd_register_uri_handler(srv, &pg);
  httpd_register_uri_handler(srv, &pp);
  Serial.printf("Funk vom Handy: %s\n", url().c_str());
}

}  // namespace phone
