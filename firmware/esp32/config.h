// config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== WiFi Configuration ====================
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ==================== Server Configuration ====================
// Ganti dengan IP address server FastAPI / Flask Anda
const char *SERVER_HOST = "192.168.1.100";
const int SERVER_PORT = 8000;
const char *MQTT_BROKER = "192.168.1.100";
const int MQTT_PORT = 1883;
const char *MQTT_TOPIC = "sigizi/measurement";

// ==================== Pin Definitions ====================
// RFID RC522 (SPI)
#define RFID_CS_PIN 5
#define RFID_RST_PIN 26 // Optional

// MicroSD Card (SPI)
#define SD_CS_PIN 15

// HX711 Load Cell
#define LOADCELL_DT 16
#define LOADCELL_SCK 4

// LCD I2C
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 20
#define LCD_ROWS 4

// ToF Sensors (VL53L1X)
#define TOF1_XSHUT_PIN 32
#define TOF2_XSHUT_PIN 33

// I2C pins for ToF (default SDA=21, SCL=22 on ESP32 DevKit)
#define I2C_SDA 21
#define I2C_SCL 22

// Miscellaneous
#define LED_BUILTIN 2

// ==================== Measurement Parameters ====================
#define MEASUREMENT_TIMEOUT_MS 30000 // 30 seconds max per toddler
#define SAMPLE_FREQUENCY_HZ 10       // 10 Hz sampling
#define MOVING_AVG_WINDOW 5          // window size for moving average filter

// ==================== Offline Storage ====================
#define SD_ENABLED true
#define FILENAME_PREFIX "/data_"

#endif