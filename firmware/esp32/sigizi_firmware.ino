// sigizi_firmware.ino
// SIGIZI - Stunting Detection System for ESP32
// Integrates: 2x VL53L1X (ToF), 4x load cell + HX711, RFID RC522, LCD 20x4,
// MicroSD, WiFi/MQTT

#include "config.h"
#include <ArduinoJson.h>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <MFRC522.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <VL53L1X.h>
#include <WiFi.h>
#include <Wire.h>


// ==================== Global Objects ====================
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
MFRC522 rfid(RFID_CS_PIN, RFID_RST_PIN);
HX711 scale;
VL53L1X tof1;
VL53L1X tof2;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ==================== Global Variables ====================
float weight_kg = 0.0;
float length_cm = 0.0;
String toddler_id = "";
bool measurement_in_progress = false;
unsigned long measurement_start_time = 0;

// Moving average buffers
float weight_buffer[MOVING_AVG_WINDOW];
float length_buffer[MOVING_AVG_WINDOW];
int buffer_index = 0;
int buffer_count = 0;

// File system
File dataFile;
String currentFilename = "";

// ==================== Function Prototypes ====================
void connectWiFi();
void connectMQTT();
void sendDataToServer(String id, float weight, float length);
void saveToSD(String id, float weight, float length);
void syncOfflineData();
void printLCD(int row, const char *message);
void initToFSensors();
float readWeight();
float readLength();
void applyMovingAverage(float new_weight, float new_length);
void resetMeasurement();

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[SIGIZI] Starting...");

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  printLCD(0, "SIGIZI v1.0");
  printLCD(1, "Initializing...");
  delay(1000);

  // Load Cell (HX711)
  scale.begin(LOADCELL_DT, LOADCELL_SCK);
  scale.set_scale(); // Set calibration factor later
  scale.tare();      // Zero offset
  Serial.println("[LOADCELL] Calibrated and tared.");

  // RFID
  SPI.begin();
  rfid.PCD_Init();
  rfid.PCD_SetAntennaGain(MFRC522::RxGain_max);
  Serial.println("[RFID] Initialized.");

  // ToF Sensors
  initToFSensors();
  Serial.println("[ToF] Sensors ready.");

  // MicroSD Card
  if (SD_ENABLED) {
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println("[SD] Card mount failed!");
      printLCD(3, "SD Card Error!");
    } else {
      Serial.println("[SD] Card mounted.");
      syncOfflineData(); // Send any unsynced data
    }
  }

  // WiFi & MQTT
  connectWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();

  printLCD(1, "Ready");
  printLCD(2, "Tap RFID card");
  printLCD(3, "");
}

// ==================== Main Loop ====================
void loop() {
  // Maintain MQTT connection
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
  mqttClient.loop();

  // Check for RFID tag
  if (!measurement_in_progress && rfid.PICC_IsNewCardPresent() &&
      rfid.PICC_ReadCardSerial()) {
    // Read UID
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uid += String(rfid.uid.uidByte[i], HEX);
    }
    toddler_id = uid;
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    Serial.print("[RFID] Tag detected: ");
    Serial.println(toddler_id);
    printLCD(1, "ID: " + toddler_id.substring(0, 8));
    printLCD(2, "Measuring...");
    measurement_in_progress = true;
    measurement_start_time = millis();
    buffer_index = 0;
    buffer_count = 0;
  }

  // Perform measurement if active
  if (measurement_in_progress) {
    // Check timeout
    if (millis() - measurement_start_time > MEASUREMENT_TIMEOUT_MS) {
      Serial.println("[ERROR] Measurement timeout!");
      printLCD(2, "Timeout! Retry");
      resetMeasurement();
      delay(2000);
      printLCD(2, "Tap RFID card");
      return;
    }

    // Acquire data at SAMPLE_FREQUENCY_HZ
    static unsigned long last_sample_time = 0;
    unsigned long now = millis();
    if (now - last_sample_time >= (1000 / SAMPLE_FREQUENCY_HZ)) {
      last_sample_time = now;

      float w = readWeight();
      float l = readLength();

      if (w > 0 && l > 0) {
        applyMovingAverage(w, l);
      }

      // Display live readings
      char buf[20];
      snprintf(buf, 20, "W:%.1fkg L:%.1fcm", weight_kg, length_cm);
      printLCD(3, buf);
    }

    // After enough samples, finalize measurement
    if (buffer_count >= MOVING_AVG_WINDOW) {
      finalizeMeasurement();
    }
  }

  delay(10);
}

