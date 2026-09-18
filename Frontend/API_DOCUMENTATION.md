# ROADIE Pothole Detection System - API Documentation

## Overview

This document describes how to integrate your ESP32 backend with the ROADIE Pothole Detection System.

## Database Structure

The system uses Supabase as the backend database with two main tables:

### Potholes Table

Stores all pothole detection data.

**Table Name:** `potholes`

| Column | Type | Description | Constraints |
|--------|------|-------------|-------------|
| id | uuid | Unique identifier | Primary key, auto-generated |
| latitude | numeric | GPS latitude | Required, -90 to 90 |
| longitude | numeric | GPS longitude | Required, -180 to 180 |
| severity | text | Severity level | Required, one of: 'low', 'medium', 'high' |
| g_force | numeric | Accelerometer reading in G | Required, >= 0 |
| device_id | text | ESP32 device identifier | Required |
| detected_at | timestamptz | Detection timestamp | Required, defaults to now() |
| created_at | timestamptz | Record creation time | Auto-generated |
| address | text | Optional address | Optional |
| notes | text | Additional notes | Optional |

### Devices Table

Tracks registered ESP32 devices.

**Table Name:** `devices`

| Column | Type | Description | Constraints |
|--------|------|-------------|-------------|
| id | uuid | Unique identifier | Primary key, auto-generated |
| device_id | text | Human-readable device ID | Required, unique |
| name | text | Device name/label | Required |
| status | text | Device status | Required, one of: 'active', 'inactive', 'maintenance' |
| last_seen | timestamptz | Last communication time | Auto-updated |
| created_at | timestamptz | Registration time | Auto-generated |

## API Endpoints

### Base URL

```
https://[YOUR_SUPABASE_URL]/rest/v1/
```

### Authentication

All requests must include the following headers:

```
apikey: YOUR_SUPABASE_ANON_KEY
Content-Type: application/json
```

## ESP32 Integration

### 1. Register Device (One-time setup)

**Endpoint:** `POST /devices`

**Request Body:**
```json
{
  "device_id": "ESP32-001",
  "name": "Vehicle Unit 1",
  "status": "active"
}
```

**Response:** `201 Created`
```json
{
  "id": "uuid",
  "device_id": "ESP32-001",
  "name": "Vehicle Unit 1",
  "status": "active",
  "last_seen": "2024-01-15T10:30:00Z",
  "created_at": "2024-01-15T10:30:00Z"
}
```

### 2. Submit Pothole Detection

**Endpoint:** `POST /potholes`

**Request Body:**
```json
{
  "latitude": 40.7128,
  "longitude": -74.0060,
  "severity": "high",
  "g_force": 2.8,
  "device_id": "ESP32-001",
  "detected_at": "2024-01-15T10:30:00Z"
}
```

**Severity Calculation Logic:**
- `g_force >= 2.5` → "high"
- `g_force >= 1.8` → "medium"
- `g_force < 1.8` → "low"

**Response:** `201 Created`
```json
{
  "id": "uuid",
  "latitude": 40.7128,
  "longitude": -74.0060,
  "severity": "high",
  "g_force": 2.8,
  "device_id": "ESP32-001",
  "detected_at": "2024-01-15T10:30:00Z",
  "created_at": "2024-01-15T10:30:00Z",
  "address": null,
  "notes": null
}
```

### 3. Update Device Status (Heartbeat)

**Endpoint:** `PATCH /devices?device_id=eq.ESP32-001`

**Request Body:**
```json
{
  "last_seen": "2024-01-15T10:30:00Z",
  "status": "active"
}
```

**Response:** `200 OK`

## ESP32 Arduino Code Example

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MPU6050.h>
#include <TinyGPS++.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* supabaseUrl = "YOUR_SUPABASE_URL";
const char* supabaseKey = "YOUR_SUPABASE_ANON_KEY";
const char* deviceId = "ESP32-001";

MPU6050 mpu;
TinyGPSPlus gps;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  // Initialize MPU6050
  mpu.initialize();

  // Register device
  registerDevice();
}

