# BBS-HD Wi-Fi Middleman & Configuration Bridge
### For Bafang BBS-HD / BBS02 with [`bbs-fw`](https://github.com/danielnilsson9/bbs-fw) Firmware

---

## What is This?

This project turns an **ESP32-S3 (44-Pin DevKit)** into an intelligent, inline serial bridge between your **Bafang Mid-Drive Motor** (running `bbs-fw`) and your **Handlebar Display** (500C, 850C, DPC-18, SW102, Eggrider, etc.).

It lets you **wirelessly tune and monitor your e-bike from your smartphone or laptop** via a local Wi-Fi dashboard, without needing a USB programming cable or a Windows PC.

```
 +------------------+     5V TTL     +-----------------------+     5V TTL     +--------------------+
 |  Bafang Display  | <------------> |   ESP32-S3 Bridge     | <------------> |  BBS-HD Controller |
 | (Handlebar unit) | (Level Shifter)| (Inline Middleman)    | (Level Shifter)| (Running bbs-fw)   |
 +------------------+                +-----------------------+                +--------------------+
                                                 ^
                                                 | Wi-Fi (No Internet Needed)
                                                 v
                                    +-------------------------+
                                    | Smartphone / Web UI     |
                                    | http://192.168.4.1      |
                                    +-------------------------+
```

### Why a "Smart" Middleman Instead of a Y-Cable?
On standard Bafang systems, the display and controller talk over a single 1200-baud serial line. If you inject configuration commands while the display is polling, **the packets collide, the config corrupts, and the display flashes Error 30 (Communication Failure)**.

This middleman solves that:
1. **Normal Riding**: Transparently passes packets between display and motor in real-time, passively extracting live speed, power, voltage, and temperature for the web dashboard.
2. **Configuring / Flashing**: When you tap "Save to Controller" on your phone, the middleman **temporarily isolates the controller** and sends mock keep-alive packets to the display. The display stays alive, **never sees a glitch or Error 30**, while the controller receives pure, uncorrupted configuration data.

---

## Bill of Materials (BOM)

To build this setup, you need:

| Item | Quantity | Description / Notes |
| :--- | :---: | :--- |
| **ESP32-S3 DevKit** | 1 | 44-pin board (e.g. ESP32-S3-DevKitC-1 with dual USB-C). |
| **Logic Level Shifter** | 1 | 4-channel bi-directional 3.3V $\leftrightarrow$ 5V module (e.g. BSS138-based). |
| **DC-DC Buck Converter** | 1 | High-voltage step-down converter rated for **at least 60V DC input** and 5V output (e.g. LM2596HV or MP1584EN). |
| **Bafang 5-Pin Extension** | 1 | Green Higo/Julet 5-pin male-to-female cable (to splice into without cutting original bike wiring). |
| **Enclosure / Heatshrink** | 1 | To weatherproof the module when mounted to the bike frame. |

---

## ⚠️ Electrical Safety Warning

> [!CAUTION]
> **Pin 1 (BAT+) of the Bafang connector carries the FULL voltage of your battery pack (48V to 58.8V DC)!**
> * **NEVER** connect Pin 1 directly to the ESP32. It will instantly destroy the microcontroller.
> * Connect Pin 1 **ONLY** to the input of your high-voltage DC-DC buck converter and pass it through to the display.
> * Always disconnect your e-bike battery before soldering or wiring!

---

## Hardware Wiring & Pinout

### 1. Bafang 5-Pin Connector (Green Higo)

```
        Looking into Female Connector:
                   [Key]
                  1 (Top)
              5             2
                4         3
```

| Pin | Color (typical) | Name | Voltage | Where to Connect |
| :-: | :--- | :--- | :--- | :--- |
| **1** | Red / Brown | **BAT+** | 48V – 58.8V | **Pass through** to Display **AND** connect to **DC-DC Buck Input (+)** |
| **2** | Black | **GND** | 0V | **Common Ground** (Battery (-), Buck (-), ESP32 GND, Shifter GND) |
| **3** | Blue | **P+** | Lock Switch | **Pass directly** from Controller to Display (powers up the motor) |
| **4** | Green | **TXD / RXD** | 5V TTL | Serial data (see table below) |
| **5** | Yellow | **RXD / TXD** | 5V TTL | Serial data (see table below) |

---

### 2. ESP32-S3 (44-Pin DevKit) Connections

