#include "clips.h"
#include <mp3dec.h>
#include <freertos/idf_additions.h>
#include "audio.h"

// eingebettet über board_build.embed_files (platformio.ini)
extern const uint8_t voiceStart[] asm("_binary_brushvoice_bin_start");
extern const uint8_t voiceEnd[] asm("_binary_brushvoice_bin_end");

namespace clips {

struct Item { char id[20]; uint32_t gen; };
static QueueHandle_t queue = nullptr;
static volatile bool playing = false;
static volatile uint32_t gen = 0;  // stop() zählt hoch: ältere Stücke werden verworfen, neuere bleiben

// Eintrag im Inhaltsverzeichnis: id[20], Versatz, Länge (je 4 Byte, Little Endian)
static bool find(const char* id, uint32_t* off, uint32_t* len) {
  if (voiceEnd - voiceStart < 4 || memcmp(voiceStart, "BV1", 3)) return false;
  int n = voiceStart[3];
  for (int i = 0; i < n; i++) {
    const uint8_t* e = voiceStart + 4 + i * 28;
    if (strncmp((const char*)e, id, 20)) continue;
    memcpy(off, e + 20, 4);
    memcpy(len, e + 24, 4);
    return *off + *len <= (uint32_t)(voiceEnd - voiceStart);
  }
  return false;
}

static void task(void*) {
  HMP3Decoder dec = nullptr;
  int16_t* pcm = (int16_t*)heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);  // nicht in den internen RAM
  if (!pcm) { vTaskDelete(nullptr); return; }
  Item it;
  for (;;) {
    playing = uxQueueMessagesWaiting(queue) > 0;
    if (xQueueReceive(queue, &it, portMAX_DELAY) != pdTRUE) continue;
    playing = true;
    uint32_t off, len;
    if (it.gen != gen || !find(it.id, &off, &len)) continue;
    dec = MP3InitDecoder();  // frisch für jedes Stück: kein Rest vom vorigen im Überlappungspuffer
    if (!dec) continue;
    auto stale = [&] { return it.gen != gen; };
    unsigned char* p = (unsigned char*)voiceStart + off;
    int left = len;
    while (left > 0 && !stale()) {
      int sync = MP3FindSyncWord(p, left);
      if (sync < 0) break;
      p += sync;
      left -= sync;
      if (MP3Decode(dec, &p, &left, pcm, 0)) { p++; left--; continue; }
      MP3FrameInfo fi;
      MP3GetLastFrameInfo(dec, &fi);
      int n = fi.outputSamps / std::max(1, fi.nChans);
      if (fi.nChans == 2)
        for (int i = 0; i < n; i++) pcm[i] = (pcm[2 * i] + pcm[2 * i + 1]) / 2;
      while (audio::voiceSpace() < (size_t)n && !stale()) vTaskDelay(pdMS_TO_TICKS(10));
      if (!stale()) audio::voiceWrite(pcm, n);  // 16 kHz mono, wie der Sprachkanal
    }
    while (audio::voiceUsed() && !stale()) vTaskDelay(pdMS_TO_TICKS(20));  // ausklingen lassen
    MP3FreeDecoder(dec);  // den Speicher nicht dauerhaft belegen (interner RAM ist knapp)
    dec = nullptr;
    if (!stale()) vTaskDelay(pdMS_TO_TICKS(250));  // kurze Pause zwischen zwei Stücken
  }
}

bool has(const char* id) {
  uint32_t o, l;
  return find(id, &o, &l);
}

bool play(const char* id) {
  if (!has(id)) return false;
  if (!queue) {
    queue = xQueueCreate(12, sizeof(Item));
    // Stapel im PSRAM: die Aufgabe bleibt bestehen, soll aber keinen internen RAM festhalten
    xTaskCreatePinnedToCoreWithCaps(task, "clips", 10240, nullptr, 4, nullptr, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  Item it{};
  strncpy(it.id, id, sizeof(it.id) - 1);
  it.gen = gen;
  playing = true;
  return xQueueSend(queue, &it, 0) == pdTRUE;
}

void stop() {
  if (!queue) return;
  gen = gen + 1;
  xQueueReset(queue);
  audio::voiceFlush();
}

bool busy() { return queue && (playing || uxQueueMessagesWaiting(queue)); }

}  // namespace clips
