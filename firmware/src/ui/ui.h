// Oberfläche: Seiten-Stapel, Farben und Bausteine – für den runden 466er und den eckigen 480er Bildschirm.
#pragma once
#include <lvgl.h>
#include "../fonts/fonts.h"
#include "../hw.h"
#include "pins.h"

namespace ui {

// ------------------------------------------------------------------ Farben (AMOLED: Schwarz = aus)
inline lv_color_t C_BG() { return lv_color_hex(0x000000); }
inline lv_color_t C_SURF() { return lv_color_hex(0x17171B); }
inline lv_color_t C_SURF2() { return lv_color_hex(0x26262C); }
inline lv_color_t C_TEXT() { return lv_color_hex(0xF4F1EA); }
inline lv_color_t C_DIM() { return lv_color_hex(0x8E8A82); }
inline lv_color_t C_BRASS() { return lv_color_hex(0xE0B25E); }   // Taschenuhr
inline lv_color_t C_MUSIC() { return lv_color_hex(0xFF5F57); }
inline lv_color_t C_RADIO() { return lv_color_hex(0x3DDC97); }
inline lv_color_t C_TIMER() { return lv_color_hex(0xFFB547); }
inline lv_color_t C_WATCH() { return lv_color_hex(0x63B3FF); }
inline lv_color_t C_ALARM() { return lv_color_hex(0xB592FF); }
inline lv_color_t C_SET() { return lv_color_hex(0xA7A7B0); }
inline lv_color_t C_RED() { return lv_color_hex(0xFF453A); }

static const int W = LCD_W, H = LCD_H, CX = LCD_W / 2, CY = LCD_H / 2;
static const bool SQUARE = BOARD_SQUARE;  // eckiges Display: Listen über die volle Breite, Infos in den Ecken

// ------------------------------------------------------------------ Seiten
struct Page {
  void (*build)(lv_obj_t* scr);
  void (*update)();             // ca. 10 × pro Sekunde, solange die Seite oben liegt
  void (*leave)();              // beim Verlassen (zurück)
  void (*resume)();             // wieder oben (nach „zurück“ von einer Unterseite)
  bool watchFace;               // Startseite (eigene Gesten)
  uint8_t enter;                // Einflug: 0 = von rechts, 1 = von unten (Apps), 2 = von oben (Kontrollzentrum)
};
enum : uint8_t { ENTER_RIGHT = 0, ENTER_BOTTOM = 1, ENTER_TOP = 2 };

void begin();
void tick();                    // aus der Hauptschleife
void push(const Page* p);
void back();
void home();
const Page* top();
lv_obj_t* topScreen();
void show(const Page* p);      // zur Seite zurück, falls sie schon offen ist, sonst öffnen
void onButton(BtnEvent e);
void wake();                    // Display ging an

// ------------------------------------------------------------------ Bausteine
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "");
lv_obj_t* title(lv_obj_t* scr, const char* text, lv_color_t color);  // oben mittig
lv_obj_t* roundButton(lv_obj_t* parent, const char* icon, int size, lv_color_t bg, lv_color_t fg,
                      const lv_font_t* font = &font_icon_40);
lv_obj_t* pill(lv_obj_t* parent, const char* text, lv_color_t bg, lv_color_t fg);

// Liste, die sich der Rundung anpasst (Zeilen werden zum Rand hin schmaler)
lv_obj_t* roundList(lv_obj_t* scr, int top = 96, int bottom = 70);
lv_obj_t* listRow(lv_obj_t* list, const char* icon, lv_color_t iconColor, const char* text, const char* sub = nullptr);
void listRowSub(lv_obj_t* row, const char* sub);
lv_obj_t* listRowSwitch(lv_obj_t* row, bool on);
void listFit(lv_obj_t* list);   // nach dem Füllen aufrufen

// Hinweise und Dialoge (oberste Ebene)
void toast(const char* text, uint32_t ms = 2500);
bool funkDim();                    // Display nur vom Funk geweckt, noch nicht angefasst: gedimmt, bald aus
void keyboard(const char* title, const char* initial, bool password, void (*done)(const char* text));
void powerDialog();
// Uhr finden (aus der Weboberfläche): 30 s klingeln mit „Hier bin ich!“, Antippen oder BOOT beendet
void findMe(bool on, int volume = 70);

// Lautstärke: großer Regler (interactive) bzw. nur Anzeige beim Drehen am Rand
void volumeShow(bool interactive);
void volumeSet(int pct);
// „Am Rand drehen“ = Lautstärke: Finger am Bildschirmrand entlang ziehen (im Uhrzeigersinn lauter).
// when: nur aktiv, wenn die Funktion true liefert (nullptr = immer)
void bezel(lv_obj_t* obj, bool (*when)() = nullptr);
bool bezelMoved();              // letzter Druck hat am Rand gedreht (dann kein „Antippen“ auslösen)

// Kurzbefehle
inline void hide(lv_obj_t* o, bool h) {
  if (h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
void play(uint8_t fx);          // Klick-Klang (wenn eingeschaltet)
void fmtTime(char* out, size_t n, uint32_t ms, bool tenths);  // mm:ss(.d) bzw. h:mm:ss

}  // namespace ui

// Seiten (in den einzelnen Dateien)
extern const ui::Page PAGE_WATCH, PAGE_APPS, PAGE_MUSIC, PAGE_BROWSE, PAGE_TIMER, PAGE_STOPWATCH, PAGE_ALARM,
    PAGE_RADIO, PAGE_SETTINGS, PAGE_FACES, PAGE_TILES, PAGE_CONTROL, PAGE_GAME, PAGE_ASSIST, PAGE_ROUTINE, PAGE_BRUSH;
void kidFaceBuild(lv_obj_t* scr);  // Zifferblatt im Kindermodus (ui_kidface.cpp)
void kidFaceUpdate();
void angryBuild(lv_obj_t* scr);    // Wutmodus: das Wut-Glas (ui_angry.cpp)
extern float angryTestShake;       // Test: so viele Sekunden Schütteln vortäuschen
extern int kidFaceTestMin;          // Testzeit für Bildschirmfotos (−1 = echte Uhrzeit)
void gameTestLevel(int level);     // Balance: Stufe direkt zeigen (Bildschirmfotos)
void gameRecords(int& lvl, uint32_t& ms);  // Balance-Rekorde: höchste Stufe, schnellste Zeit „Alle Kugeln“ (0 = keine)
void gameResetRecords();
struct BrushStats { int lunaChapter, fipsChapter, streak, best, balloon; uint32_t have; };  // Zähneputzen (ui_brush.cpp)
BrushStats brushStats();
void browseOpen(const char* path);  // Musik-Liste öffnen (Pfad der Brücke)
void tilesOpen(int idx);            // Kacheln ab Kachel idx (0 Musik, 1 Wetter, 2 Timer, 3 Funk)
uint32_t radioTarget();             // gewählter Empfänger auf der Funk-Seite (0 = alle)
