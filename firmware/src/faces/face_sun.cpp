// Daylight: der echte Tagesbogen der Sonne über dem Horizont, Himmelsfarbe nach Sonnenhöhe,
// Goldene und Blaue Stunde, verbleibendes Tageslicht und nachts der Mond in seiner Phase.
// Übertragen aus FaceLab (web/js/faces/sun.js, Stil „Himmel“); Koordinaten in Garmin-Pixeln (454).
#include <math.h>
#include <stdio.h>
#include "../fonts/fonts.h"
#include "dc.h"

namespace fc {

static const float HY = 302, XL = 72, XR = 382, CX = 227;
static const int ND = 25;
static const float SKY_E[6] = {-12, -6, -2, 3, 10, 25};
static const uint32_t SKY_Z[6] = {0x03061a, 0x0a1438, 0x1a2a5e, 0x24427e, 0x1d5296, 0x1b58a2};
static const uint32_t SKY_H[6] = {0x0a1030, 0x1f3a7a, 0x74466c, 0xe88c3a, 0x4a8bd0, 0x4f93d8};

// Tages-Zwischenspeicher (hängt nur an Tag und Ort)
static int kd = -999, ktz = -9999;
static float kla = -999, klo = -999;
static float riseM = -1, setM = -1, c6r = -1, c6s = -1, g6r = -1, g6s = -1, noonE = 0, Hh = 100;
static float dy_[ND], de_[ND];

static float r0() { return riseM >= 0 && setM >= 0 ? riseM : 0; }
static float dayLen() {
  if (riseM < 0 || setM < 0) return 1440;
  float l = setM - riseM;
  return l <= 0 ? l + 1440 : l;
}
static bool polar() { return riseM < 0 || setM < 0; }
static bool isDay(float m) {
  if (polar()) return noonE > 0;
  float u = m - riseM;
  if (u < 0) u += 1440;
  return u <= dayLen();
}

static void calc(const Data& d) {
  if (kd == d.doy && kla == d.lat && klo == d.lon && ktz == d.tz) return;
  kd = d.doy; kla = d.lat; klo = d.lon; ktz = d.tz;
  riseM = sunCross(d.doy, d.lat, d.lon, d.tz, -0.833f, true);
  setM = sunCross(d.doy, d.lat, d.lon, d.tz, -0.833f, false);
  c6r = sunCross(d.doy, d.lat, d.lon, d.tz, -6, true);
  c6s = sunCross(d.doy, d.lat, d.lon, d.tz, -6, false);
  g6r = sunCross(d.doy, d.lat, d.lon, d.tz, 6, true);
  g6s = sunCross(d.doy, d.lat, d.lon, d.tz, 6, false);
  noonE = sunElev(d.doy, sunNoon(d.doy, d.lon, d.tz), d.lat, d.lon, d.tz);
  Hh = clamp(82 + noonE, 92, 126);
  float len = dayLen(), s = r0();
  for (int i = 0; i < ND; i++) {
    float e = sunElev(d.doy, s + len * i / (ND - 1), d.lat, d.lon, d.tz);
    de_[i] = e;
    dy_[i] = HY - Hh * clamp(e, 0, 90) / fmaxf(noonE, 1);
  }
}

static void arcPos(const Data& d, float m, float& x, float& y) {
  float u = m - r0();
  if (u < 0) u += 1440;
  float f = clamp(u / dayLen(), 0, 1);
  float e = sunElev(d.doy, m, d.lat, d.lon, d.tz);
  x = XL + (XR - XL) * f;
  y = HY - Hh * clamp(e, 0, 90) / fmaxf(noonE, 1);
}

static void nightPos(float m, float& x, float& y) {
  float f;
  if (!polar()) {
    float u = m - setM;
    if (u < 0) u += 1440;
    f = clamp(u / (1440 - dayLen()), 0, 1);
  } else {
    f = m / 1440;
  }
  x = XL + (XR - XL) * f;
  y = HY - Hh * 0.82f * sinf(f * (float)M_PI);
}

static void sky(float e, uint32_t& z, uint32_t& h) {
  if (e <= SKY_E[0]) { z = SKY_Z[0]; h = SKY_H[0]; return; }
  if (e >= SKY_E[5]) { z = SKY_Z[5]; h = SKY_H[5]; return; }
  int i = 0;
  while (i < 4 && e > SKY_E[i + 1]) i++;
  float t = (e - SKY_E[i]) / (SKY_E[i + 1] - SKY_E[i]);
  z = mix(SKY_Z[i], SKY_Z[i + 1], t);
  h = mix(SKY_H[i], SKY_H[i + 1], t);
}

static void clk(char* b, size_t n, float m) {
  int r = (int)lroundf(m) % 1440;
  snprintf(b, n, "%02d:%02d", r / 60, r % 60);
}
static void hm(char* b, size_t n, float m) {
  int r = (int)fmaxf(0, roundf(m));
  snprintf(b, n, "%d:%02d", r / 60, r % 60);
}

static void status(const Data& d, float e, char* a, char* b, size_t n) {
  float m = d.mins;
  char t[16];
  b[0] = 0;
  if (polar()) {
    if (noonE > 0) { snprintf(a, n, "MITTERNACHTSSONNE"); snprintf(b, n, "SONNE %d°", (int)lroundf(e)); }
    else { snprintf(a, n, "POLARNACHT"); snprintf(b, n, "MOND %d %%", (int)lroundf(illum(d.moon) * 100)); }
    return;
  }
  if (isDay(m)) {
    float left = setM - m;
    if (left < 0) left += 1440;
    hm(t, sizeof(t), left);
    char light[40];
    snprintf(light, sizeof(light), "NOCH %s H LICHT", t);
    if (e < 6) { snprintf(a, n, "GOLDENE STUNDE"); snprintf(b, n, "%s", light); return; }
    snprintf(a, n, "%s", light);
    if (g6s >= 0) { clk(t, sizeof(t), g6s); snprintf(b, n, "GOLDENE STUNDE %s", t); }
    return;
  }
  float until = riseM - m;
  if (until < 0) until += 1440;
  hm(t, sizeof(t), until);
  char lightIn[32];
  snprintf(lightIn, sizeof(lightIn), "LICHT IN %s", t);
  if (e >= -6) {
    snprintf(a, n, "%s", e < -4 ? "BLAUE STUNDE" : "DÄMMERUNG");
    if (until < 180) { snprintf(b, n, "%s", lightIn); return; }
    float dk = c6s - m;
    if (dk < 0) dk += 1440;
    hm(t, sizeof(t), dk);
    snprintf(b, n, "DUNKEL IN %s", t);
    return;
  }
  snprintf(a, n, "%s", lightIn);
  snprintf(b, n, "MOND %d %%", (int)lroundf(illum(d.moon) * 100));
}

// Mondscheibe in der echten Phase (Sichel als Fläche zwischen Rand und Terminator)
static void moonDisc(Dc& dc, const Data& d, float x, float y, float r, float ph, uint32_t lit, uint32_t dark) {
  dc.setColor(dark);
  dc.fillCircle(x, y, r);
  if (illum(ph) < 0.03f) return;
  float s = ph < 0.5f ? 1 : -1;
  if (d.lat < 0) s = -s;
  float c = cosf(ph * 6.2831853f);
  float a[18], b[18];
  for (int i = 0; i <= 8; i++) {
    float an = (-90 + i * 22.5f) * 0.01745329f;
    a[2 * i] = x + s * r * cosf(an);
    a[2 * i + 1] = y + r * sinf(an);
    b[2 * i] = x + s * r * cosf(an) * c;
    b[2 * i + 1] = a[2 * i + 1];
  }
  dc.setColor(lit);
  dc.fillStrip(a, b, 9);
}

static void arrow(Dc& dc, float x, float y, bool up, uint32_t col) {
  dc.setColor(col);
  dc.setPenWidth(2);
  float k = up ? -1 : 1;
  dc.drawLine(x, y - 7 * k, x, y + 7 * k, true);
  dc.drawLine(x, y + 7 * k, x - 5, y + 2 * k, true);
  dc.drawLine(x, y + 7 * k, x + 5, y + 2 * k, true);
}

static void riseSet(Dc& dc, float x, float y, float m, bool up, uint32_t col, float sp) {
  if (m < 0 || sp <= 0) return;
  char t[8];
  clk(t, sizeof(t), m);
  float w = stW(t, 16) + 16, x0 = x - w / 2;
  uint32_t c = dim(col, sp);
  arrow(dc, x0 + 5, y, up, c);
  dc.setColor(c);
  st(dc, t, x0 + 16, y, 16, 2, 0, 1.0f);
}

static void stars(Dc& dc, const Data& d, const Anim& a, float vis, int n, float rMax, float yMax) {
  if (vis <= 0.02f) return;
  uint32_t sd = 7919 + d.doy;
  for (int i = 0; i < n; i++) {
    sd = rnd(sd); float x = 227 + (rf(sd) * 2 - 1) * rMax;
    sd = rnd(sd); float y = 30 + rf(sd) * (yMax - 30);
    sd = rnd(sd); float b = 0.35f + rf(sd) * 0.65f;
    if (fabsf(x - 227) < 150 && y > 40 && y < 162) continue;
    float dx = x - 227, dy = y - 227;
    if (dx * dx + dy * dy > rMax * rMax) continue;
    float tw = a.live ? b * (0.55f + 0.45f * sinf(a.t * 0.0031f + i * 1.73f)) : b;
    dc.setColor(dim(0xdfe6ff, tw * vis));
    if (b > 0.85f) dc.fillCircle(x, y, 1.6f);
    else dc.fillRect(x, y, 2, 2);
  }
}

static void sunDisc(Dc& dc, const Anim& a, float x, float y, float e, uint32_t bg) {
  float pulse = a.live ? sinf(a.t * 0.004f) : 0;
  uint32_t core = e < 6 ? mix(0xff9a3c, 0xffe08a, clamp((e + 1) / 7, 0, 1)) : 0xfff0c0;
  dc.setColor(mix(bg, core, 0.16f)); dc.fillCircle(x, y, 24 + pulse * 2);
  dc.setColor(mix(bg, core, 0.32f)); dc.fillCircle(x, y, 18 + pulse);
  dc.setColor(mix(bg, core, 0.6f)); dc.fillCircle(x, y, 14);
  dc.setColor(core); dc.fillCircle(x, y, 11);
  dc.setColor(mix(core, 0xffffff, 0.6f)); dc.fillCircle(x, y, 6);
}

static void draw(Dc& dc, const Data& d, const Anim& a) {
  calc(d);
  uint32_t acc = d.accent;
  float p = a.intro, sp = ease(p * 1.4f);
  float e = sunElev(d.doy, d.mins, d.lat, d.lon, d.tz);
  bool day = isDay(d.mins);
  uint32_t skz, skh;
  sky(e, skz, skh);

  // ---- Himmel
  const int nb = 24;
  for (int i = 0; i < nb; i++) {
    float t = (i + 0.5f) / nb;
    float y0 = floorf(i * HY / nb), y1 = floorf((i + 1) * HY / nb);
    dc.setColor(dim(mix(skz, skh, t * t), sp));
    dc.fillRect(0, y0, 454, y1 - y0);
  }
  if (!day && e > -9) {  // Dämmerungsschein, wo die Sonne unter dem Horizont steht
    float until = riseM - d.mins;
    if (until < 0) until += 1440;
    float gx = polar() ? CX : until < 720 ? XL : XR;
    float gk = clamp((e + 9) / 9, 0, 1) * sp;
    dc.setColor(mix(skh, 0xff9a5a, 0.12f * gk)); dc.fillEllipse(gx, HY, 140, 54);
    dc.setColor(mix(skh, 0xff9a5a, 0.24f * gk)); dc.fillEllipse(gx, HY, 96, 34);
    dc.setColor(mix(skh, 0xffb070, 0.4f * gk)); dc.fillEllipse(gx, HY, 54, 17);
  }
  dc.setColor(dim(mix(0, skh, 0.12f), sp));
  dc.fillRect(0, HY, 454, 454 - HY);

  // ---- Sterne
  stars(dc, d, a, clamp((-3 - e) / 7, 0, 1) * sp, 34, 205, HY - 14);

  // ---- Horizont
  uint32_t hc = mix(skh, 0xffffff, 0.45f);
  dc.setPenWidth(2);
  dc.setColor(dim(hc, sp * 0.35f));
  dc.drawLine(22, HY, 432, HY);
  dc.setColor(dim(hc, sp));
  float hw = 205 * ease(p * 1.2f);
  dc.drawLine(CX - hw * 0.8f, HY, CX + hw * 0.8f, HY);

  // ---- Sonne/Mond: im Intro vom Aufgang aus an ihren Platz
  float wp = easeInOut(p);
  float sm = d.mins, px, py;
  if (day) {
    float u = d.mins - r0();
    if (u < 0) u += 1440;
    sm = r0() + u * wp;
    arcPos(d, sm, px, py);
  } else {
    float nx, ny, n0x, n0y;
    nightPos(d.mins, nx, ny);
    nightPos(setM >= 0 ? setM : 0, n0x, n0y);
    px = n0x + (nx - n0x) * wp;
    py = n0y + (ny - n0y) * wp;
  }

  // ---- Punkte des Tagesbogens
  if (noonE > -0.833f) {
    float len = dayLen(), cur = sm - r0();
    if (cur < 0) cur += 1440;
    for (int i = 0; i < ND; i++) {
      float f = i / (float)(ND - 1);
      float x = XL + (XR - XL) * f, y = dy_[i];
      bool done = day && f * len <= cur;
      uint32_t c = de_[i] < 6 ? 0xffb560 : 0xffffff;
      float k = !day ? 0.28f : done ? 0.95f : 0.4f;
      uint32_t bgc = mix(skz, skh, (y / HY) * (y / HY));
      dc.setColor(dim(mix(bgc, c, k), sp));
      dc.fillCircle(x, y, done ? 2.6f : 2);
    }
  }

  if (day) {
    sunDisc(dc, a, px, py, e, dim(mix(skz, skh, (py / HY) * (py / HY)), sp));
  } else if (sp > 0.05f) {
    uint32_t bgc = mix(skz, skh, (py / HY) * (py / HY));
    dc.setColor(mix(bgc, 0xe8e4d0, 0.12f * sp));
    dc.fillCircle(px, py, 20);
    moonDisc(dc, d, px, py, 13, d.moon, dim(0xf2ecd8, sp), mix(bgc, 0x2a3350, sp));
  }

  // ---- Datum und Uhrzeit
  char b[48];
  float dp = ease(p * 1.6f);
  dc.setColor(dim(0xd8e0f0, dp));
  snprintf(b, sizeof(b), "%s %d. %s", d.dowStr, d.day, d.monStr);
  dc.text(b, CX, 50, &font_text_20, 1);
  dc.setColor(0xffffff);
  snprintf(b, sizeof(b), "%02d:%02d", d.h, d.m);
  st(dc, b, CX, 116, 72, 6, 1, easeInOut(p * 1.15f - 0.05f));

  // ---- Boden: Aufgang · Temperatur · Untergang
  float gp = stag(p, 1, 0.25f, 0.5f);
  riseSet(dc, 80, 326, riseM, true, 0xc8ccd6, gp);
  riseSet(dc, 374, 326, setM, false, 0xc8ccd6, gp);
  if (gp > 0) {
    dc.setColor(mix(0, d.dcol, gp));
    st(dc, d.slots[0].val, CX, 326, 18, 3, 1, 1.0f);
    dc.setColor(dim(0x808080, gp));
    dc.text(d.slots[0].lbl, CX, 341, &font_text_20, 1);
  }

  // ---- Status
  float tp = stag(p, 2, 0.2f, 0.5f);
  if (tp > 0) {
    char s1[40], s2[40];
    status(d, e, s1, s2, sizeof(s1));
    dc.setColor(dim(acc, tp));
    st(dc, s1, CX, 380, 20, 3, 1, tp);
    if (s2[0]) {
      dc.setColor(dim(0xaab0c0, tp));
      dc.text(s2, CX, 400, &font_text_20, 1);
    }
  }
  if (tp > 0) battery(dc, d, 428, dim(0x9aa0b0, tp));
}

const Face FACE_SUN = {"Daylight", "Sonnenbogen, Licht und Mond", 4, draw};

}  // namespace fc
