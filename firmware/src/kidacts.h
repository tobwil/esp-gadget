// Abschnitte des Kinder-Tagesablaufs (ohne Arduino, damit auch die Vorschau am Rechner sie kennt)
#pragma once
#include <stdint.h>

namespace kids {

enum Act : uint8_t { SLEEP, WAKE, BREAKFAST, DAYCARE, LUNCH, NAP, PLAY, DINNER, BATH, STORY, ACTS };

}  // namespace kids