```
                     +---------------------------------------+
                     |             ESP32-S3 DevKit           |
                     |               (Top View)              |
     Level Shifter LV -| [ 1] 3.3V                       GND [44] |- Common GND
               3.3V -| [ 2]                             [43] |- GPIO 43 (TX0 / Debug)
                RST -| [ 3]                             [42] |- GPIO 44 (RX0 / Debug)
             GPIO 4 -| [ 4]                             [41] |- GPIO 1
             GPIO 5 -| [ 5]                             [40] |- GPIO 2
             GPIO 6 -| [ 6]                             [39] |- GPIO 42
             GPIO 7 -| [ 7]                             [38] |- GPIO 41
   DISPLAY_RX -> 15 -| [ 8]                             [37] |- GPIO 40
   DISPLAY_TX -> 16 -| [ 9]                             [36] |- GPIO 39
CONTROLLER_RX -> 17 -| [10]                             [35] |- GPIO 38
CONTROLLER_TX -> 18 -| [11]                             [34] |- GPIO 37
             GPIO 8 -| [12]                             [33] |- GPIO 36
             GPIO 3 -| [13]                             [32] |- GPIO 35
            GPIO 46 -| [14]                             [31] |- GPIO 0
             GPIO 9 -| [15]                             [30] |- GPIO 45
            GPIO 10 -| [16]                             [29] |- GPIO 48 (Status LED)
            GPIO 11 -| [17]                             [28] |- GPIO 47
            GPIO 12 -| [18]                             [27] |- GPIO 21
            GPIO 13 -| [19]                             [26] |- GPIO 20 (USB D+)
            GPIO 14 -| [20]                             [25] |- GPIO 19 (USB D-)
      Buck 5V Out In -| [21] 5V                          GND [24] |- Common GND
         Common GND -| [22] GND                         GND [23] |- Common GND
                     +---------------------------------------+
```

---

### 3. Full Schematic Diagram

```
  BAFANG CONTROLLER CABLE                           BAFANG DISPLAY CABLE
  =======================                           ====================
  Pin 1 (BAT+ 52V) -------------------------------+--------> Pin 1 (BAT+)
         |
         +--> [ Buck Converter IN+ ]
              [ Buck Converter IN- ] <---+
                                         |
              [ Buck Converter OUT+ (5V) ] ------> ESP32 5V (Pin 21) & Level Shifter HV
              [ Buck Converter OUT- (GND)] --+---> ESP32 GND (Pin 22) & Level Shifter GND
                                             |
  Pin 2 (GND) -------------------------------+-------------> Pin 2 (GND)

  Pin 3 (P+ Lock) -----------------------------------------> Pin 3 (P+ Lock)

  Pin 4 (Controller RX) <--- [Shifter HV1 <-> LV1] <--- ESP32 GPIO 18 (TX1)
  Pin 5 (Controller TX) ---> [Shifter HV2 <-> LV2] ---> ESP32 GPIO 17 (RX1)

  Pin 4 (Display TX)    ---> [Shifter HV3 <-> LV3] ---> ESP32 GPIO 15 (RX2)
  Pin 5 (Display RX)    <--- [Shifter HV4 <-> LV4] <--- ESP32 GPIO 16 (TX2)
```

---

## Flashing the Firmware

### Option A: Using PlatformIO (Recommended)

1. Connect the ESP32-S3 to your computer via USB.
2. In a terminal, navigate to this project folder:
   ```bash
   cd /home/kyle/dev/bbs-hd-middleman
   ```
3. Build and upload:
   ```bash
   /home/kyle/.platformio/penv/bin/pio run -t upload
   ```
4. View the serial debug output:
   ```bash
   /home/kyle/.platformio/penv/bin/pio device monitor
   ```

### Option B: Using VS Code + PlatformIO Extension
1. Open this folder in VS Code.
2. Click the **PlatformIO Alien icon** on the left bar.
3. Under Project Tasks, click **Upload**.

---

## Step-by-Step User Guide

### 1. Powering Up
1. Turn on your e-bike battery.
2. Press the power button on your handlebar display. The display connects Pin 1 to Pin 3, which wakes up the motor controller and the 5V buck converter powering the ESP32.
3. The on-board LED on the ESP32 will pulse slowly while starting up.

### 2. Connecting to Wi-Fi
1. On your smartphone, tablet, or laptop, open Wi-Fi settings.
2. Connect to the network:
   * **SSID**: `BBS-FW-Middleman`
   * **Password**: `bafang1234`
3. If a captive portal notification pops up ("Tap to sign in"), tap it. Otherwise, open Chrome/Safari and browse to:
   ```
   http://192.168.4.1/
   ```

### 3. Dashboard Features

* **Live Dashboard**: View real-time speedometer (km/h or mph), motor power (Watts), motor current (Amps), battery voltage (V), battery state of charge (%), controller temperature, and motor temperature.
* **Direct Controls**: Toggle Standard/Sport mode, turn headlights ON/OFF, or click assist levels (0 to 9) straight from your screen.
* **Motor & Limits**: Set Max Current (up to 33A), Current Ramp Rate, Low Voltage Cutoff (LVC), Max Battery Voltage, Wheel Size, and Speed Limit.
* **PAS & Throttle**: Fine-tune magnet pulse start delay (e.g. 2–3 pulses for instant engagement), stop delay (150–250ms), keep current %, and throttle voltage range.
* **Assist Levels (0–9)**: Independent matrices for **Standard** and **Sport** modes. Customize Target Current %, Max Throttle %, Cadence %, Speed %, and flags (PAS, Throttle, Cruise, Cadence Override, Speed Override) for every single assist level!
* **Sensors**: Toggle speed sensor, shift sensor cut duration & threshold %, walk assist, and temperature sensor modes.
* **Voltage Calibration**: Grab a digital multimeter, check your battery pack voltage at the charge port, type the reading into the calibration box, and hit **"Calibrate"**. The middleman syncs the controller's internal ADC in EEPROM.
* **Backup & Restore**: Export your customized tuning as a `.json` file to share with other riders or keep as a backup before experimenting.

