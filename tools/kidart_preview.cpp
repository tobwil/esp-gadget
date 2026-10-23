// Vorschau des Kinder-Zifferblatts am Rechner: rendert jede Welt mit allen Szenen und der Abenteuer-Karte als PPM.
//   sh tools/kidart_preview.sh   (baut, rendert und setzt die Bilder zu docs/screens/kinderwelten.png zusammen)
#include <string>
#include <stdio.h>
#include "kidart.h"

using namespace kidart;

static void save(const char* path, const uint16_t* px) {
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", S, S);
  for (int i = 0; i < S * S; i++) {
    uint16_t c = px[i];
    unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                            (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : ".";
  static uint16_t px[S * S];
  struct { kids::Act act; const char* name; bool adv; } shots[] = {
      {kids::WAKE, "wake", false},   {kids::BREAKFAST, "breakfast", false}, {kids::DAYCARE, "school", false},
      {kids::LUNCH, "lunch", false}, {kids::PLAY, "play", false},           {kids::BATH, "bath", false},
      {kids::STORY, "story", false}, {kids::SLEEP, "sleep", false},         {kids::PLAY, "adventure", true}};
  Pet pets[WORLDS] = {CAT, DRAGON, DOG, BUNNY, CAT};
  for (int w = 0; w < WORLDS; w++)
    for (auto& s : shots) {
      Scene sc{s.act, false, (World)w, pets[w], (uint8_t)(1 + w % 3), w % 2 == 0, s.adv, 3, 0.45f, 4};
      draw(px, sc);
      char path[256];
      snprintf(path, sizeof(path), "%s/w%d_%s.ppm", out, w, s.name);
      save(path, px);
    }
  for (int h = 0; h < 2; h++)  // Zahnputz-Geschichten: Luna und Fips, je sieben Kapitel à vier Bilder
    for (int d = 0; d < 7; d++)
      for (int b = 0; b < 4; b++) {
        drawBrushStory(px, (Hero)h, d, b, 0.6f, 3);
        char path[256];
        snprintf(path, sizeof(path), "%s/s%d_%d_%d.ppm", out, h, d, b);
        save(path, px);
      }
  // Zahnputz-Spiel: die neun Abschnitte nach KAI, je mit Monstern beim Putzen und einmal ohne Putzen
  struct { MouthView v; int area; } secs[9] = {{M_CHEW, 0}, {M_CHEW, 1}, {M_CHEW, 2}, {M_CHEW, 3}, {M_OUTER, 0},
                                               {M_OUTER, 1}, {M_OUTER, 2}, {M_INNER, 0}, {M_INNER, 1}};
  for (int i = 0; i < 9; i++)
    for (int k = 0; k < 2; k++) {
      drawBrushGame(px, i % 2 ? FIPS : LUNA, secs[i].v, secs[i].area, k ? 1 : 3, !k, 3);
      char path[256];
      snprintf(path, sizeof(path), "%s/g%d_%d.ppm", out, i, k);
      save(path, px);
    }
  // Auswahl mit Kronen (7 Tage am Stück), Summ-Ballon, Album, Sticker groß
  drawBrushPick(px, 1, 3);
  save((std::string(out) + "/pick.ppm").c_str(), px);
  BalloonObj o[4] = {{30, 10, 0}, {44, 22, 1}, {52, 6, 0}, {18, 30, 0}};
  int bys[3] = {2, 10, 20};
  for (int k = 0; k < 3; k++) {
    drawBalloon(px, k == 1 ? FIPS : LUNA, bys[k], o, 4, k == 0 ? M_CHEW : k == 1 ? M_OUTER : M_INNER, k, k < 2, 5 + k * 9);
    save((std::string(out) + "/b" + std::to_string(k) + ".ppm").c_str(), px);
  }
  struct { PitchTarget t; int pos, res; bool hum; } pg4[4] = {{T_HIGH, 0, 0, false}, {T_HIGH, 1, 1, true}, {T_LOW, 2, 1, true}, {T_LOW, 0, 2, false}};
  for (int k = 0; k < 4; k++) {
    drawPitchGame(px, LUNA, pg4[k].t, pg4[k].pos, pg4[k].res, pg4[k].hum, M_OUTER, 1, 3 + k);
    save((std::string(out) + "/p" + std::to_string(k) + ".ppm").c_str(), px);
  }
  // Kipp-Spiele
  drawTiltBall(px, LUNA, M_CHEW, 0, 20.0f, 24.0f, 1, false, 2);
  save((std::string(out) + "/t0.ppm").c_str(), px);
  int tx, ty;
  tiltTarget(M_OUTER, 1, 2, tx, ty);
  drawTiltBall(px, FIPS, M_OUTER, 1, tx - 1.0f, ty + 0.0f, 2, true, 3);
  save((std::string(out) + "/t1.ppm").c_str(), px);
  BalloonObj fall[4] = {{12, 8, 0}, {30, 18, 1}, {44, 4, 0}, {24, 27, 0}};
  drawCatch(px, LUNA, 24.0f, fall, 4, M_INNER, 0, 4);
  save((std::string(out) + "/t2.ppm").c_str(), px);
  BalloonObj run[4] = {{30, 26, 0}, {44, 18, 1}, {52, 30, 1}, {20, 34, 0}};
  drawMeadowRun(px, LUNA, 18.0f, run, 4, false, M_CHEW, 2, 5);
  save((std::string(out) + "/t3.ppm").c_str(), px);
  drawMeadowRun(px, FIPS, 24.0f, run, 4, false, M_OUTER, 0, 6);
  save((std::string(out) + "/t4.ppm").c_str(), px);
  for (int pg = 0; pg < 3; pg++) {
    drawAlbum(px, 0x3FFFFFF, pg, 2);
    save((std::string(out) + "/a" + std::to_string(pg) + ".ppm").c_str(), px);
  }
  drawAlbum(px, 0x0000A53, 0, 2);
  save((std::string(out) + "/a3.ppm").c_str(), px);
  for (int id : {11, STICKER_TROPHY, STICKER_TEAM}) {
    drawSticker(px, id, 1);
    save((std::string(out) + "/st" + std::to_string(id) + ".ppm").c_str(), px);
  }
  return 0;
}
