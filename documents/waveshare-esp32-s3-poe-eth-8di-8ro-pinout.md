# Waveshare ESP32-S3-POE-ETH-8DI-8RO — Hardware Reference

Sources: ESPHome devices page, GitHub (abrinlee, Chojinodebahia), Waveshare product page.

## Core Module
- **MCU**: ESP32-S3-WROOM-1U-N16R8
- **CPU**: Dual-core Xtensa LX7 @ 240 MHz
- **Flash**: 16 MB
- **PSRAM**: 8 MB octal SPI @ 80 MHz
- **Wireless**: 2.4 GHz WiFi + Bluetooth LE

## Power
| Source | Voltage |
|--------|---------|
| USB-C  | 5 V |
| Screw terminal | 7–36 V DC |
| PoE (IEEE 802.3af) | 48 V via RJ45 |

## Digital Inputs (DI1–DI8)
**Opto-isolated, active-low, board pull-up on opto output side.**

| Label | GPIO |
|-------|------|
| DI1   | 4    |
| DI2   | 5    |
| DI3   | 6    |
| DI4   | 7    |
| DI5   | 8    |
| DI6   | 9    |
| DI7   | 10   |
| DI8   | 11   |

- GPIO reads **HIGH** when no external signal (opto off) — wire break = HIGH = danger
- GPIO reads **LOW** when opto energised (external current flowing) — circuit intact = LOW = safe
- Do NOT configure internal pull-down on these pins; the board has its own pull-up on the opto output

## Relay Outputs (RO1–RO8)
**All 8 relays controlled via I2C GPIO expander — NOT direct GPIO.**

| Component | Detail |
|-----------|--------|
| Expander chip | TCA9554 (TI) / PCA9554 (NXP) — compatible |
| I2C address | 0x20 |
| I2C SDA | GPIO 42 |
| I2C SCL | GPIO 41 |
| I2C frequency | 100 kHz |
| Output bit | RO1 = bit 0 … RO8 = bit 7 |
| Contact rating | ≤ 10 A @ 250 VAC or 30 VDC |
| Contact types | COM + NO + NC |

To energise relay N (0-indexed): write bit N HIGH to the expander output register.

## Ethernet (W5500 SPI)
| Signal | GPIO |
|--------|------|
| CS     | 16   |
| INT    | 12   |
| SCLK   | 15   |
| MISO   | 14   |
| MOSI   | 13   |

## Onboard Peripherals
| Peripheral | GPIO | Notes |
|------------|------|-------|
| RGB LED (WS2812) | 38 | Single LED |
| Buzzer | 46 | LEDC PWM |
| Boot / Reset button | 0 | Active low, internal pull-up |
| RS485 TX | 17 | |
| RS485 RX | 18 | |
| RTC (PCF85063) | I2C | Shared bus (SDA 42, SCL 41) |

## Reserved / Restricted Pins
| GPIO | Reason |
|------|--------|
| 0 | Strapping pin (BOOT); handle carefully at power-on |
| 4–11 | DI inputs (opto-isolated) |
| 12–16 | W5500 Ethernet SPI |
| 13–15 | SPI shared |
| 17–18 | RS485 UART |
| 19–20 | USB-JTAG (disabling loses debug) |
| 38 | Onboard WS2812 |
| 41–42 | I2C bus (relay expander + RTC) |
| 46 | Buzzer |

## Input Logic (Fail-Safe Wiring)
For e-stop / zone sensor with NC contacts:

```
External 12/24V supply → NC contact → DI screw terminal
                                      (open = no current = opto off = GPIO HIGH = DANGER)
                                      (closed = current flows = opto on = GPIO LOW = SAFE)
```

Wire break or open contact both result in GPIO HIGH → treat HIGH as danger level.
