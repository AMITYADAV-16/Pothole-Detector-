<div align="center">

# 🚧 ROADIE — IoT-Based Pothole Detection System

## 🔍 Overview

Road maintenance in India still leans heavily on manual surveys and citizen complaints, which means potholes are found late, repairs are scheduled poorly, and vehicles get damaged in between.

**ROADIE (PotholeDetect)** automates the first step of that pipeline. A small embedded unit mounted in a vehicle continuously reads vertical acceleration. When it sees the signature of a pothole hit, it grabs the current GPS position from the driver's phone, stores the event, and uploads it to a cloud database. Authorities then see every detected pothole, with severity and time, on a web map.

No dedicated GPS module and no camera are needed — the phone you already own supplies location, which keeps the build cheap and simple.

## ✨ Key Features

- **Real-time pothole detection** from Z-axis vibration using an MPU-6050 accelerometer
- **Speed-bump rejection** — upward-first spikes are classified separately and ignored
- **Adaptive baseline** — the resting Z-value is continuously re-estimated so mounting angle and gravity offset don't break detection
- **Severity levels** (`low` / `medium` / `high`) derived from peak g-force
- **Phone-based GPS** — the ESP32 reads NMEA sentences from a GPS-sharing app over Wi-Fi (no GPS module needed)
- **Offline-tolerant logging** — events are buffered to flash (LittleFS) and uploaded automatically once Wi-Fi is available
- **Cloud storage** on Supabase (PostgreSQL) via its REST API
- **Live web dashboard** with summary cards, interactive Leaflet/OpenStreetMap map, recent detections, history filters (All / Today / Week / Month), search and export
- **Low cost** — roughly ₹950 in core hardware (ESP32 ≈ ₹600, MPU-6050 ≈ ₹350) plus free-tier cloud services

## ⚙️ How It Works

1. **Sense** — the MPU-6050 is sampled over I²C; the ESP32 subtracts a rolling baseline from the Z-axis reading.
2. **Detect** — a threshold/state machine looks for a sharp *downward* spike followed by a short rebound. Slow, upward-first events (speed bumps) are discarded.
3. **Locate** — on a detection, the firmware polls the phone's GPS stream (`$GPGGA` sentences) and parses latitude/longitude.
4. **Store** — the event (`severity, g-force, lat, lon, millis`) is appended to `/potholes.csv` on the ESP32's flash.
5. **Upload** — every 10 s, if Wi-Fi is up, buffered rows are POSTed to Supabase; the file is deleted once everything is accepted.
6. **Visualise** — the React dashboard reads the `potholes` table and plots each event as a coloured marker.

## 🏗 System Architecture

| Layer | Component | Responsibility |
|---|---|---|
| Sensor | MPU-6050 | Vibration / acceleration sensing |
| Embedded | ESP32 (Arduino C++) | Detection logic, GPS parsing, buffering, upload |
| Location | Smartphone GPS | Latitude / longitude source |
| Cloud | Supabase (PostgreSQL) | Persistent pothole records |
| Dashboard | React + Vite + TypeScript + Leaflet | Map, history, reports |


## 🧰 Tech Stack

| Area | Technologies |
|---|---|
| Firmware | Arduino C++ (ESP32 Arduino core), `Wire`, `WiFi`, `WiFiClientSecure`, `HTTPClient`, `LittleFS` |
| Frontend | React, Vite, TypeScript / JavaScript, Tailwind CSS |
| Mapping | Leaflet + OpenStreetMap tiles |
| Backend / DB | Supabase (PostgreSQL + REST) |
| Tools | Arduino IDE, VS Code, Node.js, Git |
| Process | Agile / iterative development |

## 🔌 Hardware

| Component | Notes |
|---|---|
| ESP32 dev board (ESP32-WROOM-32) | Wi-Fi, dual-core, runs the firmware |
| MPU-6050 (GY-521) breakout | 3-axis accelerometer + gyroscope, I²C address `0x68` |
| Smartphone (Android / iOS) | GPS source, also provides the Wi-Fi hotspot |
| 5 V USB power supply / power bank | Powers the ESP32 in the vehicle |
| Breadboard + jumper wires | Prototype wiring |

### Wiring

| MPU-6050 | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |


## 🧠 Detection Algorithm

The firmware keeps a slowly-adapting baseline of the Z-axis (in g) and works on the deviation `az = rawZ − baseline`.

| Parameter | Value | Meaning |
|---|---|---|
| `START_THRESHOLD` | `0.55 g` | Deviation that opens an "event" |
| `END_THRESHOLD` | `0.25 g` | Deviation below which the event closes |
| `POTHOLE_MIN_G` | `0.70 g` | Minimum peak to count as a pothole |
| First-spike direction | `≤ −0.30 g` | Potholes start with a *downward* drop |
| Event duration | `< 250 ms` | Potholes are short, sharp impacts |
| `DETECTION_COOLDOWN` | `5 s` | Suppresses duplicate detections |
| `EVENT_TIMEOUT` | `800 ms` | Force-closes a stuck event |
| Speed-bump rule | first spike `> +0.20 g` and positive time `> 250 ms` | Logged as "speed bump — ignored" |

**Severity by peak g-force**

| Peak | Severity |
|---|---|
| `≥ 2.5 g` | 🔴 high |
| `≥ 2.0 g` | 🟠 medium |
| otherwise | 🟡 low |

> All thresholds are tuned empirically for the prototype's mounting position and test vehicle. Re-calibrate for your own setup.


## 🗄 Database Schema

Reference schema for the Supabase `potholes` table (based on the Table Editor view below):

