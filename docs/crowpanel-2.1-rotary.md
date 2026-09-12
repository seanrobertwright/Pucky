# Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display (DHE03921D)

Purchased on Amazon as "IoTeikXgo 2.1 Inch ESP32 Rotary Display" (ASIN B0GQS8L5LT) — a rebrand
of Elecrow's board. Identified 2026-09-12 by matching esptool output (bare ESP32-S3R8, native USB,
16 MB flash / 8 MB PSRAM) against Elecrow's spec and example code.

## Board

| Item | Value |
|---|---|
| Model | Elecrow CrowPanel 2.1" Rotary, **DHE03921D** (released Sept 2025) |
| MCU | **ESP32-S3R8** (bare chip, QFN56, rev v0.2) — dual-core LX7 @ 240 MHz, 512 KB SRAM |
| Flash | 16 MB, quad SPI, 3.3 V (GigaDevice, JEDEC c8/4018) |
| PSRAM | 8 MB embedded (octal, AP_3v3) |
| USB | **Native ESP32-S3 USB-Serial/JTAG** — VID `303A` / PID `1001`, no CH340. Enumerates as "USB Serial Device (COMx)"; port moves between USB sockets |
| MAC | `1c:db:d4:b3:5f:94` (this unit) |
| Display | 2.1" 480×480 round IPS, capacitive touch |
| Input | Rotary encoder with push button; ambient/"breathing" LED |
| Expansion | FPC connector with UART + I2C |
| Power | USB-C, 5 V / 1 A |
| Size | 79 × 79 × 30 mm, 80 g; -20…65 °C |

## Pin map (from Elecrow `RotaryScreen_2_1.ino`)

### LCD — ST7701, 16-bit RGB parallel

Init commands go over 9-bit software SPI; pixel data over the RGB bus.

| Signal | GPIO |
|---|---|
| SPI CS | 16 |
| SPI SCK | 2 |
| SPI SDA (MOSI) | 1 |
| DE | 40 |
| VSYNC | 7 |
| HSYNC | 15 |
| PCLK | 41 |
| R0..R4 | 46, 3, 8, 18, 17 |
| G0..G5 | 14, 13, 12, 11, 10, 9 |
| B0..B4 | 5, 45, 48, 47, 21 |
| Backlight | 6 (LEDC PWM; example runs ~80 % = 204/255) |

RGB timing (Arduino_GFX `Arduino_ESP32RGBPanel`):

```
hsync: polarity 1, front porch 10, pulse 4, back porch 20
vsync: polarity 1, front porch 10, pulse 4, back porch 20
pclk active neg 0, 12 MHz, de idle high 0, pclk idle high 0
bounce buffer: 480 * 20 px
init sequence: st7701_type5_init_operations
```

Note: the example swaps red/blue in the RGB565 output before flushing to LVGL ("ESP-IDF 5's
RGB path on this ST7701 board presents red and blue swapped").

### I2C bus — SDA 38, SCL 39

| Device | Address | Role |
|---|---|---|
| **CST8xx** touch | `0x15` | Capacitive touch (Adafruit_CST8XX library) |
| **PCF8574** I/O expander | `0x21` | See table below |

PCF8574 pins:

| Pin | Function | Dir |
|---|---|---|
| P0 | Touch RST | out |
| P2 | Touch INT | out |
| P3 | LCD power enable | out |
| P4 | LCD reset | out |
| P5 | **Encoder push button** | in, pull-up |

Power-up sequence in the example: P3 HIGH (LCD power) → P4 HIGH/LOW/HIGH (LCD reset pulse)
→ P0 HIGH/LOW/HIGH (touch reset pulse) → P2 HIGH.

### Encoder & LED

| Signal | GPIO |
|---|---|
| Encoder A | 42 |
| Encoder B | 4 |
| Encoder button | PCF8574 P5 |
| Breathing LED | 43 (LEDC PWM) |

## Software (Elecrow reference stack)

- Arduino IDE / Arduino-ESP32 core 3.x (`ledcAttach` API)
- **Arduino_GFX** (`Arduino_SWSPI` + `Arduino_ESP32RGBPanel` + `Arduino_RGB_Display`)
- **Adafruit_CST8XX** (touch), **PCF8574** library (expander), **LVGL** (v9 in the example)
- Custom partition table `elecrow_s3.csv`:

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0xA00000,
spiffs,   data, spiffs,  0xA10000,0xE0000,
coredump, data, coredump,0xAF0000,0x10000,
```

## Firmware found on this unit (as received)

- Arduino sketch, core 2.0.x (IDF v4.4.6, built 4 Oct 2023)
- Partition table: `nvs` 24 KB, `phy_init`, single `app0` factory partition of ~16 MB
  (not Elecrow's layout — likely the reseller's own demo)
- Console on native USB; prints only `esp_core_dump_flash: No core dump partition found!`

## Links

- Wiki: https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html
- GitHub (examples, factory firmware, schematic, datasheets):
  https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen
  - `example/Arduino/RotaryScreen_2_1/RotaryScreen_2_1.ino` — reference sketch (pin map source)
  - `example/Arduino/Modify_Partition_Table/elecrow_s3.csv`
  - `factory_firmware/` — pre-built bins
  - `Eagle_SCH&PCB/` — schematic
- CNX Software: https://www.cnx-software.com/2025/09/19/elecrow-esp32-s3-rotary-displays-combine-round-ips-touchscreen-knob-and-press-input/
- Amazon listing: https://www.amazon.com/dp/B0GQS8L5LT

## Useful commands

```powershell
esptool --port COM8 chip-id      # identify chip (port number varies)
esptool --port COM8 flash-id     # flash size/type
esptool --port COM8 --baud 921600 read-flash 0 0x1000000 firmware\backup\rotary21-full.bin   # full backup
```
