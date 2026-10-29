# DPS310 bench test (ESP32 DevKit V1)

```bash
pio run -t upload
pio device monitor -b 115200
```

## What it found on the bench

* **The pins are the other way round from the labels.** `SDI -> 21, SCK -> 22` on
  the silk does not match: the part answered only with **SDA 21 / SCL 22**, i.e.
  the ESP32's usual default. The sketch tries both orders and reports which one
  has a bus on it, so this is visible rather than a silent "no sensor".
* **`dio` will not boot on this board.** With the default flash mode the ROM
  loaded the second-stage bootloader and then looped on its entry point with no
  output at all. `board_build.flash_mode = dout` fixed it.
* **The compensated result is in pascals.** 89530 is 895.3 hPa, correct for
  ~1000 m. Treating it as hPa made altitude and slope wrong by 100x.
* **Gating on TMP_RDY throttled the loop to the 1 Hz temperature rate.** Pressure
  readiness governs; temperature is read when offered and otherwise reused.

## Measured

At 128x pressure oversampling, 4 Hz output, on a bench at rest:

```
81 samples over 20 s
  mean 894.821 hPa   sd 0.0428 hPa
  peak-to-peak 0.163 hPa   (1.4 cm of height)
```

0.043 hPa is about **0.36 cm of height noise**, an order of magnitude better than
a phone barometer. For grade this matters: at that noise a 5 m window yields
roughly 0.06 % of slope noise, which is inside any sane level hysteresis.
