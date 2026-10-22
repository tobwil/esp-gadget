// Waveshare ESP32-S3-Touch-AMOLED-1.75C (Schaltplan Rev. 2026)
#pragma once

// Display CO5300 (QSPI), 466 × 466
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET 1
#define LCD_TE 13
#define LCD_W 466
#define LCD_H 466

// I2C: Touch CST9217 (0x5A), AXP2101 (0x34), QMI8658 (0x6B), ES8311 (0x18), ES7210 (0x40)
#define I2C_SDA 15
#define I2C_SCL 14
#define TP_INT 11
#define TP_RST 2
#define IMU_INT1 21

// Audio: ES8311 (Lautsprecher) und ES7210 (2 Mikrofone) teilen sich einen I2S-Bus
#define I2S_MCLK 16
#define I2S_BCLK 9
#define I2S_WS 45
#define I2S_DOUT 8   // ESP → ES8311
#define I2S_DIN 10   // ES7210 → ESP
#define PA_EN 46     // Verstärker NS4150B

// Tasten: BOOT direkt am GPIO, PWR am AXP2101 (PWRON, wird per I2C abgefragt)
#define BTN_BOOT 0
#define PWR_SENSE 3  // SYS_OUT laut Schaltplan, nur zur Diagnose
