# DPS310 bench test (ESP32 DevKit V1)

Runs on **Adafruit's DPS310 library** (`adafruit/Adafruit DPS310`), not a
hand-written driver.

```bash
pio run -t upload
pio device monitor -b 115200
```

## Why the library

A hand-written register driver was compared against the library on this board, in
the same mode (128x oversampling, 4 Hz), one after the other:

| | hand-written | Adafruit library |
| :--- | ---: | ---: |
| temperature | −3.6 °C | **22.9 °C** |
| pressure | 894.8 hPa | **899.7 hPa** |
| height noise | 0.36 cm | **0.27 cm** |

The room was about 23 °C, so the library's temperature was right and the
hand-written one was 26 degrees low — and that error moved the compensated
pressure by 5 hPa. The coefficient handling for the temperature term is subtler
than the datasheet formula suggests. The library wins on correctness, and there is
now one implementation instead of two.

## What the bench found

* **The part answers on SDA 21 / SCL 22**, the reverse of the module's SDI/SCK
  labelling. The sketch tries both orders and reports which has a bus on it.
* **`dio` will not boot this board.** The ROM loads the second-stage bootloader
  and then loops on its entry point with no output. `board_build.flash_mode = dout`.
* **The library does not check readiness before reading.** Its first conversions
  return stale registers — 833 hPa and −66 °C — so the sketch gates on
  `pressureAvailable()` and rejects implausible temperatures.

## Measured

128x oversampling, 4 Hz, at rest, after discarding the first 4 s:

```
88 samples over 22 s
  mean 899.686 hPa   sd 0.0321 hPa   peak-to-peak 0.116 hPa
  height noise ±0.27 cm      temperature 22.94 C      rate 4.0 Hz
```
