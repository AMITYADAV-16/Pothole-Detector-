#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "LittleFS.h"

/* ================= USER CONFIG ================= */
#define WIFI_SSID     "Redmi 9 Prime"
#define WIFI_PASSWORD "1112223334"

#define SUPABASE_URL "https://uznkreqyaxwjjkvxhswm.supabase.co/rest/v1/potholes"
#define SUPABASE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InV6bmtyZXF5YXh3amprdnhoc3dtIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzgzMzUzNTcsImV4cCI6MjA5MzkxMTM1N30.DfzGzXjbDaRY8wO4HwmGybZaDZS-W2TIQF1J_oIYfho"

#define DEVICE_ID "ESP32_ROADIE_01"

/* ================= PHONE GPS (Share GPS - TCP/IP GPSD) ================= */
// UPDATE THESE WITH YOUR PHONE'S IP AND PORT FROM SHARE GPS APP
const char* GPS_HOST = "100.94.43.123";  // Common phone hotspot IP - check your phone's WiFi settings
const int GPS_PORT = 2947;               // Port shown in Share GPS app

WiFiClient gpsClient;
float currentLat = 0.0;
float currentLon = 0.0;
bool gpsConnected = false;

/* ================= MPU6050 ================= */
#define MPU_ADDR 0x68
#define SDA_PIN  21
#define SCL_PIN  22

/* ================= TUNING ================= */
#define START_THRESHOLD    0.55
#define END_THRESHOLD      0.25
#define POTHOLE_MIN_G      0.70
#define SPEEDBUMP_MIN_G    0.35
#define EVENT_TIMEOUT      800
#define DETECTION_COOLDOWN 5000
#define REBOUND_LOCKOUT    1000

/* ================= GLOBALS ================= */
float zBaseline = 0.0;
unsigned long lastDetection    = 0;
unsigned long lastPotholeTime  = 0;

/* ================= MPU READ ================= */
float readMPUZ() {
  int16_t az;
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3F);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 2, true);
  az = Wire.read() << 8 | Wire.read();
  return az / 16384.0;
}

/* ================= NMEA PARSER ================= */
float parseNMEALat(String nmea) {
  int i = 0;
  for (int c = 0; c < 2; c++) i = nmea.indexOf(',', i) + 1;
  int j = nmea.indexOf(',', i);
  String latStr = nmea.substring(i, j);
  String ns = nmea.substring(j+1, j+2);

  if (latStr.length() < 4) return 0.0;

  float deg = latStr.substring(0,2).toFloat();
  float min = latStr.substring(2).toFloat();
  float lat = deg + min/60.0;
  if (ns == "S") lat = -lat;
  return lat;
}

float parseNMEALon(String nmea) {
  int i = 0;
  for (int c = 0; c < 4; c++) i = nmea.indexOf(',', i) + 1;
  int j = nmea.indexOf(',', i);
  String lonStr = nmea.substring(i, j);
  String ew = nmea.substring(j+1, j+2);

  if (lonStr.length() < 5) return 0.0;

  float deg = lonStr.substring(0,3).toFloat();
  float min = lonStr.substring(3).toFloat();
  float lon = deg + min/60.0;
  if (ew == "W") lon = -lon;
  return lon;
}

/* ================= GPS FUNCTIONS ================= */
void connectGPS() {
  if (gpsClient.connected()) return;

  Serial.print("Connecting to GPS server...");
  if (gpsClient.connect(GPS_HOST, GPS_PORT)) {
    Serial.println("OK!");
    gpsClient.println("?WATCH={\"enable\":true,\"nmea\":true}");
    gpsConnected = true;
  } else {
    Serial.println("Failed - will retry");
    gpsConnected = false;
  }
}

bool readPhoneGPS() {
  connectGPS();

  if (!gpsClient.available()) return false;

  String line = gpsClient.readStringUntil('\n');
  line.trim();

  if (line.startsWith("$GPGGA")) {
    currentLat = parseNMEALat(line);
    currentLon = parseNMEALon(line);

    if (currentLat != 0.0 && currentLon != 0.0) {
      Serial.print("GPS: ");
      Serial.print(currentLat, 6);
      Serial.print(", ");
      Serial.println(currentLon, 6);
      return true;
    }
  }
  return false;
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS FAILED");
    while (1);
  }

  zBaseline = readMPUZ();
  Serial.print("Initial baseline: ");
  Serial.println(zBaseline, 4);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Connect to phone GPS
  connectGPS();

  Serial.println("ROADIE booted ✅");
}

