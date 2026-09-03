# BBS-HD / BBS-FW ESP32-S3 Serial Middleman & Wi-Fi Bridge

A smart, transparent serial bridge and Wi-Fi configuration dashboard for Bafang BBS-HD / BBS02 mid-drive e-bike motors running the open-source **[bbs-fw](https://github.com/danielnilsson9/bbs-fw)** custom firmware.

The ESP32-S3 sits inline between your **Bafang HMI Display** (e.g. 500C, 850C, DPC-18, SW102, Eggrider) and the **BBS-HD Motor Controller**.

---

## Key Features

1. **Seamless Bi-directional Pass-Through (Transparent Bridge)**
   - Display queries and controller responses are forwarded in real time at standard Bafang 1200 baud 8N1.
   - Non-intrusive packet sniffing extracts live telemetry (speed, current, voltage, power, assist level, lights, temperatures, error codes) without polling overhead.

2. **Intelligent Config Arbitration & Display Keep-Alive (Zero Error 30)**
   - When you access the Wi-Fi Dashboard to read, edit, or flash configurations to the controller:
     - The middleman enters **Config Intercept Mode**.
     - It isolates the display's commands so they cannot corrupt or collide with configuration packets on the controller's bus.
     - **Mock Synthesizer**: The middleman responds to the display's periodic status requests using cached metrics. The display receives expected replies and **never freezes or flashes Error 30 (Comm Error)**.
     - Any button presses made on the display (like assist level changes or lights) during configuration are enqueued and flushed to the controller once the transaction completes.

3. **Full bbs-fw v5 Parameter Configuration via Web UI**
   - **Motor & Limits**: Max Current (up to 33A for BBSHD), Current Ramp Rate (A/s), Low Voltage Cutoff (LVC), Max Battery Voltage, Speed Limit, Wheel Size (inches), Speed Sensor Pulses, Metric/Imperial units.
   - **PAS & Throttle**: PAS start delay (magnet pulses), stop delay (ms), keep current %, keep current cadence (RPM), throttle voltage window (mV), initial power kick %, global speed limits.
   - **Per-Level Assist Matrix**: 10 Standard Levels (0-9) and 10 Sport Levels (0-9) with full flag controls: PAS enable, Throttle enable, Cruise, Cadence Override, Speed Override, and Target Current %.
   - **Peripherals & Features**: Speed sensor, Shift sensor cut duration and threshold, Push/Walk assist, Temperature sensors, Lights mode, Pretension cutoff, Walk mode display data field.

4. **Live Battery Multimeter Calibration**
   - Measure actual pack voltage with a digital multimeter, enter the voltage into the dashboard, and click "Calibrate". The middleman commands the controller to store the exact ADC calibration factor in EEPROM (`opcode 0xf3`).

5. **Self-Contained Mobile-Friendly Web Dashboard**
   - Hosted directly on the ESP32-S3 (SoftAP: `BBS-FW-Middleman`, IP: `192.168.4.1`) with a built-in DNS captive portal.
   - 100% offline: requires no external internet or CDNs. Works on your phone while on trails or in your garage.
   - Export and import configuration profiles as `.json` files.

---

## 44-Pin ESP32-S3 DevKit Pinout

The project is configured for standard 44-pin ESP32-S3 DevKit boards (2 rows of 22 pins):

```
                     +---------------------------------------+
                     |             ESP32-S3 DevKit           |
                     |               (Top View)              |
               3.3V -| [ 1]                             [44] |- GND
               3.3V -| [ 2]                             [43] |- GPIO 43 (TX0 / Debug)
                RST -| [ 3]                             [42] |- GPIO 44 (RX0 / Debug)
             GPIO 4 -| [ 4]                             [41] |- GPIO 1
             GPIO 5 -| [ 5]                             [40] |- GPIO 2
             GPIO 6 -| [ 6]                             [39] |- GPIO 42
             GPIO 7 -| [ 7]                             [38] |- GPIO 41
   DISPLAY_TX -> 15 -| [ 8]                             [37] |- GPIO 40
   DISPLAY_RX -> 16 -| [ 9]                             [36] |- GPIO 39
CONTROLLER_TX -> 17 -| [10]                             [35] |- GPIO 38
CONTROLLER_RX -> 18 -| [11]                             [34] |- GPIO 37
             GPIO 8 -| [12]                             [33] |- GPIO 36
             GPIO 3 -| [13]                             [32] |- GPIO 35
            GPIO 46 -| [14]                             [31] |- GPIO 0
             GPIO 9 -| [15]                             [30] |- GPIO 45
            GPIO 10 -| [16]                             [29] |- GPIO 48 (Status LED)
            GPIO 11 -| [17]                             [28] |- GPIO 47
            GPIO 12 -| [18]                             [27] |- GPIO 21
            GPIO 13 -| [19]                             [26] |- GPIO 20 (USB D+)
            GPIO 14 -| [20]                             [25] |- GPIO 19 (USB D-)
                 5V -| [21]                             [24] |- GND
                GND -| [22]                             [23] |- GND
                     +---------------------------------------+
```

| Function | ESP32-S3 Pin | Direction | Connects To (via Level Shifter) |
| :--- | :--- | :--- | :--- |
| **Controller RX** | `GPIO 18` (Pin 11) | Input (3.3V) | Controller TXD (Pin 5 on motor connector) |
| **Controller TX** | `GPIO 17` (Pin 10) | Output (3.3V) | Controller RXD (Pin 4 on motor connector) |
| **Display RX** | `GPIO 16` (Pin 9) | Input (3.3V) | Display TXD (Pin 4 on display connector) |
| **Display TX** | `GPIO 15` (Pin 8) | Output (3.3V) | Display RXD (Pin 5 on display connector) |
| **Status LED** | `GPIO 48` (Pin 29) | Output | Built-in RGB / Activity indicator |
| **Power In** | `5V` (Pin 21) | Power | 5V Output from DC-DC Buck Converter |
| **Ground** | `GND` (Pin 22/44) | Ground | Common Ground |

---

## Wiring & Electrical Schematic

### ⚠️ CRITICAL VOLTAGE WARNING
* **Bafang Battery Voltage (Pin 1)** carries full pack voltage (**36V – 58.8V DC**).
* **NEVER connect Pin 1 directly to the ESP32! Doing so will instantly destroy the ESP32.**
* Bafang serial signals are **5V TTL logic**. ESP32-S3 GPIOs operate at **3.3V**. Use a bi-directional logic level shifter (or resistor dividers on RX lines) between the Bafang 5V lines and ESP32 3.3V pins.

### Bafang 5-Pin (Green Higo) Connector Pinout

| Pin # | Wire Color | Label | Function | Connection in Middleman Setup |
| :---: | :--- | :--- | :--- | :--- |
| **1** | Red / Brown | **BAT+** | Battery Voltage (48V-52V) | **Pass directly** from Controller to Display + input to 5V DC-DC Buck Converter |
| **2** | Black | **GND** | Ground | Common Ground (Controller, Display, ESP32, Level Shifter) |
| **3** | Blue | **P+** | Power Switch Lock | **Pass directly** between Controller and Display (Display power button shorts Pin 1 to Pin 3 to boot the motor) |
| **4** | Green | **TXD / RXD** | Display TX &rarr; Controller RX | Connect to Display RX / Controller TX through Middleman |
| **5** | Yellow | **RXD / TXD** | Controller TX &rarr; Display RX | Connect to Controller RX / Display TX through Middleman |

### Interconnect Diagram

```
 [ BAFANG CONTROLLER ]                                [ BAFANG DISPLAY ]
   Pin 1 (BAT+ 52V)  -------------------------------->  Pin 1 (BAT+)
          |
          +---> [ DC-DC Buck Converter ]
                     (e.g. 12-60V -> 5V)
                        |
                        +---> ESP32 5V (Pin 21)
                        
   Pin 2 (GND)       -------------------------------->  Pin 2 (GND)
          |
          +-----------------> ESP32 GND (Pin 22)
          +-----------------> Level Shifter GND
          
   Pin 3 (P+ Lock)   -------------------------------->  Pin 3 (P+ Lock)

   Pin 4 (Motor RX)  <--- [Level Shifter HV1 <-> LV1] <--- ESP32 GPIO 17 (TX1)
   Pin 5 (Motor TX)  ---> [Level Shifter HV2 <-> LV2] ---> ESP32 GPIO 18 (RX1)

   Pin 4 (Display TX)---> [Level Shifter HV3 <-> LV3] ---> ESP32 GPIO 16 (RX2)
   Pin 5 (Display RX)<--- [Level Shifter HV4 <-> LV4] <--- ESP32 GPIO 15 (TX2)
```

---

## How to Build & Flash

### Prerequisites
* [PlatformIO Core](https://platformio.org/) installed (CLI or VS Code extension).

### Flashing via USB
1. Connect your ESP32-S3 DevKit to your computer via USB (native USB or UART port).
2. Build and upload:
   ```bash
   cd /home/kyle/dev/bbs-hd-middleman
   pio run --target upload
   ```
3. Open serial monitor (115200 baud):
   ```bash
   pio device monitor
   ```

---

## Using the Wi-Fi Dashboard

1. Power on your e-bike display (which supplies power to the motor and middleman).
2. On your phone or laptop, look for the Wi-Fi network:
   - **SSID**: `BBS-FW-Middleman`
   - **Password**: `bafang1234`
3. If a captive portal prompt appears, tap it; otherwise open your browser and navigate to:
   ```
   http://192.168.4.1/
   ```
4. **Dashboard**: Shows real-time speed, power, current, battery %, assist level, and temperatures.
5. **Config Tabs**:
   - Tap **"Read From Controller"** to pull the active EEPROM settings from your motor.
   - Adjust assist levels, current limits, ramp rates, or throttle response.
   - Tap **"Save To Controller"** to flash the new settings to the motor controller's EEPROM.
6. **Calibration**: Use a digital multimeter on your battery pack and input the exact voltage into the calibration tab to sync the controller's internal ADC.
