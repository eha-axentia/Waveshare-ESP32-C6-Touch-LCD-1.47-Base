# Waveshare ESP32-C6-Touch-LCD-1.47 — PlatformIO Base Project

A minimal Arduino/PlatformIO starting point for the
[Waveshare ESP32-C6-Touch-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47)
development board.

![Board running a clock demo on its 1.47" IPS display](https://openelab.io/cdn/shop/files/esp32-c6-touch-lcd-1-2_60b74dd8-f0e3-434e-abe6-ac6f407dd8f3.webp)

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-C6FH8, 32-bit RISC-V, 160 MHz, 8 MB flash |
| Wireless | Wi-Fi 6 (802.11ax), Bluetooth 5 LE, Zigbee/Thread (802.15.4) |
| Display | 1.47" IPS LCD, 172×320, ST7789 / JD9853 driver, SPI |
| Touch | AXS5106L capacitive controller, I2C (address `0x3B`) |
| IMU | QMI8658A 6-axis (accel + gyro), I2C (address `0x6A`) |
| SD card | TF/microSD slot, SPI (shared bus with LCD) |
| USB | USB Type-C |

> Pin assignments are sourced from the official board schematic
> (`ESP32-C6-Touch-LCD-1.47-Schematic.pdf`).

### Pin assignments

All constants live in [`include/board_pins.h`](include/board_pins.h).

![Top-down view of the PCB back — all GPIO header labels visible](https://openelab.io/cdn/shop/files/esp32-c6-touch-lcd-1-1_1226aa02-6714-4353-95d3-5d1f2bcaeeaa.webp)

![PCB back — USB-C port, SD card slot, and GPIO headers](https://openelab.io/cdn/shop/files/esp32-c6-touch-lcd-1_de4e2078-2065-4217-8c14-9595f3905f81.webp)

#### LCD (SPI)

| Signal | GPIO |
|--------|------|
| SCLK | 1 |
| MOSI | 2 |
| CS | 14 |
| DC | 15 |
| RST | 22 |
| Backlight | 23 |

#### Touch — AXS5106L (I2C)

Touch and IMU **share the same I2C bus** (GPIO 18/19, different addresses).
Initialise `Wire` once; both devices are accessible on the same bus.

| Signal | GPIO |
|--------|------|
| SDA | 18 (shared with IMU) |
| SCL | 19 (shared with IMU) |
| RST | 20 |
| INT | 21 |

> **Note:** GPIO 18/19 are also the ESP32-C6 hardware USB D−/D+ lines.
> Because this board routes them to I2C, the built-in USB-CDC peripheral
> cannot be used. See [USB / serial section](#usb--serial) below.

#### IMU — QMI8658A (I2C, 6-axis accel + gyro)

SA0 is tied to GND on this board, fixing the I2C address at `0x6A`.

| Signal | GPIO |
|--------|------|
| SDA | 18 (shared with Touch) |
| SCL | 19 (shared with Touch) |
| INT1 | 5 |
| INT2 | 6 |

#### SD card (SPI, shared bus with LCD)

| Signal | GPIO |
|--------|------|
| MOSI | 2 (shared) |
| MISO | 3 |
| SCLK | 1 (shared) |
| CS | 4 |

#### UART

| Signal | GPIO |
|--------|------|
| TX | 16 |
| RX | 17 |

#### Miscellaneous

| Peripheral | GPIO |
|------------|------|
| BOOT button | 9 (active-low) |
| Free / header | 7, 8, 12, 13 |

---

## Software

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB driver for the onboard USB-UART bridge (CH340 / CP2102)

### Libraries (installed automatically by PlatformIO)

| Library | Purpose |
|---------|---------|
| [`moononournation/GFX Library for Arduino`](https://github.com/moononournation/Arduino_GFX) | ST7789 / JD9853 display driver |
| [`lewisxhe/SensorLib`](https://github.com/lewisxhe/SensorLib) | QMI8658A IMU driver |

### Build & flash

```bash
# Build
pio run

# Flash
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor
```

Or use the PlatformIO toolbar buttons in VS Code.

---

## USB / serial

GPIO 18/19 on the ESP32-C6 serve double duty as USB D−/D+ **and** the
shared I2C bus for the AXS5106L touch controller and QMI8658A IMU.
Because this board routes those pins to I2C, `ARDUINO_USB_CDC_ON_BOOT=0`
is set in [`platformio.ini`](platformio.ini).

Serial output uses **UART0** (TX = GPIO16, RX = GPIO17), bridged to USB
by the onboard CH340/CP2102 chip.

If your board revision uses a direct USB connection instead (no CH340),
set `ARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini` and remap the I2C
pins to a free GPIO pair.

---

## Demo sketch

[`src/main.cpp`](src/main.cpp) demonstrates all on-board peripherals:

- **Display** — static labels drawn once; IMU values refreshed every 100 ms
- **QMI8658A** — accelerometer (g) and gyroscope (°/s) printed on screen
  and over serial at 115200 baud
- **Touch** — polls AXS5106L every 20 ms; draws a dot in the lower panel
  at the touch position and prints raw coordinates to serial
- **WiFi** — persistent multi-network credential store, AP fallback, web
  config UI; see [WiFi notes](#wifi-notes) below
- **NTP clock** — syncs on connect, re-syncs every 12 h, displays 24 h
  time in the header top-right corner; timezone selectable via web UI

### Screen layout

```
y=  0 ┌────────────────────────┬─────────────┐
      │ ● SSID (≤15 chars)     │  HH:MM:SS   │  textsize 1
      │   IP address           │             │  textsize 1
y= 26 ├────────────────────────┴─────────────┤
      │ [Connected]  ESP32-C6 / Touch LCD    │  textsize 2
      │ [AP mode]    WiFi Config AP          │
      │              SSID / Pass / URL       │  textsize 1
      │ [Connecting] Connecting to: <SSID>   │
y= 76 ├──────────────────────────────────────┤
      │ -- Accelerometer (g) --              │
      │ X:  +0.00  Y:  +0.00  Z:  +0.00     │
      │ --- Gyroscope (dps) ---              │
      │ X:  +0.00  Y:  +0.00  Z:  +0.00     │
y=188 ├──────────────────────────────────────┤
      │ Touch: tap to draw                   │
y=210 ├──────────────────────────────────────┤
      │   touch drawing area                 │
y=320 └──────────────────────────────────────┘
```

Status dot colours: **green** = connected, **yellow** = connecting or AP mode.

---

## WiFi notes

### First-time setup

On first boot (no credentials stored) the device starts a temporary Access
Point and shows the credentials on screen:

```
SSID: ESP32-C6-XXXX   ← random 4-char suffix, regenerated each boot
Pass: XXXXXX           ← random 6-char password, regenerated each boot
      192.168.4.1/settings
```

Connect to that network and open `http://192.168.4.1/settings` in a browser.
The settings page scans for nearby networks, shows a dropdown, and lets you
enter a password. Saving triggers an immediate connection attempt.

### Credential storage

Credentials are stored in NVS (non-volatile flash) via the `Preferences`
library — no source-file changes needed and they survive reflashing firmware.

Up to **3 networks** are stored in a circular buffer. When a fourth is added,
the oldest slot is overwritten. On connection attempt, networks are tried
**newest-first** with a 10-second timeout per entry.

### Connection / AP lifecycle

| Situation | Behaviour |
|-----------|-----------|
| No credentials | AP starts immediately |
| Credentials present | Try each, newest-first; start AP if all fail |
| STA connects | AP is shut down |
| STA link lost | AP restarts; immediate retry, then every 2 minutes |
| AP active | Web server reachable at `192.168.4.1` |
| STA connected | Web server reachable at the STA IP |

The AP and STA run in dual-mode (`WIFI_AP_STA`) during connection attempts
so the configuration page remains accessible while the device is connecting.

---

## NTP clock notes

### Behaviour

- `configTzTime()` is called as soon as a STA connection succeeds.
- Time is re-synced every **12 hours** automatically.
- Changing the timezone via the settings page calls `configTzTime()` immediately.
- The clock displays `--:--:--` until the first successful NTP sync.
- 24-hour format is always used.

### Clock position

`HH:MM:SS` (textsize 1, 48 × 8 px) sits in the **top-right corner** of the
header at x = 122, y = 2. The SSID in the same row is capped at 15 characters
to avoid overlap.

### Timezone selection

Timezone is configured on the web settings page (`/settings`, section *Time
Zone*) and stored in NVS under key `tz` as a POSIX TZ string.

Built-in zones (20 entries covering all major regions):

| Label | POSIX string |
|-------|-------------|
| UTC | `UTC` |
| GMT/BST (London, Dublin) | `GMT0BST,M3.5.0/1,M10.5.0` |
| CET (Stockholm/Paris/Berlin) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| EET (Helsinki/Kyiv/Tallinn) | `EET-2EEST,M3.5.0/3,M10.5.0/4` |
| EST/EDT (New York) | `EST5EDT,M3.2.0,M11.1.0` |
| PST/PDT (Los Angeles) | `PST8PDT,M3.2.0,M11.1.0` |
| JST (Tokyo/Seoul) | `JST-9` |
| … and 13 more | see `TZ_LIST[]` in `src/main.cpp` |

---

## Display driver notes (JD9853 / ST7789)

Although Arduino_GFX treats the panel as an ST7789, the actual controller is a
**JD9853**. The standard ST7789 init sequence alone is insufficient — the panel
requires a vendor-specific register sequence sent after `gfx->begin()`.

### Mandatory init order

```cpp
gfx->begin();       // standard ST7789 init
lcd_reg_init();     // JD9853 vendor registers + MADCTL + INVON
gfx->setRotation(n);// re-apply rotation MADCTL after panel init
```

`lcd_reg_init()` (see [`src/main.cpp`](src/main.cpp)) ends with two critical
commands that must come from this custom sequence, not from Arduino_GFX's
defaults:

| Command | Value | Effect |
|---------|-------|--------|
| `0x36` (MADCTL) | `0x00` | RGB channel order |
| `0x21` (INVON) | — | Display inversion on — required by this panel |

Skipping `lcd_reg_init()` causes the red and blue colour channels to be
swapped (cyan appears yellow, yellow appears cyan, etc.).

`setRotation()` must be called **after** `lcd_reg_init()` because the panel
init overwrites MADCTL to `0x00`; `setRotation()` then re-applies the correct
rotation bits on top.

### Rotation table

The constructor must be created with `rotation = 0`; the desired orientation
is set via `gfx->setRotation(n)` after `lcd_reg_init()`.

| `setRotation(n)` | MADCTL sent | Orientation |
|:---:|---|---|
| 0 | `0x00` | Portrait, natural (top of board = top of image) |
| 2 | `0xC0` (MX + MY) | Portrait, 180° |
| 4 | `0x40` (MX) | Portrait, horizontally mirrored |
| 6 | `0x80` (MY) | Portrait, vertically mirrored |
| 1 | `0x60` (MX + MV) | Landscape |
| 3 | `0xA0` (MY + MV) | Landscape, 180° |
| 5 | `0xE0` (MX + MY + MV) | Landscape, horizontally mirrored |
| 7 | `0x20` (MV) | Landscape, vertically mirrored |

---

## Touch sensor notes (AXS5106L)

The AXS5106L communicates over I2C (address `0x3B`) using a 5-byte burst read
from register `0x01`:

| Byte | Content |
|------|---------|
| 0 | Touch-point count (0 or 1) |
| 1 | X high nibble (bits 11:8) |
| 2 | X low byte (bits 7:0) |
| 3 | Y high nibble (bits 11:8) |
| 4 | Y low byte (bits 7:0) |

The AXS5106L reports coordinates already in **display pixel space** (0–171 × 0–319) — no 0–4095 scaling needed. For rotation 0 (portrait) the X axis is mirrored relative to the display, so apply:

```cpp
int16_t px = (LCD_WIDTH  - 1) - raw_x;
int16_t py = raw_y;
```

The controller requires a hardware reset pulse on RST (GPIO20) before first use.

---

## IMU notes (QMI8658A)

The QMI8658A is a 6-axis IMU (3-axis accelerometer + 3-axis gyroscope) on I2C
address `0x6A` (SA0 tied to GND). It shares the bus with the AXS5106L touch
controller — initialise `Wire` once and access both devices normally.

### Recommended configuration

| Parameter | Constant | Value |
|-----------|----------|-------|
| Accel range | `ACC_RANGE_4G` | ±4 g |
| Accel ODR | `ACC_ODR_250Hz` | 250 Hz |
| Gyro range | `GYR_RANGE_64DPS` | ±64 °/s |
| Gyro ODR | `GYR_ODR_224_2Hz` | 224 Hz |

### Available ranges

| Parameter | Options |
|-----------|---------|
| Accel range | `ACC_RANGE_2G`, `ACC_RANGE_4G`, `ACC_RANGE_8G`, `ACC_RANGE_16G` |
| Accel ODR | `ACC_ODR_8000Hz` … `ACC_ODR_31Hz` |
| Gyro range | `GYR_RANGE_16DPS` … `GYR_RANGE_2048DPS` |
| Gyro ODR | `GYR_ODR_8000Hz` … `GYR_ODR_31Hz` |

Data-ready is polled via `imu.getDataReady()` before each `getAccelerometer()`
/ `getGyroscope()` call.

---

## Project structure

```
├── include/
│   └── board_pins.h    # All GPIO constants
├── src/
│   └── main.cpp        # Demo application
├── platformio.ini      # Build configuration
└── README.md
```

---

## References

- [Waveshare wiki — ESP32-C6-Touch-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47)
- [Board schematic (PDF)](https://files.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47/ESP32-C6-Touch-LCD-1.47-Schematic.pdf)
- [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX_Library)
- [SensorLib (QMI8658A driver)](https://github.com/lewisxhe/SensorLib)
- [ESP32-C6 datasheet (Espressif)](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
