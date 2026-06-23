# ESP32-S3 Traveler Weather Clock

An ESP32-S3 based traveler weather clock for the Adafruit Feather ESP32-S3 TFT board.

The device combines Wi-Fi connectivity, network time, weather information, and a compact TFT dashboard. It is designed as a portable clock for travel scenarios, where the display can show time, date, weather status, temperature, connection state, and other glanceable information.

## Hardware

- Board: Adafruit Feather ESP32-S3 TFT
- PlatformIO board ID: `adafruit_feather_esp32s3_tft`
- MCU: ESP32-S3
- Flash: 4 MB
- PSRAM: 2 MB
- Display: 1.14 inch 240 × 135 ST7789 TFT
- USB: USB-Serial/JTAG over USB-C
- Power: USB-C or LiPo battery

## Project Structure

```text
.
├── README.md
├── LICENSE
├── docs/
│   ├── hardware.md
│   ├── product-analysis.md
│   ├── firmware-analysis.md
│   ├── partition-map.md
│   └── backup-and-restore.md
├── tools/
│   └── analyze_flash_metadata.py
└── tests/
    └── test_analyze_flash_metadata.py
```

## Product Overview

This firmware behaves as a network-connected traveler weather clock.

Main product areas:

- Time display: current time, date, and likely travel/time-zone related information.
- Weather display: weather condition, temperature, and update status.
- TFT interface: compact dashboard layout on the onboard 240 × 135 display.
- Wi-Fi connectivity: network access for time and weather updates.
- Local configuration: stored settings for connectivity, display preferences, and weather-clock behavior.
- Maintenance path: a UF2-related image is present, suggesting a recovery or update workflow.

See [docs/product-analysis.md](docs/product-analysis.md) for the product-level analysis.

## Firmware Metadata

The local Flash image has a 4 MB layout with one main app image, one UF2-related image, and data partitions for runtime state and file storage.

| Partition | Type | Offset | Size | Role |
|---|---|---:|---:|---|
| `nvs` | data/nvs | `0x009000` | `0x5000` | Configuration storage |
| `otadata` | data/ota | `0x00E000` | `0x2000` | OTA boot state |
| `ota_0` | app/ota_0 | `0x010000` | `0x2C0000` | Main application |
| `uf2` | app/factory | `0x2D0000` | `0x40000` | UF2 maintenance/recovery image |
| `ffat` | data/fat | `0x310000` | `0xF0000` | Application file system |

Application image summary:

| Image | Project | Version | Build | ESP-IDF |
|---|---|---|---|---|
| `ota_0` | `arduino-lib-builder` | `43a8f6d` | `Jun 2 2026 11:17:54` | `v5.5.4` |
| `uf2` | `tinyuf2` | `0.35.0` | `Jul 3 2025 10:50:48` | `v5.3.2` |

More details are in [docs/firmware-analysis.md](docs/firmware-analysis.md) and [docs/partition-map.md](docs/partition-map.md).

## Metadata Analysis Tool

The repository includes a small Python utility for reading ESP32 Flash image metadata.

Markdown output:

```powershell
python tools\analyze_flash_metadata.py path\to\flash-backup.bin --format markdown
```

JSON output:

```powershell
python tools\analyze_flash_metadata.py path\to\flash-backup.bin
```

## Tests

```powershell
python -m unittest discover -s tests -v
```

## Development Ideas

Possible next steps for rebuilding or extending the traveler weather clock:

- TFT clock dashboard
- weather condition screen
- travel city or time-zone switching
- Wi-Fi setup flow
- battery indicator
- low-power display behavior
- UF2 or serial maintenance workflow
