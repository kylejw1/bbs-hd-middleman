# DPS310 Relative Altimeter & Bench Test (ESP32 DevKit V1)

Runs on **Adafruit's DPS310 library** (`adafruit/Adafruit DPS310`).

```bash
pio run -t upload
pio device monitor -b 115200
```

## Operation & Features

* **Maximum Pressure Precision**: Operates at **128x oversampling** (the hardware maximum of the DPS310) at 4 Hz.
* **Background Temperature Compensation**: Temperature is polled at 1 Hz (8x oversampling). Even when temperature is not displayed, the DPS310 silicon diaphragm physically requires continuous temperature compensation to eliminate thermal pressure drift.
* **Startup Zero Calibration**: Automatically discards initial boot/settling samples and averages the first ~3 seconds of baseline readings to lock in $P_0$ (0.00 cm).
* **Double-Precision Hypsometric Equation**: Uses 64-bit IEEE 754 floating point arithmetic (`double`) to prevent catastrophic cancellation errors when subtracting $(1 - (P/P_0)^a)$ for small centimeter-scale movements.
* **Dual Output & Visual Meter**: Outputs both raw height (instantaneous) and EMA-filtered height (low jitter), plus an ASCII elevation indicator.
* **Interactive Re-Zero**: Send `'z'` or press `Enter` in the serial terminal at any time to re-zero the ground reference to the sensor's current position.

## What the bench found

* **The part answers on SDA 21 / SCL 22**, the reverse of the module's SDI/SCK labelling. The sketch tries standard 21/22 first and automatically falls back to 22/21 if needed.
* **`dio` will not boot this board.** The ROM loads the second-stage bootloader and then loops on its entry point with no output. `board_build.flash_mode = dout`.
* **The library returns stale registers on boot.** Its first conversions return uncompensated values (~833 hPa and −66 °C). The sketch gates on temperature sanity checks (`>-40 °C`) and discards initial settling frames before establishing the 0 baseline.

## Output Format

```text
 Time(s) |  Height (cm) |   Raw (cm)  | Height (m) | Pressure (hPa) | Elevation Visual
---------+--------------+-------------+------------+----------------+-----------------------------------
    6.77 |       -1.34 cm |      -3.82 cm |     -0.013 m |        900.234 | [-50cm             0             +50cm]
    7.02 |        1.37 cm |       6.41 cm |     +0.014 m |        900.223 | [-50cm             0             +50cm]
    7.52 |        4.61 cm |       7.35 cm |     +0.046 m |        900.222 | [-50cm             |*            +50cm]
```
