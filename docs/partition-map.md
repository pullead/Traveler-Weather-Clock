# Partition Map

Partition table offset: `0x8000`

| Label | Type | Subtype | Offset | Size | Role |
|---|---|---|---:|---:|---|
| `nvs` | data | nvs | `0x009000` | `0x5000` | Configuration storage |
| `otadata` | data | ota | `0x00E000` | `0x2000` | OTA boot state |
| `ota_0` | app | ota_0 | `0x010000` | `0x2C0000` | Main application |
| `uf2` | app | factory | `0x2D0000` | `0x40000` | UF2 maintenance/recovery image |
| `ffat` | data | fat | `0x310000` | `0xF0000` | Application file system |

## Data Partitions

### `nvs`

NVS commonly stores Wi-Fi configuration, application settings, calibration data, counters, and other key-value records. For this traveler weather clock, it may also contain selected location/city, time-zone settings, weather units, refresh intervals, or weather service configuration.

### `otadata`

OTA data stores boot selection state.

### `ffat`

The FAT/FFAT partition can contain files, logs, configuration files, cached content, weather icons/assets, cached weather payloads, city lists, or other application data.

## App Partitions

Application partitions contain the executable firmware images. This repository documents their metadata for development reference.