void loop() {
  // Read accelerometer data
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  // Convert to G-force
  float gForce = sqrt(pow(ax/16384.0, 2) + pow(ay/16384.0, 2) + pow(az/16384.0, 2));

  // Detect pothole (threshold: 1.5G)
  if (gForce > 1.5) {
    // Get GPS coordinates
    float lat = gps.location.lat();
    float lng = gps.location.lng();

    // Determine severity
    String severity = "low";
    if (gForce >= 2.5) severity = "high";
    else if (gForce >= 1.8) severity = "medium";

    // Send to API
    sendPotholeData(lat, lng, severity, gForce);

    delay(2000); // Debounce
  }

  delay(100);
}

void registerDevice() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(supabaseUrl) + "/rest/v1/devices";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Prefer", "return=representation");

    StaticJsonDocument<200> doc;
    doc["device_id"] = deviceId;
    doc["name"] = "Vehicle Unit 1";
    doc["status"] = "active";

    String jsonBody;
    serializeJson(doc, jsonBody);

    int httpCode = http.POST(jsonBody);

    if (httpCode > 0) {
      Serial.printf("Device registered: %d\n", httpCode);
    }

    http.end();
  }
}

void sendPotholeData(float lat, float lng, String severity, float gForce) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(supabaseUrl) + "/rest/v1/potholes";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Prefer", "return=representation");

    StaticJsonDocument<300> doc;
    doc["latitude"] = lat;
    doc["longitude"] = lng;
    doc["severity"] = severity;
    doc["g_force"] = gForce;
    doc["device_id"] = deviceId;
    doc["detected_at"] = getISOTimestamp();

    String jsonBody;
    serializeJson(doc, jsonBody);

    int httpCode = http.POST(jsonBody);

    if (httpCode > 0) {
      Serial.printf("Pothole sent: %d\n", httpCode);
      String response = http.getString();
      Serial.println(response);
    } else {
      Serial.printf("Error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

String getISOTimestamp() {
  // Implement NTP time sync and return ISO8601 timestamp
  // Example: "2024-01-15T10:30:00Z"
  return "2024-01-15T10:30:00Z";
}
```

## Real-time Updates

The frontend automatically subscribes to real-time updates using Supabase Realtime. When your ESP32 sends new pothole data, it will appear instantly on the dashboard without manual refresh.

## Testing

You can test your API integration using curl:

```bash
# Submit a test pothole detection
curl -X POST 'https://YOUR_SUPABASE_URL/rest/v1/potholes' \
  -H "apikey: YOUR_SUPABASE_ANON_KEY" \
  -H "Content-Type: application/json" \
  -H "Prefer: return=representation" \
  -d '{
    "latitude": 40.7128,
    "longitude": -74.0060,
    "severity": "high",
    "g_force": 2.8,
    "device_id": "ESP32-001",
    "detected_at": "2024-01-15T10:30:00.000Z"
  }'
```

## Error Handling

### Common HTTP Status Codes

- `200 OK` - Request successful
- `201 Created` - Resource created successfully
- `400 Bad Request` - Invalid data format
- `401 Unauthorized` - Invalid API key
- `409 Conflict` - Duplicate device_id
- `500 Internal Server Error` - Server error

### Example Error Response

```json
{
  "code": "23505",
  "details": "Key (device_id)=(ESP32-001) already exists.",
  "hint": null,
  "message": "duplicate key value violates unique constraint \"devices_device_id_key\""
}
```

## Best Practices

1. **Batch Requests**: If detecting multiple potholes quickly, consider batching requests
2. **Error Retry**: Implement exponential backoff for failed requests
3. **Connection Management**: Reuse HTTP connections when possible
4. **Timestamp Sync**: Use NTP to ensure accurate timestamps
5. **Power Management**: Send heartbeat every 5 minutes to conserve power
6. **Data Validation**: Validate GPS coordinates before sending
7. **Debouncing**: Wait 2-3 seconds between detections to avoid duplicates

## Support

For issues or questions, check the system settings page for configuration details.
