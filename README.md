# ESP32 AV Controller 🎛️

**A friendly, web-based AV discovery and terminal tool for an ESP32.**

---

## What is this? 💡

ESP32 AV Controller is a small, useful project that turns an ESP32 into a networked tool for discovering AV devices, capturing simple protocol traffic, and providing a web-based terminal and control UI. It uses LittleFS to host a lightweight web UI on the device and exposes a JSON API and WebSocket endpoints for realtime interaction.

---

## Features ✅

- Web UI served from LittleFS (see `data/`) with websockets for live logs and terminal
- Device discovery (subnet scan, SSDP), capture of traffic, and capture management
- Built-in terminal for connecting to network devices (ASCII/HEX modes)
- OTA firmware and filesystem updates via `/update`
- WiFi configurability (AP / STA / AP+STA) and an easy access AP SSID: `ESP32-AV-Tool`
- mDNS name: `esp32-av-tool.local` (when mDNS is available)

---

## Quick Start (Windows / PlatformIO) ⚙️

Prerequisites:
- PlatformIO (VS Code extension or `pio` CLI)
- USB connection to an ESP32 development board (board: `esp32dev`)

Build and upload firmware:

```bash
# build
platformio run

# upload firmware to device
platformio run --target upload

# upload filesystem (web UI from `data/` to LittleFS)
platformio run --target uploadfs

# open serial monitor (115200)
platformio device monitor -b 115200
# or
platformio run --target monitor
```

Notes:
- The project uses LittleFS (`board_build.filesystem = littlefs`). If `LittleFS.begin()` prints "LittleFS mount failed", make sure you uploaded the filesystem with `uploadfs`.
- The configured serial monitor baud rate is **115200**.

---

## How to use the web UI 🖥️

- After boot, visit http://esp32-av-tool.local/ (mDNS) or http://<device-ip>/
- The UI provides:
  - Live logs and terminal (via websockets)
  - WiFi scan/config UI (AP/STA modes)
  - Discovery / scanning features
  - Capture browsing and pinning
  - OTA update form at `/update`

API endpoints (selected):
- `GET /api/health` – device status and uptime
- `GET /api/wifi` / `POST /api/wifi` – view/change WiFi settings
- `GET /api/wifi/scan` – get visible SSIDs
- `POST /api/discovery/start` – start discovery
- `GET /api/discovery/results` – read discovery results
- `GET /api/captures` – list captured traffic
- WebSocket endpoints: `/ws` (logs), `/term` (terminal), `/wsproxy`, `/wsdisc`

---

## Project Layout 🔧

- `src/` — C++ sources (networking, discovery, capture proxy, Web API, WiFi helper)
- `include/` — headers and small notes
- `data/` — web UI files (served from LittleFS): `index.html`, `app.js`, `style.css`
- `platformio.ini` — PlatformIO configuration (board: `esp32dev`, `littlefs`, library deps)

Key libs used (auto-installed by PlatformIO):
- `ESPAsyncWebServer-esphome`
- `ArduinoJson` (v7)
- `ESP32Ping`

---

## Development Notes & Tips 🛠️

- Config and runtime state are stored in non-volatile Preferences (NVS). Device config (devices list, templates) is stored as JSON in preferences via `ConfigManager`.
- WiFi defaults to an AP SSID of `ESP32-AV-Tool` when not set and enforces a minimum AP password length.
- OTA supports both firmware and filesystem updates via the web form.
- For debugging, use the serial logs (115200) and watch the Web UI live logs.

---

## Troubleshooting ⚠️

- LittleFS mount failure: ensure `uploadfs` was run and that `board_build.filesystem` is set to `littlefs`.
- Can't reach the web UI: check serial logs for WiFi mode and IP. If using mDNS, try `http://<device-ip>/`.
- WiFi not connecting: check saved SSID/password via `/api/wifi` and use the UI to set credentials if needed.

---

## Contributing & Contact 🤝

All contributions are welcome! Open an issue or a pull request with a short description of changes.
If this repository is hosted on a platform (GitHub/GitLab), please file issues there for questions or bugs.

---

## License

No license file is included in the repository. Add a `LICENSE` file (for example: MIT) to make the license explicit.

---

Thanks for checking out this project — enjoy exploring AV gear with an ESP32! 🎉
