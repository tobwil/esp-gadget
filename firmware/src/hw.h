// Hardware: Display + LVGL, Touch, Tasten, Akku (AXP2101), Bildschirmfotos und Fernsteuerung über USB.
#pragma once
#include <Arduino.h>
#include <lvgl.h>

enum BtnEvent : uint8_t {
  BTN_NONE,
  BOOT_SHORT,       // BOOT kurz gedrückt (beim Loslassen)
  BOOT_HOLD,        // BOOT wird gehalten (einmal nach 350 ms)
  BOOT_RELEASE,     // BOOT nach dem Halten losgelassen
  PWR_SHORT,        // PWR kurz
  PWR_LONG,         // PWR lang (≈ 1,5 s)
};

void hwBegin();
void hwLoop();                 // Tasten, Akku, serielle Befehle
BtnEvent hwButton();           // nächstes Tasten-Ereignis (oder BTN_NONE)

void hwBrightness(uint8_t pct, bool ignoreCap = false);  // ignoreCap: Taschenlampe
void hwBrightnessCap(uint8_t pct);  // Obergrenze (Sparmodus, Schlafenszeit); 100 = keine
void hwScreen(bool on);        // Display an/aus (Touch weckt es wieder)
bool hwScreenOn();
void hwActivity();             // Bedienung: Zeit bis zum Abschalten neu starten
uint32_t hwIdleMs();           // ms seit der letzten Bedienung
bool hwWakeRequested();        // Touch bei dunklem Display

int hwBattery();               // %, −1 = kein Akku
int hwBatteryMv();             // Akkuspannung in mV, 0 = unbekannt
bool hwUsb();
bool hwCharging();
void hwPowerOff();
void hwDeepSleep(uint32_t wakeInS);  // kehrt nicht zurück; 0 = nur Tasten/Touch wecken
bool hwWokeFromSleep();