### 4. Using the Hosted Dashboard over Bluetooth (no Wi-Fi needed)

The dashboard above is served *by the middleman itself*, so it already works with no internet and no router — you just have to join the bike's own Wi-Fi. If you would rather keep your phone on its normal network, use the hosted copy instead:

1. Open **https://kylejw1.github.io/bbs-hd-middleman/** in **Chrome** (Android, or desktop Chrome/Edge). If you use **Brave**, read the note below first — it ships with Web Bluetooth switched off.
2. Tap **Bluetooth** in the header and pick **BBSHD-Middleman** from the browser's device list.
3. The page is now talking to the bike over Bluetooth LE. Add it to your home screen and it keeps working with no network at all — the app shell is cached on first visit.

Notes and limits:

* **Chromium only.** Safari on iOS has no Web Bluetooth, so iPhones must use the on-device Wi-Fi dashboard from step 2 above. That copy is always available and is never going away.
* **Brave needs Web Bluetooth enabled by hand.** Brave is Chromium-based but ships the API disabled, so the header shows **BT: Unsupported** and `navigator.bluetooth` is undefined. Turn it on with `brave://flags/#enable-web-bluetooth` → **Enabled**, then restart the browser. Launching Brave with `--enable-features=WebBluetooth` does the same thing. This is Brave's deliberate choice, not something the page can work around.
* The page checks whether the browser exposes Web Bluetooth and says **BT: Unsupported** if it does not, rather than offering a button that cannot work. If you see that, use the Wi-Fi dashboard instead.
* The browser remembers the device, so a revisit reconnects on its own. If it does not, tap **Bluetooth** again.
* Bluetooth and the SoftAP share the ESP32's single radio. Bluetooth is meant to replace the Wi-Fi dashboard, not to run alongside it under heavy polling.
* **Firmware updates are the one exception.** A `.bin` is far too large for the Bluetooth link, and a secure page cannot talk to the device's plain-HTTP address, so the hosted copy's Firmware tab just tells you where to go. To flash, join `BBS-FW-Middleman` and open `http://192.168.4.1/` (or `http://bbshd.local/` if the middleman is on your home network) and upload from there.
* **Set a Bluetooth PIN.** Out of the box the Bluetooth API is open, which means anyone within radio range could change your settings. Click **BT PIN** in the header and choose something 4–16 characters; the page remembers it and sends it automatically from then on. If you ever forget it, the Wi-Fi page is never gated — reconnect the normal way and set a new one (this is why the header has two buttons and not one).
* The page checks the device's Bluetooth protocol version before connecting, so a newer hosted page will refuse to talk to firmware it does not understand rather than misbehave.
* The hosted page is a convenience: everything it can do, the on-device page can also do.

---

## Troubleshooting & FAQ

#### The display shows "Error 30" (Communication Error)
* **Check Level Shifter Ground**: The ESP32 ground, level shifter ground, and Bafang ground must all be tied together.
* **Check TX/RX crossover**:
  * ESP32 `GPIO 18` (TX) goes to Controller Pin 4 (RX).
  * ESP32 `GPIO 17` (RX) goes to Controller Pin 5 (TX).
  * If in doubt, try swapping Pin 4 and Pin 5 on one of the sides.

#### The ESP32 doesn't turn on when I turn on the display
* Check your DC-DC buck converter. Verify with a multimeter that its output is exactly 5.0V.
* Ensure Pin 3 (Blue wire) is passed directly from the display to the controller; without Pin 3, the display cannot wake up the controller.

#### Can I connect the ESP32 to my home Wi-Fi?
* Yes! In the top-right corner of the web interface, click the **"Wi-Fi"** button, type your home Wi-Fi SSID and password, and submit. Once the join succeeds the middleman stays on your home network at `http://bbshd.local/` and turns its own access point off, because the ESP32's single radio handles either job well but both at once poorly. If the join fails, the fallback access point comes back automatically.

#### Does the dashboard need an internet connection?
* No. The page, its CSS and its JavaScript are all compiled into the ESP32 and served from it, with no CDN or external request of any kind. There is nothing to load from the internet, which is why it works in the middle of nowhere. The only optional internet-dependent piece is the hosted Bluetooth copy described in step 4 above, and even that works offline after the first visit.