/* ================= LOOP ================= */
void loop() {
  static bool          eventActive  = false;
  static unsigned long eventStart   = 0;
  static float         firstSpike   = 0;
  static float         peakAz       = 0;
  static unsigned long positiveTime = 0;
  static unsigned long lastBaselineUpdate = 0;

  // Read GPS every loop
  readPhoneGPS();

  float rawZ = readMPUZ();

  if (!eventActive && abs(rawZ - zBaseline) < 0.15) {
    zBaseline = 0.98 * zBaseline + 0.02 * rawZ;
    if (millis() - lastBaselineUpdate > 5000) {
      lastBaselineUpdate = millis();
      Serial.print("Baseline: ");
      Serial.println(zBaseline, 4);
    }
  }

  float az  = rawZ - zBaseline;
  unsigned long now = millis();

  if (!eventActive && abs(az) >= START_THRESHOLD) {
    eventActive  = true;
    eventStart   = now;
    firstSpike   = az;
    peakAz       = abs(az);
    positiveTime = 0;
  }

  if (eventActive) {
    peakAz = max(peakAz, abs(az));
    if (az > 0.1) positiveTime += 2;
  }

  if (eventActive && abs(az) < END_THRESHOLD) {
    unsigned long duration = now - eventStart;
    eventActive = false;

    if (firstSpike <= -0.30 &&
        duration < 250 &&
        peakAz >= POTHOLE_MIN_G &&
        now - lastDetection > DETECTION_COOLDOWN) {
      lastDetection   = now;
      lastPotholeTime = now;
      logPothole(peakAz);
    }
    else if (firstSpike > 0.20 &&
             positiveTime > 250 &&
             now - lastPotholeTime > REBOUND_LOCKOUT) {
      Serial.println("Speed bump — ignored");
    }

    peakAz       = 0;
    positiveTime = 0;
  }

  if (eventActive && (now - eventStart) > EVENT_TIMEOUT) {
    eventActive = false;

    if (firstSpike <= -0.30 &&
        peakAz >= POTHOLE_MIN_G &&
        now - lastDetection > DETECTION_COOLDOWN) {
      lastDetection   = now;
      lastPotholeTime = now;
      logPothole(peakAz);
    }

    peakAz       = 0;
    positiveTime = 0;
  }

  if (WiFi.status() == WL_CONNECTED) {
    uploadStoredData();
  }

  delayMicroseconds(2000);
}

/* ================= LOG ================= */
void logPothole(float gforce) {
  // Force GPS read before logging
  for (int i = 0; i < 5; i++) {
    readPhoneGPS();
    delay(50);
  }

  String severity =
    (gforce >= 2.5) ? "high"   :
    (gforce >= 2.0) ? "medium" : "low";

  float lat = (currentLat != 0.0) ? currentLat : 0.0;
  float lon = (currentLon != 0.0) ? currentLon : 0.0;

  // ALWAYS 5 fields
  String record = severity;
  record += "," + String(gforce, 2);
  record += "," + String(lat, 6);
  record += "," + String(lon, 6);
  record += "," + String(millis());

  Serial.print("CSV: ");
  Serial.println(record);

  File f = LittleFS.open("/potholes.csv", FILE_APPEND);
  if (f) {
    f.println(record);
    f.close();
  }

  Serial.print("POTHOLE LOGGED → ");
  Serial.print("Lat: "); Serial.print(lat, 6);
  Serial.print(" Lon: "); Serial.print(lon, 6);
  Serial.print(" G: "); Serial.println(gforce, 2);
}

/* ================= UPLOAD ================= */
void uploadStoredData() {
  static unsigned long lastUpload = 0;
  if (millis() - lastUpload < 10000) return;
  lastUpload = millis();

  File f = LittleFS.open("/potholes.csv", FILE_READ);
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() < 5) continue;
    if (!sendToSupabase(line)) {
      f.close();
      return;
    }
  }

  f.close();
  LittleFS.remove("/potholes.csv");
  Serial.println("All data uploaded ✅");
}

/* ================= SUPABASE ================= */
/* ================= SUPABASE ================= */
bool sendToSupabase(String csv) {
  if (WiFi.status() != WL_CONNECTED) return false;

  int i1 = csv.indexOf(',');
  int i2 = csv.indexOf(',', i1 + 1);
  int i3 = csv.indexOf(',', i2 + 1);
  int i4 = csv.indexOf(',', i3 + 1);

  if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0) {
    Serial.println("❌ Bad CSV: " + csv);
    return true; // Skip bad line
  }

  String severity = csv.substring(0, i1);
  float  gforce   = csv.substring(i1 + 1, i2).toFloat();
  String latStr   = csv.substring(i2 + 1, i3);
  String lonStr   = csv.substring(i3 + 1, i4);

  severity.trim();
  severity.toLowerCase();

  String payload = "{";
  payload += "\"latitude\":" + latStr + ",";
  payload += "\"longitude\":" + lonStr + ",";
  payload += "\"severity\":\"" + severity + "\",";
  payload += "\"g_force\":" + String(gforce, 2) + ",";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\"";
  payload += "}";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.begin(client, SUPABASE_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");

  int code = http.POST(payload);
  String response = http.getString();
  http.end();

  Serial.print("📥 SUPABASE RESPONSE: ");
  Serial.println(code);
  
  if (code != 201 && code != 204) {
    Serial.println("❌ ERROR: " + response);
    return false;
  }
  
  Serial.println("✅ UPLOADED!");
  return true;
}