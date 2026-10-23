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

// Zähneputzen mit Pferd Luna oder Reh Fips
enum Hero : uint8_t { LUNA, FIPS };
const char* heroName(Hero h);
const char* brushChapter(Hero h, int day);         // Fortsetzungsgeschichte: Kapitel des Tages (0 = Montag)
const char* brushText(Hero h, int day, int beat);  // ein Satz je Bild (beat 0 … 3)
void drawBrushStory(uint16_t* px, Hero h, int day, int beat, float prog, int frame);  // Belohnung: Bild des Kapitels
void drawBrushPick(uint16_t* px, int frame, uint8_t crowns = 0);  // beide Figuren zur Auswahl; Bit 0/1: Krone für Luna/Fips
// Zahnputz-Spiel nach KAI (Kauflächen, Außenflächen, Innenflächen): der Mund in drei Ansichten, Zahnmonster auf dem
// Bereich, der gerade dran ist, und die Zahnbürste in der richtigen Bewegung. Gezeichnet wie im Spiegel.
//   Kauflächen: area 0 oben rechts, 1 oben links, 2 unten links, 3 unten rechts
//   außen:      area 0 rechts, 1 vorne, 2 links (Zähne aufeinander)
//   innen:      area 0 oben, 1 unten (von Rot nach Weiß)
enum MouthView : uint8_t { M_CHEW, M_OUTER, M_INNER };
void drawBrushGame(uint16_t* px, Hero h, MouthView v, int area, int germs, bool brushing, int frame);

// Summ-Ballon: Ballon (y = obere Kante in Blöcken, 1 … 20) mit Luna bzw. Fips, Sterne zum Einsammeln, kleiner Mund unten
struct BalloonObj { int8_t x, y; uint8_t kind; };  // kind 0 Stern, 1 Regenbogenstern (3 Punkte)
constexpr int BALLOON_X = 4, BALLOON_W = 14, BALLOON_H = 18;  // Ballon links, in Blöcken
void drawBalloon(uint16_t* px, Hero h, int balloonY, const BalloonObj* objs, int n, MouthView v, int area, bool humming, int frame);

// Sammelalbum: 24 Sticker zum Sammeln, dazu Pokal (7 Tage am Stück) und Team-Herzen (zu zweit geputzt)
constexpr int STICKERS = 26, STICKER_TROPHY = 24, STICKER_TEAM = 25;
const char* stickerName(int id);
void drawSticker(uint16_t* px, int id, int frame);                 // ein Sticker groß
void drawAlbum(uint16_t* px, uint32_t have, int page, int frame);  // 12 je Seite, fehlende als „?“

}  // namespace kidart