```sql
create table public.potholes (
  id           bigint generated always as identity primary key,
  latitude     double precision,
  longitude    double precision,
  severity     text check (severity in ('low', 'medium', 'high')),
  detected_at  timestamptz not null default now(),
  g_force      double precision,
  device_id    text
);


The project also has a `devices` table for registered IoT units.

## 🚀 Getting Started

> ⚠️ **Never commit real credentials.** Keep Wi-Fi passwords and Supabase keys in git-ignored files (see below).

### Prerequisites

- Arduino IDE with the **ESP32 board package** installed
- Node.js 18+ and npm
- A free [Supabase](https://supabase.com) project
- An Android/iOS phone with a GPS-sharing app that exposes a **GPSD-style TCP server** (the prototype used *Share GPS*)

### 1. Set up Supabase

1. Create a project and run the SQL from [Database Schema](#-database-schema).
2. Enable Row Level Security and add a policy that allows **insert** for the `anon` role (used by the ESP32) and **select** for the dashboard.
3. Copy your **Project URL** and **anon public key**.

### 2. Flash the firmware

Create `firmware/secrets.h` (and add it to `.gitignore`):

```cpp
#define WIFI_SSID      "your-phone-hotspot"
#define WIFI_PASSWORD  "your-password"
#define SUPABASE_URL   "https://<project-ref>.supabase.co/rest/v1/potholes"
#define SUPABASE_KEY   "<your-anon-key>"
#define DEVICE_ID      "ESP32_ROADIE_01"

// Phone GPS (Share GPS app -> TCP / GPSD)
#define GPS_HOST       "192.168.x.x"   // phone IP shown in the app
#define GPS_PORT       2947
```

Then in the Arduino IDE:

1. Select board **ESP32 Dev Module**.
2. Open the firmware sketch and build/upload.
3. Open the Serial Monitor at **115200 baud** — you should see the baseline value, Wi-Fi connection, and `GPS: lat, lon` lines.

No third-party libraries are required; everything used ships with the ESP32 Arduino core.

### 3. Run the dashboard

```bash
cd dashboard
npm install
```

Create `dashboard/.env` (git-ignored) — adjust variable names to match your code:

```env
VITE_SUPABASE_URL=https://<project-ref>.supabase.co
VITE_SUPABASE_ANON_KEY=<your-anon-key>
```

```bash
npm run dev
# open http://localhost:5173
```

### 4. Test it

1. Turn on the phone hotspot and start GPS sharing.
2. Power the ESP32 (it joins the hotspot and connects to the phone's GPS server).
3. Tap or drop the sensor board / drive over a rough patch.
4. Watch `POTHOLE LOGGED → Lat … Lon … G …` in the serial monitor, then `✅ UPLOADED!`, and see the marker appear on the dashboard.

### Suggested repository layout

```.
├── firmware/            # ESP32 Arduino sketch (+ secrets.h, git-ignored)
├── dashboard/           # React + Vite + TypeScript web app
├── assets/              # README images
├── docs/                # Project report, UML diagrams
└── README.md
```

## 🖥 Dashboard Screenshots

**Dashboard — summary cards, live map and recent detections**
<img width="393" height="221" alt="image" src="https://github.com/user-attachments/assets/e443b1b7-1e1a-4023-88d0-0af544d467b4" />

**Detection history — filter by All / Today / Week / Month**

<img src="assets/dashboard-history.png" alt="ROADIE detection history page" width="900"/>

The sidebar provides **Dashboard**, **History**, **Reports** and **Settings** views. Markers are colour-coded by severity, and each detection shows time, location, g-force and device ID.


## ✅ Testing & Results

The system was tested at unit, integration, system and field level.

| Test | Expected | Observed | Result |
|---|---|---|---|
| Pothole detection accuracy | Potholes detected, speed bumps filtered | Potholes detected; speed bumps filtered | ✅ Pass |
| Mobile GPS accuracy | Within 5 m | Within ~3 m | ✅ Pass |
| Cloud transmission | Record stored in < 2 s | ~1.5 s, dashboard updated | ✅ Pass |
| Dashboard map display | Markers with correct details | Accurate markers and details | ✅ Pass |
| Power / stability | Stable long-run operation | 8+ hours continuous | ✅ Pass |

Project testing reported approximately **90 % detection accuracy** in field runs. This is a small student-scale evaluation (single vehicle, single device), so treat it as a prototype result rather than a benchmark.


## ⚠️ Known Limitations

- **Phone dependency** — GPS and the Wi-Fi hotspot both come from the driver's phone; if GPS has no fix, records can be saved with empty/zero coordinates (visible in the sample data).
- **Empirical thresholds** — tuned for one mounting position and one vehicle; different suspensions and speeds will need re-calibration.
- **Speed not used** — detection doesn't yet normalise by vehicle speed, so low-speed and high-speed impacts of the same pothole can differ.
- **TLS verification disabled** — the firmware uses `setInsecure()` on the HTTPS client for simplicity; pin the certificate before any real deployment.
- **Credentials in code** — move all secrets out of source (see [Getting Started](#-getting-started)) and restrict the anon key with Row Level Security.
- **Internet required** for cloud sync (events are buffered locally until then).


##  Acknowledgements

- Presented as a research paper, *"IoT Based Pothole Detection"*, at the **National Symposium on Sustainable Applications for Future Environment (NSSAFE-2025)**, AITR Indore, 8 Nov 2025.
- [Leaflet](https://leafletjs.com/) and [OpenStreetMap](https://www.openstreetmap.org/) contributors for mapping.
- [Supabase](https://supabase.com), [Vite](https://vitejs.dev/), [React](https://react.dev/), [Tailwind CSS](https://tailwindcss.com/), and the Espressif / Arduino communities.
- Our faculty guide and the Department of CSIT, AITR.