// ==================== Measurement Finalization ====================
void finalizeMeasurement() {
  // Compute final filtered values
  float sum_w = 0, sum_l = 0;
  for (int i = 0; i < MOVING_AVG_WINDOW; i++) {
    sum_w += weight_buffer[i];
    sum_l += length_buffer[i];
  }
  weight_kg = sum_w / MOVING_AVG_WINDOW;
  length_cm = sum_l / MOVING_AVG_WINDOW;

  // Round to 1 decimal
  weight_kg = round(weight_kg * 10) / 10.0;
  length_cm = round(length_cm * 10) / 10.0;

  Serial.printf("[MEAS] Final -> Weight: %.1f kg, Length: %.1f cm\n", weight_kg,
                length_cm);

  // Display result
  lcd.clear();
  printLCD(0, "Measurement Done");
  printLCD(1, "ID: " + toddler_id.substring(0, 8));
  printLCD(2, String(weight_kg, 1) + " kg  " + String(length_cm, 1) + " cm");
  printLCD(3, "Sending data...");

  // Send to server (if WiFi connected) or save to SD
  if (WiFi.status() == WL_CONNECTED) {
    sendDataToServer(toddler_id, weight_kg, length_cm);
  } else if (SD_ENABLED) {
    saveToSD(toddler_id, weight_kg, length_cm);
    printLCD(3, "Saved to SD (offline)");
  } else {
    printLCD(3, "No connection & no SD!");
  }

  delay(2000);
  resetMeasurement();
  printLCD(1, "Ready");
  printLCD(2, "Tap RFID card");
  printLCD(3, "");
}

void resetMeasurement() {
  measurement_in_progress = false;
  toddler_id = "";
  weight_kg = 0;
  length_cm = 0;
  buffer_index = 0;
  buffer_count = 0;
}

// ==================== Sensor Reading Functions ====================
float readWeight() {
  if (scale.is_ready()) {
    long reading = scale.read();
    // Convert to kg based on calibration factor (to be adjusted)
    // Assuming calibration factor = 1000 (example)
    float kg = reading / 1000.0;
    return kg;
  }
  return -1;
}

float readLength() {
  // Read both ToF sensors and take average (if both valid)
  if (tof1.timeoutOccurred() || tof2.timeoutOccurred()) {
    return -1;
  }
  uint16_t dist1 = tof1.read();
  uint16_t dist2 = tof2.read();
  if (dist1 > 0 && dist2 > 0 && dist1 < 4000 && dist2 < 4000) {
    // Assuming length = (distance from head + distance from foot) / 2? Actually
    // we want body length. In SIGIZI design, two ToF measure from fixed ends;
    // total length = distance between sensors - (d1 + d2)? Simplified: average
    // both (or sum of both subtracted from fixed length) For now, use average
    // as placeholder. User must adapt according to mechanical setup.
    float avg = (dist1 + dist2) / 2.0;
    return avg / 10.0; // convert mm to cm
  }
  return -1;
}

void applyMovingAverage(float new_weight, float new_length) {
  weight_buffer[buffer_index] = new_weight;
  length_buffer[buffer_index] = new_length;
  buffer_index = (buffer_index + 1) % MOVING_AVG_WINDOW;
  if (buffer_count < MOVING_AVG_WINDOW)
    buffer_count++;
}

