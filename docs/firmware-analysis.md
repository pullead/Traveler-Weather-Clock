# Firmware Analysis

This document records metadata extracted from the local 4 MB Flash image. The firmware is a network-connected traveler weather clock, and the analysis focuses on Flash layout, app image metadata, and product structure.

For product-level interpretation, see [product-analysis.md](product-analysis.md).

## Image Metadata

| Field | Value |
|---|---|
| Image size | `4,194,304` bytes |
| Flash size | 4 MB |
| SHA256 | `D97DAB3E126B6A1C24F0579A3F93E1B884DDE21BCD1A805EAC458892B8631C1D` |
| Partition table offset | `0x8000` |

## Application Images

### `ota_0`

| Field | Value |
|---|---|
| Offset | `0x010000` |
| Size | `0x2C0000` |
| Segment count | `6` |
| Entry address | `0x40375C84` |
| SPI mode | `0x02` |
| SPI speed/size byte | `0x2F` |
| Project name | `arduino-lib-builder` |
| Version | `43a8f6d` |
| Compile date | `Jun  2 2026` |
| Compile time | `11:17:54` |
| ESP-IDF version | `v5.5.4` |
| ELF SHA256 prefix | `8DDCE42D36E03F21` |

### `uf2`

| Field | Value |
|---|---|
| Offset | `0x2D0000` |
| Size | `0x40000` |
| Segment count | `5` |
| Entry address | `0x40374DE4` |
| SPI mode | `0x02` |
| SPI speed/size byte | `0x2F` |
| Project name | `tinyuf2` |
| Version | `0.35.0` |
| Compile date | `Jul  3 2025` |
| Compile time | `10:50:48` |
| ESP-IDF version | `v5.3.2` |
| ELF SHA256 prefix | `DAE6527530D5EF14` |

## Interpretation

The image uses an OTA-style layout with one main application partition (`ota_0`), a factory UF2-related image (`uf2`), and a FAT-style data partition (`ffat`).

For a traveler weather clock, the data partitions are likely used for connection settings, selected city or location, weather service configuration, time-zone settings, cached weather data, logs, or UI assets.

This analysis does not reconstruct source code. It documents firmware metadata and high-level product context useful for future ESP32-S3 development work.
