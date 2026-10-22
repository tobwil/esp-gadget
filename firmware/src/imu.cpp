#include "imu.h"
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include "config.h"
#include "pins.h"

namespace imu {

static SensorQMI8658 qmi;
static bool ok = false;

bool begin() {
  ok = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (ok) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    qmi.disableAccelerometer();  // erst im Spiel einschalten
  }
  Serial.printf("Bewegungssensor: %s\n", ok ? "ok" : "nicht gefunden");
  return ok;
}

void sleep(bool on) {
  if (!ok) return;
  if (on) qmi.disableAccelerometer();
  else qmi.enableAccelerometer();
}

bool gravity(float& gx, float& gy) {
  float x, y, z;
  if (!ok || !qmi.getAccelerometer(x, y, z)) return false;
  // Achsen des Sensors auf dem 1.75C (siehe Waveshare-Beispiel): +x zeigt zur 12, +y zur 9.
  // Liegt die Uhr schräg, misst die nach oben zeigende Achse +g – die Kugel rollt in die Gegenrichtung.
  gx = IMU_SIGN_X * y;
  gy = IMU_SIGN_Y * x;
  return true;
}

}  // namespace imu
