#include "hearing.h"
#include <math.h>
#include <algorithm>

namespace hearing {

Result analyze(const int16_t* w, float& floor) {
  constexpr int LAG_MIN = 30, LAG_MAX = 229;  // 533 … 70 Hz
  Result res;
  static float x[N];
  float e = 0, s1 = 0, s2 = 0;
  for (int i = 0; i < N; i++) {
    float v = w[i] / 32768.0f;
    e += v * v;
    s1 += 0.3f * (v - s1);  // zwei einpolige Tiefpässe
    s2 += 0.3f * (s1 - s2);
    x[i] = s2;
  }
  res.rms = sqrtf(e / N);
  // Grundrauschen: fällt schnell; steigt nur mit leisen Werten zügig, während etwas zu hören ist kaum
  // (sonst „verschluckt“ es nach zwei Minuten Dauerputzen die Bürste – tests/host prüft das)
  bool loud = res.rms >= std::max(0.0015f, floor * 2.0f);
  floor += (res.rms - floor) * (res.rms < floor ? 0.3f : loud ? 0.0003f : 0.01f);
  if (!loud) return res;
  res.sound = true;
  static float r[LAG_MAX + 2];
  float best = 0;
  for (int lag = LAG_MIN; lag <= LAG_MAX + 1; lag++) {
    float s = 0, a = 0, b = 0;
    for (int i = 0; i + lag < N; i++) { s += x[i] * x[i + lag]; a += x[i] * x[i]; b += x[i + lag] * x[i + lag]; }
    r[lag] = s / (sqrtf(a * b) + 1e-9f);
    if (lag <= LAG_MAX) best = std::max(best, r[lag]);
  }
  res.periodic = best;
  if (best < 0.45f) return res;
  // erster Gipfel nahe am besten (sonst eine Oktave zu tief), dann zwischen zwei Abständen verfeinern
  int lag = LAG_MIN + 1;
  while (lag < LAG_MAX && !(r[lag] >= best * 0.88f && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1])) lag++;
  float d = r[lag - 1] - 2 * r[lag] + r[lag + 1];
  float off = d < 0 ? 0.5f * (r[lag - 1] - r[lag + 1]) / d : 0;
  res.voiced = true;
  res.pitch = 16000.0f / (lag + off);
  return res;
}

}  // namespace hearing
