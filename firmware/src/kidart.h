// Pixel-Szenen des Kinder-Zifferblatts: reines Zeichnen in ein Raster (ohne LVGL und Arduino), damit sich alle
// Welten, Tiere und Karten auch am Rechner als Vorschau rendern lassen (tools/kidart_preview.cpp).
//   Welten: Zuhause, Weltraum, Bauernhof, Unterwasser, Märchenland – gleicher Tagesablauf, eigene Kulisse und Kleidung
//   Tier-Begleiter: ist in jeder Szene dabei, freut sich über Sterne, bekommt mit genug Sternen Halstuch und Krone
//   Wochen-Abenteuer: in der Spielzeit eine Karte mit sieben Stationen, jeden Tag ein Kapitel weiter
#pragma once
#include <stdint.h>
#include "kidacts.h"

namespace kidart {

constexpr int S = 116;  // Raster in Pixeln; die Szene besteht aus 2 × 2-Blöcken (58 × 48 Blöcke)
constexpr uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F); }
uint16_t mix(uint16_t a, uint16_t b, int t);  // t = 0 … 255 Anteil von b

enum World : uint8_t { HOME, SPACE, FARM, SEA, FAIRY, WORLDS };
enum Pet : uint8_t { NO_PET, CAT, BUNNY, DOG, DRAGON, PETS };

struct Scene {
  kids::Act act;
  bool weekend;
  World world;
  Pet pet;
  uint8_t petLevel;   // 1 … 3 (Halstuch ab 2, Krone ab 3)
  bool petHappy;      // Herzchen
  bool adventure;     // Wochenkarte statt Spielplatz
  int day;            // 0 = Montag … 6 = Sonntag
  float progress;     // Fortschritt des heutigen Kapitels 0 … 1
  int frame;          // Animationsschritt
};

void draw(uint16_t* px, const Scene& s);  // füllt das ganze Raster (die Szene liegt oben, darunter schwarz)
bool showsAdventure(const Scene& s);      // zeigt diese Szene die Abenteuer-Karte?
const char* worldName(World w);
const char* petName(Pet p);
const char* chapter(World w, int day);    // Titel des Kapitels (Montag … Sonntag)

}  // namespace kidart
