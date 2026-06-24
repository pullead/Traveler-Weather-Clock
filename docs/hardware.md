# Hardware Notes

## Board

- Model: Adafruit Feather ESP32-S3 TFT
- PlatformIO board ID: `adafruit_feather_esp32s3_tft`
- MCU: ESP32-S3
- CPU: dual-core Xtensa LX7, up to 240 MHz
- Wireless: 2.4 GHz Wi‑Fi and Bluetooth LE
- Flash: 4 MB
- PSRAM: 2 MB
- Display: onboard 1.14 inch IPS TFT, 240 × 135, ST7789
- USB: native USB-C, USB-Serial/JTAG capable
- Battery: LiPo connector and onboard charger
- Expansion: Feather headers and STEMMA QT / Qwiic I2C

## Common Development Interfaces

| Function | Arduino name | GPIO |
|---|---:|---:|
| I2C SDA | `SDA` | GPIO42 |
| I2C SCL | `SCL` | GPIO41 |
| SPI MOSI | `MOSI` | GPIO35 |
| SPI MISO | `MISO` | GPIO37 |
| SPI SCK | `SCK` | GPIO36 |
| SPI CS / TFT CS | `SS` / `TFT_CS` | GPIO7 |
| UART TX | `TX` | GPIO1 |
| UART RX | `RX` | GPIO2 |
| A0 | `A0` | GPIO18 |
| A1 | `A1` | GPIO17 |
| A2 | `A2` | GPIO16 |
| A3 | `A3` | GPIO15 |
| A4 | `A4` | GPIO14 |
| A5 | `A5` | GPIO8 |
| Built-in red LED | `LED_BUILTIN` | GPIO13 |
| NeoPixel data | `PIN_NEOPIXEL` | GPIO33 |
| NeoPixel power | `NEOPIXEL_POWER` | GPIO34 |
| TFT/I2C power | `TFT_I2C_POWER` | GPIO21 |
| TFT DC | `TFT_DC` | GPIO39 |
| TFT RST | `TFT_RST` | GPIO40 |
| TFT backlight | `TFT_BACKLITE` | GPIO45 |
| Boot button | `BOOT` | GPIO0 |

## Notes

- The ESP32-S3 does not provide classic ESP32 DAC outputs.
- `TFT_I2C_POWER` must be enabled before using the onboard TFT and some I2C peripherals.
- During flashing or ROM bootloader operations, the board may enumerate on a different COM port than normal runtime.
