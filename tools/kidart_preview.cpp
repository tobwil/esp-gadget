// Vorschau des Kinder-Zifferblatts am Rechner: rendert jede Welt mit allen Szenen und der Abenteuer-Karte als PPM.
//   sh tools/kidart_preview.sh   (baut, rendert und setzt die Bilder zu docs/screens/kinderwelten.png zusammen)
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
  return 0;
}
