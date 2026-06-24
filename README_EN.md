# Traveler Weather Clock 2.0

<p align="center">
  <a href="./README.md">简体中文</a> ·
  <a href="./README_EN.md">English</a> ·
  <a href="./README_JA.md">日本語</a> ·
  <a href="./docs/archive/README_v1.md">Previous README</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-2.0-2f80ed?style=for-the-badge" alt="Version 2.0">
  <img src="https://img.shields.io/badge/ESP32--S3-Weather%20Clock-00bcd4?style=for-the-badge" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/Mobile-Web%20Admin-46b36f?style=for-the-badge" alt="Mobile Web Admin">
  <img src="https://img.shields.io/badge/License-MIT-f5b642?style=for-the-badge" alt="MIT License">
</p>

<p align="center">
  <img src="./docs/assets/startup-screen.png" alt="Traveler Weather Clock 2.0 startup screen" width="100%">
</p>

<p align="center">
  <strong>An ESP32-S3 anime-style weather clock with a traveling cyclist, real weather, day/night scenes, and a loyal little dog.</strong>
</p>

---

## Demo Video

<p align="center">
  <a href="https://pullead.github.io/Traveler-Weather-Clock/demo.html">
    <img src="./docs/assets/demo-cover.jpg" alt="Traveler Weather Clock demo video cover" width="100%">
  </a>
</p>

<p align="center">
  <a href="https://pullead.github.io/Traveler-Weather-Clock/demo.html">▶ Open the web video player</a>
  ·
  <a href="https://github.com/pullead/Traveler-Weather-Clock/raw/refs/heads/main/docs/assets/demo.mp4">Fallback download/playback</a>
</p>

---

## Overview

Traveler Weather Clock 2.0 is a custom ESP32-S3 TFT firmware that turns weather data into a living 2D side-scrolling travel scene. A traveler rides a bicycle across a moving dirt road while a fluffy white Bichon dog runs happily behind. The sky changes with real sunrise, sunset, moon phase, cloud cover, precipitation and local weather. At midnight, the traveler rests beside a tent and campfire while the dog sleeps nearby.

The default location is Asago City, Hyogo Prefecture, Japan. Time, weather, holidays, precipitation probability and astronomy-related visuals are driven by live online data.

---

## Highlights in 2.0

| Area | Features |
| --- | --- |
| Animated travel scene | Side-scrolling cyclist, moving dirt road, layered background, changing grass, day/night ambience |
| Weather effects | Sunny, cloudy, overcast, rain, snow, sleet, thunderstorm, typhoon, wind, fog, haze and more |
| Astronomy visuals | Sunrise/sunset path, sun height, moon phase, moon trajectory and moon phase tilt |
| Status bar | Time, date, weather label/icon, temperature range, feels-like icon, Wi-Fi, Japanese holiday, solar path, precipitation chart, moon phase and current temperature |
| Dog companion | White Bichon runs during the day and guards the tent at night |
| BOOT preview mode | Single-click for random weather/day-night scene combinations, double-click to restore live weather |
| Multi-Wi-Fi memory | Previously connected home/office networks are remembered and automatically reused |
| Mobile web admin | Manage scenes, character settings, dog settings, status modules, Wi-Fi and system options from your phone |
| Custom startup screen | A polished Traveler Weather Clock 2.0 boot image |

---

## Mobile Web Admin

<p align="center">
  <img src="./docs/assets/mobile-admin-ui.png" alt="Traveler Weather Clock mobile web admin UI" width="100%">
</p>

When your phone and the ESP32 are on the same Wi-Fi network, open:

- `http://travel-clock.local`
- or the board IP shown on the display/serial monitor, for example `http://192.168.0.100`

The web admin includes:

- Overview: startup image, current weather, 24-hour precipitation probability, sunrise/sunset, moon phase and device status
- Forecast: current weather, 24-hour forecast, 7-day forecast, sun path and moon phase
- Character settings: traveler outfit, dog color, dog speed, dog night brightness, animation speed and scene preview
- Status modules: enable/disable Wi-Fi, holiday, sun path, precipitation, feels-like temperature, moon phase and more
- System settings: brightness, theme color, animation speed, night mode and reboot
- Wi-Fi management: add, delete and clear remembered networks

Settings are stored in ESP32 NVS/Preferences and automatically restored after reboot.

---

## Data Sources

- Default location: Asago City, Hyogo Prefecture, Japan
- Time zone: Asia/Tokyo / JST
- Weather: Open-Meteo
- Japanese holidays: holidays-jp
- Time sync: NTP
- Sun/moon visuals: firmware astronomy calculations + network time

---

## Hardware

The 2.0 firmware is currently tuned for:

- Adafruit Feather ESP32-S3 TFT
- 240 × 135 ST7789 TFT display
- ESP32-S3 Wi-Fi
- BOOT button

Other ESP32-S3 + TFT boards can be supported by adjusting the display driver, pins and layout resolution.

---

## Quick Start

### Build

```bash
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_tft \
  firmware/TravelWeatherClockV2
```

### Upload

```bash
arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_tft \
  --port /dev/cu.usbmodem1101 \
  firmware/TravelWeatherClockV2
```

Replace `/dev/cu.usbmodem1101` with your actual serial port.

### First Wi-Fi Setup

On first boot, or after clearing Wi-Fi, the board enters Wi-Fi setup mode. Follow the screen instructions or open:

```text
http://192.168.4.1
```

After a network is saved, the board remembers it. When moving between home and office, it will automatically connect to a known available Wi-Fi network.

---

## BOOT Button

| Action | Result |
| --- | --- |
| Single click | Random weather + day/night + animation scene preview |
| Double click | Restore the current live weather and real time |
| Long press about 4 seconds | Clear saved Wi-Fi configuration |

---

## Project Structure

```text
weather-micro-station/
├── README.md
├── README_EN.md
├── README_JA.md
├── docs/
│   ├── assets/
│   │   ├── startup-screen.png
│   │   ├── mobile-admin-ui.png
│   │   ├── demo-cover.jpg
│   │   └── demo.mp4
│   ├── demo.html
│   ├── index.html
│   └── archive/
│       └── README_v1.md
├── firmware/
│   └── TravelWeatherClockV2/
├── include/
├── src/
└── tools/
```

---

## Previous Version

The original README is preserved here: [docs/archive/README_v1.md](./docs/archive/README_v1.md).

Git history also keeps the old project description. For future stable releases, GitHub Releases/Tags such as `v1.x` and `v2.0.0` are recommended.

---

## Multilingual README Notes

GitHub does not allow custom JavaScript in README files, so a true in-place language switcher is not possible. This project uses stable language links at the top of each README:

- [简体中文](./README.md)
- [English](./README_EN.md)
- [日本語](./README_JA.md)

Each language page includes the images and playable demo video link.

---

## Credits

This project started from the weather-clock idea in sfrechette/weather-micro-station and evolved into a deeply customized ESP32-S3 Chinese/Japanese travel-themed firmware with real-time weather animation and a mobile web admin.

Weather and holiday data are provided by Open-Meteo and holidays-jp.

---

## License

MIT License. See [LICENSE](./LICENSE).