// ==================== ToF Initialization (two sensors with different I2C
// addresses) ====================
void initToFSensors() {
  // Set XSHUT pins as outputs and pull low to disable both sensors initially
  pinMode(TOF1_XSHUT_PIN, OUTPUT);
  pinMode(TOF2_XSHUT_PIN, OUTPUT);
  digitalWrite(TOF1_XSHUT_PIN, LOW);
  digitalWrite(TOF2_XSHUT_PIN, LOW);
  delay(10);

  // Enable first sensor, set its address to 0x30
  digitalWrite(TOF1_XSHUT_PIN, HIGH);
  delay(10);
  tof1.init();
  tof1.setAddress(0x30);
  tof1.startContinuous(50);

  // Enable second sensor, set its address to 0x31
  digitalWrite(TOF2_XSHUT_PIN, HIGH);
  delay(10);
  tof2.init();
  tof2.setAddress(0x31);
  tof2.startContinuous(50);

  Serial.println("[ToF] Both sensors active with addresses 0x30 and 0x31");
}

// ==================== WiFi & MQTT ====================
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
    lcd.clear();
    printLCD(0, "WiFi OK");
    printLCD(1, WiFi.localIP().toString());
    delay(1500);
  } else {
    Serial.println("\n[WiFi] Connection failed! Running offline.");
    lcd.clear();
    printLCD(0, "WiFi Failed");
    printLCD(1, "Offline mode");
    delay(1500);
  }
}

void connectMQTT() {
  while (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    if (mqttClient.connect("SIGIZI_ESP32")) {
      Serial.println("[MQTT] Connected to broker.");
      mqttClient.subscribe("sigizi/command");
    } else {
      Serial.print("[MQTT] Failed, rc=");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Handle incoming commands if needed (e.g., sync request)
  Serial.print("[MQTT] Message arrived: ");
  Serial.println(topic);
}

// ==================== Data Transmission ====================
void sendDataToServer(String id, float weight, float length) {
  // Send via MQTT (JSON)
  StaticJsonDocument<200> doc;
  doc["id"] = id;
  doc["weight_kg"] = weight;
  doc["length_cm"] = length;
  doc["timestamp"] = millis();
  char buffer[200];
  serializeJson(doc, buffer);
  if (mqttClient.publish(MQTT_TOPIC, buffer)) {
    Serial.println("[MQTT] Data sent successfully.");
    printLCD(3, "Data sent!");
  } else {
    Serial.println("[MQTT] Failed to send data.");
    // Fallback: save to SD
    if (SD_ENABLED)
      saveToSD(id, weight, length);
    printLCD(3, "Send failed, saved");
  }
}

// ==================== SD Card Operations ====================
void saveToSD(String id, float weight, float length) {
  if (!SD_ENABLED)
    return;
  // Generate filename with date (simplified: use counter)
  if (currentFilename == "") {
    int fileCount = 0;
    while (SD.exists(FILENAME_PREFIX + String(fileCount) + ".csv"))
      fileCount++;
    currentFilename = FILENAME_PREFIX + String(fileCount) + ".csv";
  }
  dataFile = SD.open(currentFilename, FILE_APPEND);
  if (dataFile) {
    dataFile.print(id);
    dataFile.print(",");
    dataFile.print(weight);
    dataFile.print(",");
    dataFile.println(length);
    dataFile.close();
    Serial.println("[SD] Data saved.");
  } else {
    Serial.println("[SD] Error opening file.");
  }
}

void syncOfflineData() {
  File root = SD.open("/");
  if (!root)
    return;
  File file = root.openNextFile();
  while (file) {
    if (String(file.name()).startsWith(FILENAME_PREFIX)) {
      Serial.print("[SYNC] Sending file: ");
      Serial.println(file.name());
      // Read each line and send via MQTT
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
          // Parse CSV line: id,weight,length
          int firstComma = line.indexOf(',');
          int secondComma = line.indexOf(',', firstComma + 1);
          String id = line.substring(0, firstComma);
          float w = line.substring(firstComma + 1, secondComma).toFloat();
          float l = line.substring(secondComma + 1).toFloat();
          sendDataToServer(id, w, l);
          delay(100); // avoid flooding
        }
      }
      file.close();
      // Optionally delete file after sync
      SD.remove(file.name());
    }
    file = root.openNextFile();
  }
  root.close();
  Serial.println("[SYNC] Offline sync completed.");
}

// ==================== LCD Helper ====================
void printLCD(int row, const char *message) {
  lcd.setCursor(0, row);
  lcd.print("                    "); // clear line
  lcd.setCursor(0, row);
  lcd.print(message);
}