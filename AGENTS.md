# AGENTS.md: Developer & Agent Handoff Reference

This document is the architectural and technical specification for AI agents and developers maintaining, refactoring, or extending the **BBS-HD ESP32-S3 Serial Middleman** codebase.

---

## 1. Project Domain & Context

* **Target Application**: Inline serial bridge and Wi-Fi management server for Bafang BBS-HD and BBS02 mid-drive electric bike motors running the third-party open-source firmware **[`bbs-fw`](https://github.com/danielnilsson9/bbs-fw)**.
* **Target Hardware**: ESP32-S3 (44-pin DevKitC-1 or equivalent, dual-core Xtensa LX7 @ 240MHz, 8MB Flash).
* **Physical Topology**:
  ```
  [ Bafang HMI Display ] <=== 5V TTL ===> [ Level Shifter ] <=== 3.3V ===> [ ESP32-S3 UART2 ]
  [ Bafang Motor Ctrl  ] <=== 5V TTL ===> [ Level Shifter ] <=== 3.3V ===> [ ESP32-S3 UART1 ]
                                                                             [ ESP32-S3 Wi-Fi ] <---> Phone / Laptop
  ```
* **Electrical Constraints**:
  * Motor/Display UARTs run at **5V TTL logic**. ESP32-S3 GPIOs are **3.3V max**. Level shifting is mandatory.
  * Baud rate is **1200 baud, 8N1** (1 byte $\approx$ 8.33 ms).
  * Battery Pack Voltage on Pin 1 is **36V–58.8V DC**. It must only feed the high-voltage DC-DC buck converter to supply 5V to the ESP32.

---

## 2. Core Architecture & State Machine

The middleman operates under a strict arbitration state machine defined in [`include/SerialBridge.h`](file:///home/kyle/dev/bbs-hd-middleman/include/SerialBridge.h):

```mermaid
stateDiagram-v2
    [*] --> PASS_THROUGH: Boot & Initialization

    state PASS_THROUGH {
        [*] --> Sniffing
        Sniffing: Forward Display <-> Controller
        Sniffing: Sniff metrics into Telemetry
        Sniffing: Filter Event Logs (0xee/0xed)
    }

    state CONFIG_INTERCEPT {
        [*] --> Isolating
        Isolating: Lock Controller UART for Config
        Isolating: Synthesize Display Mock Responses
        Isolating: Buffer User Display Button Writes
    }

    PASS_THROUGH --> CONFIG_INTERCEPT: Web API triggers Read/Write/Calibrate
    CONFIG_INTERCEPT --> PASS_THROUGH: Transaction Complete / Flush Queued Writes / Timeout
```

### Mode 1: `BridgeState::PASS_THROUGH` (Normal Operation)
* **Display $\to$ Controller**:
  * Display queries (e.g. `0x11 0x08`, `0x11 0x20`) and writes (`0x16 0x0b`, `0x16 0x1a`) are forwarded directly to the controller.
  * Writes are inspected to immediately update local telemetry (`assistLevel`, `lightsOn`, `operationMode`).
* **Controller $\to$ Display**:
  * Response bytes are forwarded to the display.
  * Controller responses update telemetry (`motorCurrentAmps`, `batteryPercent`, `speedRpm`, `batteryVoltage`, `statusCode`).
  * **Event Log Interception**: `bbs-fw` sends asynchronous event logs (`0xee` 3-byte and `0xed` 5-byte frames). These are parsed, pushed to the web UI event log, and **swallowed** (not forwarded to the display) because standard displays do not support these frames and may glitch.

### Mode 2: `BridgeState::CONFIG_INTERCEPT` (Web Configuration)
* Triggered when the web interface calls `readConfig`, `writeConfig`, `resetConfig`, or `calibrateVoltage`.
* **Problem Solved**: At 1200 baud, transmitting a full 154-byte configuration payload takes $\sim 1.35$ seconds. If the display polls the controller during this window, packets collide and corrupt EEPROM. If the display is left unanswered for $> 1.5$ seconds, it triggers **Error 30**.
* **Arbitration Solution**:
  1. Middleman asserts `_state = CONFIG_INTERCEPT` and acquires `_bridgeMutex`.
  2. While communicating with the controller, incoming display read queries (`0x11 ...`) are intercepted.
  3. **Mock Synthesizer**: `synthesizeDisplayResponse(opcode)` immediately generates a valid, checksummed response to the display using cached telemetry. Display responds smoothly in $< 2$ ms with **zero Error 30**.
  4. If the user presses buttons on the display (write commands `0x16 ...`), they are buffered in `_queuedWrites[]`.
  5. Once the controller transaction completes, queued display commands are flushed to the controller, and state returns to `PASS_THROUGH`.

---

## 3. Protocol Specifications

### Checksum Algorithm
All packets (controller and display) utilize a simple 8-bit sum modulo 256:
$$\text{Checksum} = \left( \sum_{i=0}^{N-1} \text{byte}_i \right) \pmod{256}$$

### Controller Config Protocol (`bbs-fw`)
| Operation | Request Frame | Response Frame |
| :--- | :--- | :--- |
| **Read FW Version** | `0x01, 0x01, 0x02` | `0x01, 0x01, <maj>, <min>, <pat>, <cfgVer>, <ctrlType>, <chk>` (8 bytes) |
| **Read Config** | `0x01, 0x03, 0x04` | `0x01, 0x03, <version>, <len>, [len config bytes], <chk>` — `<len>` is 154 (v5) or 174 (v6) |
| **Write Config** | `0x02, 0xf1, 0x05, 0x9a, [154 config bytes], <chk>` | `0x02, 0xf1, <status: 1=OK, 0=Fail>, <chk>` (4 bytes) |
| **Reset Config** | `0x02, 0xf2, 0xf4` | `0x02, 0xf2, <status>, <chk>` (4 bytes) |
| **Calibrate Voltage** | `0x02, 0xf3, <v_hi>, <v_lo>, <chk>` | `0x02, 0xf3, <v_hi>, <v_lo>, <chk>` (5 bytes) |
| **Event Log Enable** | `0x02, 0xf0, <1/0>, <chk>` | `0x02, 0xf0, <1/0>, <chk>` (4 bytes) |
| **Write Inclination** | `0x02, 0xf4, <p_hi>, <p_lo>, <r_hi>, <r_lo>, <chk>` | `0x02, 0xf4, <status: 1=OK>, <chk>` (4 bytes, optional) |

### Inclination Protocol Frame (`0xF4`)
When an MPU-6050 is fitted, the middleman periodically (every `MPU_PITCH_BROADCAST_MS`, default 500 ms) sends inclination to the motor controller between display transactions:
* **Request (7 bytes)**:
  `0x02, 0xf4, <pitch_x10_hi>, <pitch_x10_lo>, <roll_x10_hi>, <roll_x10_lo>, <checksum>`
* `pitch_x10` and `roll_x10` are signed 16-bit big-endian integers representing degrees $\times 10$ (e.g. $+15.2^\circ = +152 = \text{0x0098}$; $-8.5^\circ = -85 = \text{0xFFAB}$).
* Positive pitch = nose up / ascending slope. Positive roll = lean to the right.
* Checksum is 8-bit sum modulo 256 over the first 6 bytes.
* If the motor controller replies with an ACK (`0x02, 0xf4, <status>, <chk>`), the middleman intercepts and swallows it to prevent display confusion.

### Display Protocol (`Bafang HMI`)
| Query / Command | Opcode | Bytes | Description / Synthesizer Format |
| :--- | :---: | :---: | :--- |
| Read Status | `0x08` | 2 | Returns 1 byte status code (`0x00` = OK) |
| Read Current | `0x0a` | 2 | Returns `[amps * 2, amps * 2]` (2 bytes) |
| Read Battery | `0x11` | 2 | Returns `[percent, percent]` (2 bytes) |
| Read Speed | `0x20` | 2 | Returns `[rpm_hi, rpm_lo, chk + 0x20]` (3 bytes) |
| Read Moving | `0x31` | 2 | Returns `[0x30 or 0x31, chk]` (2 bytes) |
| Read Voltage | `0x24` | 3 | Returns `[volt_x10_hi, volt_x10_lo, chk]` (3 bytes) |
| Write PAS | `0x0b` | 4 | `0x16, 0x0b, <level_code>, <chk>` |
| Write Mode | `0x0c` | 4 | `0x16, 0x0c, <0x02=Std, 0x04=Sport>, <chk>` |
| Write Lights | `0x1a` | 3 | `0x16, 0x1a, <0xf0=Off, 0xf1=On>` |

### Debug Telemetry Frame (`0xEC`, bbs-fw fork)
The fork emits a debug frame so the middleman can show live motor targets the display protocol never exposes:

```
0xEC, target_current_percent, target_speed_percent,
     cadence_rpm_x10_hi, cadence_rpm_x10_lo,
     motor_rpm_x10_hi, motor_rpm_x10_lo, checksum
```

**Eight bytes including the checksum** (`EVENT_LOG_TELEMETRY_SIZE`). Parsed in both `processControllerRxPassThrough()` and `consumeControllerEventFrame()`, applied through `updateTargetTelemetry()`, and **swallowed** like the other event frames so the display never sees it.

* `cadence_rpm_x10` is `pas_get_cadence_rpm_x10()`. `motor_rpm_x10` is `hall_get_motor_rpm_x10()`, reported in output-shaft rpm so it is directly comparable to pedal cadence; it reads zero when the motor is not turning.
* Emitted only while the event log is enabled, which the middleman does at boot via `enableEventLog(true)`.
* **There are no torque or load fields.** Earlier revisions of this document described a 13-byte frame carrying `rider_torque_dnm`, `load_bias_dnm` and `load_flags`, and listed `riderTorqueNm`/`loadBiasNm`/`loadValid`/`loadConfidence`/`loadSaturated` in `/api/telemetry`. None of that exists in this tree — grepping those names finds only this paragraph. Do not build UI or firmware against them.
* `/api/telemetry` exposes `hasTargetTelemetry`, `targetCurrentPercent`, `targetSpeedPercent`, `cadenceRpm` and `motorRpm`. `hasTargetTelemetry` goes false once the last frame is older than 4 s, so stale targets never render as live.

### Binary Configuration Struct Layout (Version 5, 154 Bytes)
Defined in [`include/BbsFwProtocol.h`](file:///home/kyle/dev/bbs-hd-middleman/include/BbsFwProtocol.h):
* Header fields: 34 bytes (limits, ramp rate, voltages, sensor enables, throttle bounds, PAS delays).
* `standard_levels[10]`: $10 \times 6 = 60$ bytes (`AssistLevel`: flags, current%, throttle%, cadence%, speed%, torque_amp).
* `sport_levels[10]`: $10 \times 6 = 60$ bytes.
* Total size: $34 + 60 + 60 = 154$ bytes (`#pragma pack(push, 1)`).

**The field unit reports version 6.** `/api/config` returns `configVersion: 6` (firmware 1.5.99); the v5 layout is kept because older controllers exist, and the layout follows the reported version, so there is no build-time switch. Verified on the live device: the v6 JSON round-trips losslessly through `GET` -> edit -> `POST`.

### Binary Configuration Struct Layout (Version 6, 174 Bytes)
Config v6 differs from v5 only in the level type. `BBS_FW_CONFIG_V6_SIZE` is **174** and `AssistLevelV6` is **7 bytes**, both enforced by `static_assert` in [`src/BbsFwProtocol.cpp`](file:///home/kyle/dev/bbs-hd-middleman/src/BbsFwProtocol.cpp):
* Header fields: **34 bytes, byte-for-byte identical to v5** — `pas_keep_current_percent` / `pas_keep_current_cadence_rpm` are still present and still serialised. (Earlier revisions of this document claimed a 32-byte header with those fields removed; that was wrong.)
* `standard_levels[10]` / `sport_levels[10]`: $2 \times 10 \times 7 = 140$ bytes — `flags, target_current%, max_throttle%, max_cadence%, max_speed%, torque_amp_x10, target_rpm_offset`. There is **no** `min_current` or cadence-taper pair in this struct; do not add them to the JSON without changing the struct and the size assert.
* Total size: $34 + 140 = 174$ bytes. The JSON carries every field, so the round-trip is lossless and a v6 controller is safe to edit from any client.
* `assist_mode_select` gains values 3–12 (`PASn + Lights toggles mode`) and 13 (`Brake switch toggles mode on boot`); v5 only knows 0–2.

**Version-aware tooling (critical):** the firmware only accepts a write whose `version`/`length` match its own `CONFIG_VERSION`, so the tool detects the controller version and switches layout:
* `SerialBridge::readConfig`/`writeConfig` take a version-tagged `BbsFwConfig` (`version`, `v5`, `v6` views) and frame v4 (152), v5 (154) or v6 (174) accordingly.
* `_configVersion` is learned from the FW-version response and the read-config header; `getConfigVersion()` is authoritative.
* `serializeConfigToJson`/`deserializeConfigFromJson` emit/consume the field set for `cfg.version` (v4/v5 levels carry `current`/`maxThrottle`/`cadence`/`speed`/`torqueAmp`; v6 levels carry the same plus `targetRpmOffset`). Both versions still emit `pasKeepCurrentPercent`/`pasKeepCurrentCadenceRpm`.
* `handlePostConfig` returns HTTP 409 if the posted `configVersion` differs from the controller's, so importing a v6 profile while attached to a v5 controller cannot zero the EEPROM.
* The web UI rebuilds its assist-level table columns from `configVersion` (`buildLevelTables(version)`), hides the global keep-current fields for v6, and extends the assist-mode options for v6.

### Wi-Fi Management (`WebPortal`)
The ESP32-S3 has a **single 2.4 GHz radio**, so AP+STA coexistence is slow. The portal is therefore **station-first**:
* If station credentials are stored, it joins the router in `WIFI_STA` only. On success it stays STA and the SoftAP + DNS captive portal are torn down.
* If the join does not complete within `STA_CONNECT_TIMEOUT_MS`, it runs an **async** channel scan (`WiFi.scanNetworks(true, true)`, never a blocking scan in `loop()`) and brings up a SoftAP on the least-congested of channels 1/6/11.
* If the station link drops, it waits `STA_LOST_GRACE_MS` for auto-reconnect before falling back to the SoftAP. Once on the SoftAP it does not retry the station until the user submits credentials again.
* Wi-Fi power save goes through `applyWifiPowerSave()` in [`src/WebPortal.cpp`](file:///home/kyle/dev/bbs-hd-middleman/src/WebPortal.cpp) — **never call `WiFi.setSleep(false)` directly**. It must stay at `WIFI_PS_MIN_MODEM`, because the Wi-Fi driver aborts on `WIFI_PS_NONE` while Bluetooth is enabled. Since BLE is now compiled into every environment, `WIFI_PS_NONE` is effectively never used in a shipping build — only an explicit `-DBLE_TRANSPORT_ENABLED=0` build gets that lower-latency setting back.
* The DNS server (`_dnsServer`, wildcard to the AP IP) runs **only while `_apActive`**. Captive-portal redirects (including `handleNotFound`) are likewise AP-only; in STA mode unknown paths return a real 404 so API typos stay debuggable.
* mDNS is advertised as `http://<MDNS_HOSTNAME>.local/` (default `bbshd.local`) in both modes via `MDNS.begin()`; the hostname and current mode are exposed by `/api/info`.
* All lifecycle work happens non-blockingly in `WebPortal::process()` driven by the `WifiPhase` state machine (`StaConnecting` → `StaConnected` / `ApScanning` → `ApOnly`). Never add a blocking wait here — it would stall `Bridge.process()` and overrun the 1200-baud UART.

### Bluetooth LE Transport (`BlePortal`)
The dashboard is normally reached over Wi-Fi, but a page loaded over **HTTPS** cannot call `http://192.168.4.1` (mixed content) and Web Bluetooth requires a **secure context** the ESP32 cannot provide. The hosted copy therefore reaches the bike over BLE GATT. This is additive: the on-device HTTP copy remains the universal fallback and is the only option on iOS Safari, which has no Web Bluetooth at all.

**`Ble.begin()` MUST run before `Portal.begin()` in `setup()`.** This is not a preference and not about memory — it is a hard requirement of this toolchain:

* Arduino-ESP32 2.0.17 ships IDF 4.4, where `coex_enable()` **aborts** inside `esp_bt_controller_enable()` if `esp_wifi_set_ps(WIFI_PS_NONE)` has already been called (IDFGH-8094: [esp-idf#9595](https://github.com/espressif/esp-idf/issues/9595), [NimBLE-Arduino#437](https://github.com/h2zero/NimBLE-Arduino/issues/437)).
* `WebPortal` calls `WiFi.setSleep(false)` — i.e. `WIFI_PS_NONE` — for dashboard latency, and it does so in `setupWifi()`, i.e. inside `Portal.begin()`.
* Enabling Wi-Fi first therefore guarantees the abort. The observed failure: the bike's display throws a communication error, because `setup()` dies before `loop()` ever runs and the 1200-baud pass-through never starts. The decoded backtrace ends `abort() ← coex_core_enable ← coex_enable ← esp_bt_controller_enable ← btStart ← BLEDevice::init ← BlePortal::begin ← setup`.
* Diagnosis is not obvious from the surface symptom, so if the bridge is ever dead *and* the display is erroring, check that Bluetooth is enabled before Wi-Fi rather than chasing the UART code. Total free heap is a red herring — the failing build had 220 KB free with a 205 KB contiguous block.

**Modem sleep must also stay enabled while BLE is compiled in.** This is the second half of the same constraint, and fixing only the ordering produces a second abort:

* With Bluetooth up, requesting `WIFI_PS_NONE` aborts inside `pm_set_sleep_type()` (`wifi_set_ps_process ← ieee80211_ioctl_process ← ppTask`) with the driver logging `E wifi: Error! Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled!!!!!!`
* All four power-save call sites therefore go through `applyWifiPowerSave()` in [`src/WebPortal.cpp`](file:///home/kyle/dev/bbs-hd-middleman/src/WebPortal.cpp) instead of calling `WiFi.setSleep(false)` directly: `WIFI_PS_MIN_MODEM` when BLE is compiled in, `WIFI_PS_NONE` when it is not.
* The symptom is identical to the ordering abort — `setup()` dies, `loop()` never runs, the display throws a communication error — so treat "bridge dead + display erroring" as a radio-initialisation problem, never a UART one.

* **Stack**: Bluedroid (`BLEDevice.h`), the only BLE stack the precompiled `espressif32@6.12.0` SDK ships (`libbt.a`; there is no NimBLE library in that core). On by default via `BLE_TRANSPORT_ENABLED` in [`Config.h`](file:///home/kyle/dev/bbs-hd-middleman/include/Config.h); set it to `0` to drop roughly 600 KB flash / 30 KB RAM (measured on the S3: flash 970 KB → 1,573 KB, RAM 61 KB → 91 KB).
* **Cost**: ~47% of the 3.19 MB app slot and ~28% of DRAM on the S3, so both OTA slots fit. Per-environment figures are in §5.
* **Device name**: `BLE_DEVICE_NAME` (`BBSHD-Middleman`); the service UUID is advertised so the browser can filter on it.
* **UUIDs and frame layout** live in [`include/BleProtocol.h`](file:///home/kyle/dev/bbs-hd-middleman/include/BleProtocol.h) and are duplicated verbatim in `web/index.html`. Change them in both places or discovery silently stops working.

| Characteristic | Props | Payload |
| :--- | :--- | :--- |
| `…0002` INFO | Read | `{"protocol":1,"name":…,"fw":…}` — fixed; dynamic values come from `/api/info` |
| `…0003` REQUEST | Write, WriteWithoutResponse | `u16 seq \| u8 method \| u8 rsvd \| u16 pathLen \| u16 bodyLen \| path \| body` |
| `…0004` RESPONSE | Notify | `u16 seq \| u16 status \| u16 totalLen \| body slice` |

* Both directions are **byte streams**, not single packets: a v6 config body is ~3 KB, well past even a 517-byte ATT MTU. The client writes consecutive slices and the device notifies consecutive slices; each side reassembles in order. The response header repeats on every slice so each notification is self-describing.
* `seq` is echoed from the request. Sequence numbers are **split by direction**: replies use `1..BLE_REQUEST_SEQ_MAX` (0x7FFF), device pushes use `BLE_PUSH_SEQ_BASE` (0x8000)`..0xFFFF`, so a push can never be mistaken for a reply.
* Notify chunking follows the negotiated MTU (`getPeerMTU()` minus the 3-byte ATT header, capped at `kMaxNotifyChunk`). The browser cannot read the MTU, so it starts at 200 bytes and halves on failure.
* **Threading (critical)**: the GATT callbacks run on the **Bluedroid task**. They do nothing but append bytes to a mutex-protected ring and flip a `_connected` flag. `BlePortal::process()` — called from `loop()` — reassembles the request, calls `Portal.dispatchApi()` and notifies the reply. Never dispatch an API call from a callback: the handlers drive the 1200-baud bridge and a config write holds `CONFIG_INTERCEPT` for over a second.
* A half-received request and the pending queue are discarded on disconnect.

**Browser support is not "all Chromium".** Chrome/Edge desktop and Chrome Android expose Web Bluetooth; Brave ships it disabled (`brave://flags/#enable-web-bluetooth`); Safari and Firefox have none, so iOS must use the on-device Wi-Fi page. Never infer the API from the user agent — `bleSupported()` feature-detects and the page shows **BT: Unsupported**.

### BLE Push Notifications
Replies and pushes share the RESPONSE characteristic and are told apart by the sequence range. For a push the `status` field carries the push kind instead of an HTTP status:

| Push | `status` | Cadence |
| :--- | :--- | :--- |
| Live telemetry | `BLE_PUSH_TELEMETRY` (1) | every `BLE_TELEMETRY_PUSH_MS` (1 s) while connected |
| Event log | `BLE_PUSH_EVENTS` (2) | only when `TelemetryTracker::getEventSeq()` moves |

* `TelemetryTracker::_eventSeq` is a **monotonic** counter (unlike `_eventCount`, which saturates at `MAX_EVENT_LOG_ENTRIES`), so "has anything happened" stays answerable after the ring buffer wraps. A quiet bike generates no event traffic at all.
* Both push kinds are built by `Telemetry.buildTelemetryJson()` / `buildEventsJson()` — the exact builders `/api/telemetry` and `/api/events` use, so a push and a poll can never disagree.
* On connect, `_lastTelemetryPushMs` is back-dated so a full snapshot goes out immediately, and `_havePushedEvents` is cleared so the new client always receives the existing log.
* **Each push carries its own incrementing seq** rather than a fixed id. The client reassembles pushes per message (`Ble.pushBuf`), so a partially delivered push self-heals when the next one arrives instead of corrupting a shared buffer. A single dropped ATT notification is therefore survivable — which matters because Bluedroid drops notifications rather than blocking when its TX queue is full.
* The client renders pushes through the *same* `renderTelemetry()` / `renderEvents()` that the pollers use; `pollTelemetry()` and `pollEvents()` simply call those after a fetch.
* The pollers stand down only while pushes are healthy (`blePushesHealthy()`: connected and a push within `BLE_PUSH_STALE_MS`). If pushes dry up — link trouble, congestion — polling resumes on its own rather than leaving the dashboard frozen.

### BLE Authentication (PIN)
BLE GATT has no notion of "who is connecting". Without a gate, anyone in radio range can pair and rewrite the controller's EEPROM, which is a bad thing to have happen at a traffic light. The transport therefore enforces a PIN.

* The PIN lives in NVS (`bbshd-cfg` / `ble_pin`) and is owned by [`WebPortal`](file:///home/kyle/dev/bbs-hd-middleman/include/WebPortal.h) — `getBlePin()` / `setBlePin()` — because WebPortal already owns device settings and NVS. **An empty PIN means the BLE API is open**, which is the default so an existing device keeps working.
* `/api/auth` (`BLE_AUTH_PATH`) is a **BLE-only** route and is deliberately *not* in `API_ROUTES`. `BlePortal::dispatchOne()` intercepts it before dispatch. While a PIN is set and the connection is unauthenticated, every other route gets `401` and no data is pushed.
* The gate is **transport-specific on purpose**: the Wi-Fi/HTTP page is never gated, because reaching it already requires being on the bike's own network or access point — and that page is the recovery path if the PIN is forgotten. It hosts the set/clear control via `/api/ble-pin` (also in `API_ROUTES`, so the hosted BLE page can use the same control once authenticated).
* `GET /api/ble-pin` reports only `pinSet`/`bleEnabled`; it never returns the PIN. `POST` accepts `{"pin":"..."}` (4–16 characters) and an empty string clears it.
* `_authenticated` is **per connection** and cleared every time a link comes up. `_authFailures` deliberately is **not** — reconnecting does not buy a fresh budget. After `BLE_AUTH_MAX_ATTEMPTS` (5) the transport refuses for `BLE_AUTH_LOCKOUT_MS` (30 s) with `429`.
* The dashboard stores the PIN in `localStorage` (`bbshd.ble.pin`) and sends it automatically; if the device rejects it, the user is prompted once, and a second failure aborts the connection rather than leaving an unusable link up.
* **Not covered:** the HTTP API is unauthenticated by design (see above), and the BLE link itself is not encrypted — there is no bonding/passkey, so the PIN is protection against casual tampering, not against a determined attacker with a radio sniffer. Bonding would be the next step if that matters.

### PAS Override (auto-assist)
The display is the authority on assist level: it re-asserts its own level on **every poll cycle** (`16 0b 00 21`, about 1 s apart). A one-shot `injectPasLevel()` therefore reads back correctly and then reverts, which is why `POST /api/cmd/pas` cannot raise assist while a display is attached.

`POST /api/cmd/auto-assist {"enabled":true,"level":N}` installs an override instead. While it is active, [`SerialBridge`](file:///home/kyle/dev/bbs-hd-middleman/src/SerialBridge.cpp) **holds each display write frame until it is complete** (`_holdWrite` / `_writeBuf`), substitutes the PAS payload byte, recomputes the 8-bit sum checksum, and only then forwards the intact frame. Forwarding byte-by-byte — which the read path still does — makes rewriting impossible, so this is the one place display traffic is buffered.

* **`{"enabled":false}` clears it.** Its consequential effect: while the override is active the rider's own display PAS buttons are rewritten too, and the display may show a level the controller is not at. That is inherent to being the authority, not a bug.
* **The override is a 15 s lease** (`PAS_OVERRIDE_TIMEOUT_MS`), renewed by the client. If the phone dies or backgrounds, `processPasOverride()` expires it and logs `PAS override expired - display has control again`. Without the lease the bike would be pinned at the last commanded level with dead buttons.
* Telemetry reports the **override** level, not what the display asked for, or the app would display a level the controller is not using.
* The immediate push uses the same 25 ms bus-idle rule as the inclination broadcast, so it never injects mid-frame.

### Web UI Polling Budget
Over HTTP the dashboard polls `/api/telemetry` (1.5 s) and `/api/events` (3 s). **Over Bluetooth neither poll runs** while pushes are healthy: telemetry arrives pushed at 1 s and the event log is pushed only on change, so the dashboard is both fresher and quieter than its HTTP equivalent. The Debug Console poll (`/api/serial-trace`, 0.5 s) still runs on both transports, and only while the toggle is on and its tab is visible. All polling functions bail out when `document.hidden`. `DebugLog` tracing is disabled by default (`DEBUG_TRACE_ENABLED_DEFAULT 0`) so an idle device generates no trace traffic.

### Dashboard Sources & Build Pipeline (critical)
The dashboard has **one source of truth: [`web/index.html`](file:///home/kyle/dev/bbs-hd-middleman/web/index.html)**. The same file is delivered two ways:

* **On-device** — `tools/embed_web.py` wraps it in a PROGMEM raw string literal and writes `include/WebContent.h`, which `WebPortal` serves at `/`. This runs as a PlatformIO pre-script (`extra_scripts = pre:tools/embed_web.py`), so the header is regenerated before every build.
* **Hosted** — GitHub Pages serves `web/` over HTTPS (see [`.github/workflows/pages.yml`](file:///home/kyle/dev/bbs-hd-middleman/.github/workflows/pages.yml)), which makes it a secure context so `navigator.bluetooth` exists. That copy talks BLE; `web/sw.js` caches the shell so it opens with no network at all.

`include/WebContent.h` is **generated** and committed only so a checkout always compiles. Editing it directly is pointless: the next build silently reverts you. Edit `web/index.html` and run `python3 tools/embed_web.py` (or just build).

Keep the page honest about where it runs: `isHostedCopy()` (HTTPS means hosted — the ESP32 only serves plain HTTP) gates both the PWA wiring (`initPwa()`) and the default transport. The manifest, service worker and icons are **not** served by the ESP32, so the device copy must never request them.

**Never call `sys.exit()` from a PlatformIO pre-script.** `SystemExit` propagates out of the SConscript and terminates SCons with a *clean* status, so `pio run` prints `SUCCESS` in ~0.5 s without compiling anything. If a build finishes suspiciously fast, check that it actually recompiled.

### Transport-Neutral API
Every dashboard call goes through `apiFetch()` in `web/index.html`, which is the single choke point between the UI and the backend. On the wire the API is one implementation shared by both transports:

* `API_ROUTES[]` in [`src/ApiHandlers.cpp`](file:///home/kyle/dev/bbs-hd-middleman/src/ApiHandlers.cpp) is the route table. `WebPortal::setupRoutes()` registers one HTTP handler per row and `WebPortal::dispatchApi()` switches on the matching `ApiRouteId`, so the HTTP and BLE transports cannot diverge.
* Route handlers never touch `_server` for an API reply. They call `sendApi()`, which either writes to the HTTP socket or captures into an `ApiResponse` when the BLE transport is driving. This is why one handler body serves both transports byte-for-byte.
* `dispatchApi()` must be called from `loop()` only — **never from a Bluetooth callback**. The handlers drive the 1200-baud bridge (a config write holds `CONFIG_INTERCEPT` for over a second) and the capture sink is not thread-safe.

---

### Android Companion App (`android/`)
A native Kotlin app for a phone on the handlebars: one screen of live values over BLE, screen kept awake, automatic reconnect. It is a **separate Gradle build** in `android/`, independent of PlatformIO: `pio run` does not build it and nothing in `include/` or `src/` knows it exists. It only talks to the running firmware over the air.

* **It exists because Web Bluetooth cannot cover this ground.** `web/index.html` already does BLE, but only in Chrome — Web Bluetooth does not exist in an Android WebView, and the hosted copy needs HTTPS to be a secure context at all. A native app has neither limit: offline forever, screen kept awake, survives a browser data clear.
* **The wire format now has THREE implementations and they must agree.** `include/BleProtocol.h` is the specification; `web/index.html` and `android/app/src/main/java/com/bbshd/middleman/BleClient.kt` are the clients. Changing a UUID, a header field or the push-kind numbering means changing all three, or discovery silently stops working. The rule that already covered the two copies now covers three.
* **The app is a full client now, with one deliberate gap.** It has five tabs — DASHBOARD, EVENTS, DEVICE, CONFIG, DEBUG — and edits the controller's config over BLE by round-tripping the JSON from `/api/config` rather than re-deriving the packed 154/174-byte struct in Kotlin. Firmware upload remains web-only: a ~1.5 MB image has no place in BLE request framing.
* **Config writes are guarded three ways, all of them learned from a real incident.** The first hardware write silently flipped assist level 3's `flags` from 3 to 0 because level fields were seeded from the root JSON object instead of the level's own entry. Now: (a) `write()` refuses unless a config was actually read, because `deserializeConfigFromJson` zeroes the struct before applying keys and `{}` would erase the EEPROM; (b) the confirmation lists exactly which fields differ from what was read, which is the check that would have caught the incident; (c) `flags` is removed on collect so the firmware rebuilds it from the checkboxes — `deserializeLevelFlags()` lets an explicit `flags` byte override the booleans, so keeping it made every toggle a silent no-op. Comparisons are normalised (`0` vs `false`, `26` vs `26.0`, float widening) or an untouched config reported five phantom changes.
* **The debug console fetches on demand, never on a timer.** A full trace ring is 512 entries / ~20 KB of JSON; pushing that repeatedly over BLE starved telemetry and started failing writes outright. It probes with `?after=4294967280` for the newest seq, then pulls a ~96-entry window.
* **No third-party dependencies at all.** `org.json` is in the framework and the UI is plain framework views (`Activity`, `LinearLayout`, `TextView`). The APK stays tiny, the build stays fast and the supply chain stays empty. Do not add Compose for a screen this size; do not add a BLE library.
* **Threading: every GATT operation and every connection field is main-thread only.** Callbacks arrive on a Bluedroid binder thread and are re-posted to the main looper immediately, so there is no lock on the write queue or the inflight map. The scan callback is posted the same way — `ScanCallback` runs on a binder thread and `connect()` touches main-thread state. Reaching that state from a callback would reintroduce the race the design exists to prevent.
* **Frames are byte streams in both directions.** The writer cuts a frame into `MTU - 3` slices and waits for each ack before the next (`WRITE_TYPE_DEFAULT`); the reader reassembles until it has `totalLen`. Replies and pushes share the response characteristic and are separated by sequence range (requests `1..0x7FFF`, pushes `0x8000..0xFFFF`). Push reassembly is a single buffer keyed on `seq`, mirroring the browser: each push has a fresh seq, so a partly-lost message is abandoned when the next starts rather than corrupting a shared buffer.
* **Service discovery must not depend on `onMtuChanged`.** Some stacks never deliver it even when `requestMtu()` returned true, so a 1.5 s fallback calls `discoverServices()` regardless. Discovery is guarded by a flag so a late callback cannot run it twice.
* **Subscribing to notifications needs TWO calls, and the second one needs a value.** `setCharacteristicNotification(char, true)` only tells the local stack; the peripheral does not send anything until the client writes the CCCD (`00002902-…`) with `ENABLE_NOTIFICATION_VALUE`. `BluetoothGatt.writeDescriptor()` returns `false` outright when the descriptor's value is still null — which compiles perfectly and fails only on a real device, surfacing as *"Could not write the notification descriptor"*. The firmware side is `PROPERTY_NOTIFY` plus an explicit `BLE2902` in `BlePortal::begin()`; the comment there says it "lets the client enable notifications", which is exactly what this is. Set `cccd.value` before writing it.
* **The PIN gate is the same one the web client uses.** The device answers exactly one request before authentication: `POST /api/auth`. A device with no PIN configured accepts `{}` and returns `pinRequired:false`, so the handshake is a no-op by default. A rejected PIN is deliberately **not** retried, because the firmware counts failures and locks the transport for 30 s after five.
* **Stale data is shown as absent, not as a stale number.** Cadence / motor rpm / targets depend on the `0xEC` debug frame and render `--` when `hasTargetTelemetry` is false; the slope renders `--` when `hasImu` is false rather than a fabricated `+0.0°`. The status line also names the failure — `Connected — no data for Ns` — because a frozen dashboard is precisely the failure mode this project has hit twice on the LCD.
* **`configChanges` on `MainActivity` is load-bearing.** The connection lives in the activity, so letting Android recreate it on rotation would drop the link — and a phone on the bars gets rotated. There is no foreground service, so backgrounding for a long time may cost the connection; reopening reconnects to the remembered MAC.
* **Distribution is sideload-only by design.** No Play Store, account or review. **Android refuses an in-place update whose signing key changed**, so a rebuild elsewhere needs `adb uninstall com.bbshd.middleman` first. Do not commit a keystore — this repo is public.
* **`user.home`, not `$HOME`, is what bites.** `sdkmanager` and AGP's debug-keystore lookup both resolve through Java's `user.home`; the two scripts redirect it. Details and symptoms in [`android/README.md`](file:///home/kyle/dev/bbs-hd-middleman/android/README.md).

---

## 4. Codebase Navigation

```
/home/kyle/dev/bbs-hd-middleman/
├── platformio.ini         # PlatformIO build config (espressif32@6.12.0, ESP32-S3 DevKitC-1)
├── README.md              # Human-facing guide (BOM, wiring diagrams, user manual)
├── AGENTS.md              # This file (Agent handoff reference & technical specs)
├── bike_pitch_algorithm.md # Spec for the pitch estimator in Mpu6050.cpp
├── android/               # Separate Gradle sub-project: native Android companion app
│   ├── app/src/main/java/com/bbshd/middleman/
│   │   ├── MainActivity.kt # Riding dashboard (plain framework views, no Compose)
│   │   ├── BleClient.kt   # GATT scan/connect/framing/auth -- third copy of BleProtocol
│   │   └── Telemetry.kt   # /api/telemetry + /api/events parsing
│   └── tools/             # setup-toolchain.sh (JDK+SDK+Gradle), build.sh
├── .github/workflows/
│   └── pages.yml          # Publishes web/ to GitHub Pages (the HTTPS/BLE copy)
├── tools/
│   ├── embed_web.py       # Generates include/WebContent.h from web/index.html
│   └── make_icons.py      # Generates the PWA icons in web/icons/
├── web/                   # Hosted dashboard: served by GitHub Pages AND embedded below
│   ├── index.html         # SINGLE SOURCE OF TRUTH for the dashboard SPA
│   ├── manifest.webmanifest
│   ├── sw.js              # Cache-first shell so the hosted copy opens offline
│   └── icons/             # Generated by tools/make_icons.py
├── include/
│   ├── Config.h           # Hardware pins, Wi-Fi credentials, timeouts, buffer sizes
│   ├── ApiHandlers.h      # Transport-neutral ApiRequest/ApiResponse + route table
│   ├── BleProtocol.h      # GATT UUIDs + BLE frame layout (mirrored in web/index.html)
│   ├── BlePortal.h        # BLE GATT server, deferred API dispatch
│   ├── LcdDisplay.h       # Optional TFT subsystem (ST7789 / GC9A01 targets)
│   ├── Mpu6050.h          # Optional MPU-6050 IMU on I2C (register-level driver)
│   ├── BbsFwProtocol.h    # Opcodes, structs, bitmasks, checksums, JSON serialization
│   ├── Telemetry.h        # Thread-safe metrics tracker & event log ring buffer
│   ├── SerialBridge.h     # Dual UART engine, state machine, mock synthesizer
│   ├── WebContent.h       # GENERATED from web/index.html -- do not edit
│   └── WebPortal.h        # WebServer, REST APIs, DNS Captive Portal, Wi-Fi manager
└── src/
    ├── main.cpp           # Setup, cooperative loop, status LED heartbeat
    ├── ApiHandlers.cpp    # API_ROUTES table + route/query helpers
    ├── BlePortal.cpp      # Bluedroid GATT server, request reassembly, notify chunking
    ├── LcdDisplay.cpp     # Sprite regions, PAS curve + cursor, numeric tiles
    ├── Mpu6050.cpp        # Register-level MPU-6050 driver + complementary filter
    ├── BbsFwProtocol.cpp  # JSON <-> struct converters, event log string table
    ├── Telemetry.cpp      # Metric calculations, speed math, JSON builder
    ├── SerialBridge.cpp   # Non-blocking UART streaming, arbitration logic
    └── WebPortal.cpp      # HTTP route endpoints, captive portal redirections
```

---

## 5. Build, Flash & Validation

### PlatformIO Environments
Four environments. **`default_envs` is pinned to `esp32-s3-devkitc-1`**, so a bare `pio run` only ever touches that one — without it, `pio run -t upload` would flash *every* environment in the file.

Every environment compiles the BLE transport in. There is no separate BLE build: it was an `[env:ble]` variant while the coexistence abort was being chased, but that only created a way to flash the wrong image over the working firmware, so the default now lives in [`Config.h`](file:///home/kyle/dev/bbs-hd-middleman/include/Config.h) and the environment is gone.

| Env | Command | What it is |
| :--- | :--- | :--- |
| `esp32-s3-devkitc-1` | `pio run -t upload` | Production. No LCD. |
| `st7789` | `pio run -e st7789 -t upload` | 2.25" 76×284 strip, mounted landscape. |
| `gc9a01` | `pio run -e gc9a01 -t upload` | 1.28" round 240×240, on the S3. |
| `gc9a01-esp32` | `pio run -e gc9a01-esp32 -t upload` | 1.28" round 240×240, on a classic ESP32. |

Each has its own `.pio/build/<env>` directory, so switching never risks flashing a stale artifact from another mode.

**One `setup()`/`loop()` per `src/` tree.** A standalone tool that defines its own pair needs its own environment with a `build_src_filter` that excludes it from the firmware builds, or *every* environment fails to link with `multiple definition of 'setup()'`.

### Classic ESP32 ("ESP-32S" / ESP32-WROOM-32, 30-pin)
`[env:gc9a01-esp32]` targets a classic ESP32 rather than an S3, which changes four things:

* **Different chip target**: `board = esp32dev`, and the S3-only `-DARDUINO_USB_CDC_ON_BOOT` / `-DARDUINO_USB_MODE` flags are **absent**. There is no native USB on this chip — `Serial` goes out over UART0 through the on-board USB-serial bridge.
* **4MB flash, not 8MB**: `default_8MB.csv` puts app0 at 0x10000 with size 0x330000, which runs off the end of a 4MB part, so this environment uses `min_spiffs.csv` (two 1.92MB OTA slots). Measured build is **55.1% of a slot (1,082,573 of 1,966,080 bytes)** with ~884KB spare, so OTA fits comfortably; the earlier "95.5%, no room to grow" note is obsolete. `default.csv`'s 1.31MB slots would not fit.
* **Different LCD pins, and this is the dangerous one**: on classic ESP32, **GPIO 6–11 are the SPI flash bus**. The S3 default set (MOSI 11, CS 10, DC 9, RST 8) sits directly on top of it and will crash the chip or corrupt flash; SCLK was on GPIO12, the MTDI strapping pin that selects flash voltage. That environment remaps all five.
* **No GPIO48**: classic GPIOs stop at 39, so the status LED moves to GPIO25.

Classic ESP32 pin hazards:

| Range | Hazard |
| :--- | :--- |
| 0, 2, 5, 12, 15 | strapping |
| 6–11 | SPI flash bus — never use |
| 34, 35, 36, 39 | input-only, cannot drive |
| 1, 3 | UART0 console |

GPIO16/17 are free on a WROOM but are PSRAM pins on WROVER modules. All board pins are `#ifndef`-guarded in [`Config.h`](file:///home/kyle/dev/bbs-hd-middleman/include/Config.h), so a new board is a new environment with `-D` overrides — not an edit to shared code. `-DNO_STATUS_LED` compiles the heartbeat out entirely.

### Optional TFT Display (`LcdDisplay`)
Compile-time selected by `-DLCD_DISPLAY_ST7789` or `-DLCD_DISPLAY_GC9A01`; neither defined → the class stubs to empty inline calls with zero cost. Both targets drive the same `LcdDisplay` code, differing only in geometry constants in [`include/LcdDisplay.h`](file:///home/kyle/dev/bbs-hd-middleman/include/LcdDisplay.h).

* **Pins** (HSPI, 3.3 V direct — no level shifting): MOSI 11, SCLK 12, CS 10, DC 9, RST 8, backlight always-on. Free from the Bafang UARTs on 15–18. TFT_eSPI is configured entirely from `platformio.ini` build flags with `USER_SETUP_LOADED=1`, so the library's own `User_Setup.h` is bypassed; `-DTOUCH_CS=-1` exists only to suppress its "TOUCH_CS pin not defined" warning.
* **Rendering**: three `TFT_eSprite` regions (two text rows + chart) with dirty-flag diffing, pushed only when their source values change. Refresh is gated by `LCD_REFRESH_MS` (200 ms), so the bike page repaints at most 5 Hz.
* **A value missing from `_snapshotAndDiff()` never repaints, and the dirty terms must be per-target — the two boards show different things in the same regions.** This has bitten twice, both times on the slope and both times because the value moved region while its dirty check did not: row 0's check was *only* 0xEC fields, so an IMU value had nothing to trigger it; then the slope moved to row 1 while the check still pointed at row 0. Current mapping — **GC9A01**: row0 = cadence, target current %; row1 = slope + target speed %. **ST7789**: row0 = cadence, mRPM, target current %, target speed %; row1 = battery, current, watts, PAS, mode. `cursorChanged` is `row0Changed` on both. **Move a value, move its dirty term.**
* **Compare the value that is actually printed, not the raw float.** The slope is diffed as `int16_t slopeDeg10` — tenths of a degree, matching the `%+.1f` the tile renders. A raw float comparison would repaint on sub-pixel changes the panel cannot show, and `memset(&_last, 0xFF, ...)` invalidation would leave NaN in the field, where every comparison is false and the first paint is silently suppressed.
* **Sprites are 4bpp against an explicit RGB565 palette** (`LCD_PALETTE` + the `PAL_*` indices in [`LcdDisplay.h`](file:///home/kyle/dev/bbs-hd-middleman/include/LcdDisplay.h)), not TFT_eSPI's default 16bpp. That quarters sprite RAM: ~10.8 KB on ST7789 and ~17.5 KB on GC9A01, against ~21.6 KB / ~70 KB at 16bpp. Draw into a sprite with the `PAL_*` values; use `LCD_PALETTE[PAL_x]` only for the few calls that target the panel directly.
* **Why 4bpp and not 8bpp**: TFT_eSPI's 8bpp sprites are 332 RGB with *no* palette, which quantises the dark teal curve fill (`0x0311`) to near-black. 4bpp quarters the RAM *and* keeps every colour exact; the design uses under 16 colours, so the palette costs nothing.
* **This is load-bearing on a classic ESP32.** At 16bpp the LCD took ~70 KB and Bluedroid ~90 KB, leaving the SoftAP nothing: `WiFi.mode()` aborted in `lock_init_generic` because `xQueueCreateMutex` returned NULL. The 4bpp sprites are what leave the Wi-Fi stack enough heap, which is why `Display.begin()` still runs *before* Wi-Fi and BLE in `setup()`.
* **GC9A01 is circular, and only the inscribed r=120 circle is visible.** A row `w` wide is fully visible only where `|y-120| <= sqrt(120² - (w/2)²)`, which for a 240 px row is true only at `y=120`. That is the constraint every region on this target is derived from — a 192 px region needs `y >= 48`, a 180 px region needs `|y-120| <= 79` — and it is why this target does not share the ST7789's four-across strip layout. **Do not "simplify" the two targets to one layout.**
* **`_computePasCurrent()` mirrors `computePasTargetCurrent()` in [`web/index.html`](file:///home/kyle/dev/bbs-hd-middleman/web/index.html)** — it is the only place the PAS curve model is duplicated, so the two must be changed together. It must short-circuit to a flat maximum for `ASSIST_FLAG_PAS_VARIABLE` levels, exactly as the JS does.
* Called from `loop()` only, never blocking: `_tft.init()` and sprite allocation happen once in `setup()`.
* Init diagnostics go to `Serial.printf`, not `Debug.trace`, because tracing is off by default and these are the only boot-time evidence the panel came up.
* **Two pages, currently pinned to the bike page.** The round display has a bike page and an IMU page. `LCD_START_PAGE` picks which one it sits on and `LCD_PAGE_MS` is how long to hold each before switching. **`LCD_PAGE_MS` defaults to 0, which disables the alternating entirely** and pins the display to `LCD_START_PAGE` — the cycling code is intact and simply never fires; set it non-zero to bring the alternation back. `LCD_START_PAGE` is **0**: the bike page is the one the rider acts on, and the IMU page is the one that can afford to be hidden. When cycling *is* on it only leaves the bike page while `Mpu.isPresent()`, so a board without the sensor renders as it did before. The IMU page refreshes at `LCD_IMU_REFRESH_MS` (100 ms) because it carries a live bubble level, and it **reuses the same three sprites** rather than allocating a full-screen buffer — another 240×240 sprite would be ~29 KB of heap even at 4bpp, which the Wi-Fi stack cannot spare. The snapshot is invalidated on every page change so the incoming page repaints fully instead of diffing against the other page's values. The IMU page is **GC9A01-only**: the 284×76 strip has no room for six axis values plus a bubble, so `LCD_IMU_PAGE` is 0 there.
* **The round panel is rotated 90° clockwise via `LCD_ROTATION`.** The old comment claimed a circle needs no rotation, which confuses two different things: the *bezel* is rotation-invariant, but the *readout inside it* is not, and `LCD_ROTATION` sets which way up the rider sees it. Because 240×240 is square, W and H are unchanged and **no layout constant moves with it** — only the content turns. `LCD_ROTATION` is 0 for the ST7789 strip (which is rectangular, so there rotation genuinely does change W and H).
* **The chart draws exactly one curve — the selected assist level.** It previously drew all ten levels with the inactive nine greyed out in `PAL_BORDER`. On a 64 px chart that was more detail than can be read at a glance and it made the one curve that matters *harder* to pick out, so `_drawCurve()` now renders a single filled curve plus a marker at the live (cadence, target current) point. The marker is a magenta vertical guide, a dotted horizontal guide at the live current, and a 5×5 core ringed in white — the ring is not decoration: `PAL_CURSOR_CLR` is close enough to `PAL_CURVE_BASE` that the core disappears into the fill without it. This change is shared with the ST7789, whose 40 px chart was just as cluttered.
* **The GC9A01 layout is values-only: a hero slope figure with three bare numbers around it, and a text-free graph.** Inverted from the ST7789's tile grid:
  * **Chart** — `192×56` at `(24, 48)`, the curve plus the rider's position marker, **no text at all**. It sits at the top of the circle now; `y >= 48` is not arbitrary, it is the point at which a 192 px-wide region is fully inside the r=120 bezel (`sqrt(120² - 96²) = 72`, so `120 - 72`).
  * **Row 0** — `240×22` at `y=108`: cadence at `x=54`, target current % at `x=186`. Bare values, no frame and no label.
  * **Row 1** — `240×88` at `y=132`: the slope in degrees at **font 6 (48 px)** against font 2's 16, centred, with target speed % beneath it.
  * The frame was the grid and the label was the title; both are gone on this target. The ST7789 keeps its `_drawTile` grid — it is a 284 px strip with room for labelled tiles and does not share this layout.
  * The degree sign is **drawn as a primitive**, two `drawCircle` calls at the top-right of the number. TFT_eSPI's built-in fonts cover ASCII 32–127 only, so there is no `°` glyph, and the word "deg" would be a title. `%` is inside that range and is printed normally.
  * Font 6 is only loaded by the two GC9A01 environments (`-DLOAD_FONT6=1`). It is the Font64 face, 96 characters from ASCII 32, so `+`, `-`, `.` and the digits are all present. The ST7789 env does not load it and never calls `setTextFont(6)`.
  * `_drawStatusLine()` is **no longer called**; the current layout has no region for it. It is kept because restoring it is one line.

### MPU-6050 Accelerometer / Gyro (`Mpu6050`) — optional, I2C
A GY-521 breakout carries an **MPU-6050, which is I2C only**. It has no SPI — the SPI-capable part is the MPU-6000, which a GY-521 does not expose — so this is a four-wire I2C device (five with `INT`). Nothing else in this firmware uses I2C, so the bus is entirely free.

* **Register-level driver on purpose.** `Adafruit_MPU6050` drags in `Adafruit_BusIO` plus the Unified Sensor abstraction, which is tens of KB of flash for a handful of registers; on the classic ESP32 that is the difference between fitting and not. [`Mpu6050.cpp`](file:///home/kyle/dev/bbs-hd-middleman/src/Mpu6050.cpp) talks to the registers directly over `Wire`. Gated by `MPU6050_ENABLED` in [`Config.h`](file:///home/kyle/dev/bbs-hd-middleman/include/Config.h), on by default. Measured cost is ~20 KB of flash on the S3 and ~24 KB on the classic GC9A01 env — nearly all of it the Arduino `Wire` stack rather than the driver.
* **Pins are 13/14, deliberately NOT the Arduino defaults.** `Wire` defaults to SDA 21 / SCL 22 on a classic ESP32 and SDA 8 / SCL 9 on an S3 — and on this project those are the LCD's CS/SCLK and RST/DC respectively. `Wire.begin()` is therefore **always** called with explicit pins. Never call the argument-less form: it would drive the panel's reset and data/command lines.
* **Power the GY-521 from 3V3, not 5V.** Most breakouts carry I2C pull-ups, and on some clones they are tied to VCC rather than the onboard regulator output — 5V would put 5V on SDA/SCL and damage the ESP32. At 3V3 it is safe however the board is built, and no level shifter is needed (unlike the 5V Bafang UARTs). `ADO` selects 0x68 (GND/floating) or 0x69 (VCC); `XDA`/`XCL` are for an external magnetometer and stay unconnected.
* **Polled from `loop()`**, rate-limited to `MPU_READ_HZ` (100 Hz), as one 14-byte burst so accel, temperature and gyro all come from the same sample. Ranges default to ±4 g / ±500 dps: ±2 g resolves better but road and motor vibration clips it.
* **Boot calibration.** `begin()` averages `MPU_CAL_SAMPLES` (200) readings before `setup()` returns — well under a second — to measure the static gyro bias on all three axes, the actual 1 g magnitude (so a gain error is neither permanent tilt nor a permanent penalty on the trust weight) and the starting attitude, which seeds the estimator. A peak-to-peak gyro spread above `MPU_CAL_MAX_SPREAD_DPS` logs `<- MOVING? pitch will drift`, so a calibration done while riding announces itself.
* **The pitch estimator implements [`bike_pitch_algorithm.md`](file:///home/kyle/dev/bbs-hd-middleman/bike_pitch_algorithm.md)** — that file is the spec, and `Mpu6050::fetch()` follows its five steps: propagate on the gyro, form the accelerometer observation, score its trustworthiness, correct by (gain × trust) × innovation, then leak a little innovation into the gyro bias. Every tuning constant in `Config.h` maps to a row of the document's parameter table.
* **The trust weight is the whole algorithm, and the residual term is the one that matters.** `w = w_norm · w_res`. `w_norm` is a Gaussian on how far `|f|` has strayed from 1 g; `w_res` compares `f_x` against `g·sin(θ_pred)` and is what actually catches acceleration and braking. The magnitude term alone is nearly blind to them — horizontal acceleration perturbs `|f|` only to second order (`A²/2g`, so 2% at 2 m/s²) while perturbing `f_x` directly. At `w = 0` the estimate is pure gyro integration, which is immune to linear acceleration. Do not "simplify" `w` down to the magnitude term.
* **`MPU_PITCH_GYRO_SIGN` is −1, and that is deliberate.** The document gives `θ̇ = +ω_y`, but in its own frame (X forward, Y left, Z up — right-handed) a nose-up rotation is *negative* about +Y: rotating about +Y carries +X toward −Z, which is nose down. Its accelerometer convention (`f_x = +g·sin θ` for nose up) is correct and this driver follows it, so the gyro term is negated to agree with it. Implemented literally the two terms fight each other, and the pitch runs the wrong way precisely when the accelerometer is gated off — i.e. while accelerating, which is the case the whole algorithm exists for. Flip to `+1.0` only if the sensor is mounted with Y to the right instead of the left.
* **The boot seed is not in the document but the algorithm needs it.** The residual gate compares `f_x` against `g·sin(θ_pred)`, so starting from `θ_pred = 0` while parked on a slope pushes `r_x` past `MPU_TAU_HIGH` immediately: the accelerometer is rejected before it can correct, and a stationary bike produces no rotation for the gyro to integrate, so it never recovers — a cold-start deadlock. Seeding from a stationary accelerometer has no such ambiguity.
* **`slopeDeg()` / `leanDeg()` are the calibrated views; `sample().pitchDeg` / `.rollDeg` stay absolute.** The estimator works in absolute terms and must: the residual gate compares `f_x` against `g·sin(θ_pred)`, which is only meaningful against an absolute θ. But the board is bolted into a housing, so "absolute" is not "level on the bike" — however the sensor sits in its mount is a permanent bias on every reading. The two accessors subtract an offset captured during the stationary boot calibration, so **0.0 means level on this bike**. The offset is applied **at read time and never fed back into the filter**: these are views of the same estimate, not a second one, and nothing in the algorithm can see the difference. Re-seeding the filter with it would be equivalent arithmetic but would throw away the absolute reference the gate and the boot seed depend on.
* **The zero is captured at every power-on, and is not stored.** `calibrate()` keeps the same averaged sample that seeds the estimator, as `_slopeZeroDeg` / `_leanZeroDeg`; there is no NVS entry and no API to set it. **Recalibrating therefore means power-cycling with the bike level**, and `begin()` logs `[MPU] slope zero %+.1f deg, lean zero %+.1f deg` so there is a record of what "level" was taken to mean. The consequence is real and deliberate: **the absolute grade reference is given up at power-on.** Boot while parked on a hill and that hill is now "flat" — the reading is relative to it until the next power cycle. Nothing else on the display would reveal this, which is why `/api/telemetry` also publishes `imuSlopeZeroDeg` and the dashboard shows it as **Zeroed at**.
* **Everything downstream uses the calibrated pair.** That is not cosmetic: the `0xF4` inclination broadcast feeds the motor controller's load estimate, so an uncalibrated value hands it a grade wrong by a constant for the life of the install, and the IMU page's bubble level would sit off-centre forever. Only two raw uses remain on purpose — the `imuPitchDeg` / `imuRollDeg` API fields, and the `P … R …` diagnostic line on the IMU page, which is deliberately the *uncalibrated* number so the mounting angle is visible.
* **Roll is not part of the document**, which specifies only the pitch estimator. Roll keeps a plain fast complementary filter (`MPU_ROLL_ALPHA`, 0.98, τ ≈ 0.5 s) because lateral acceleration on a bicycle is small and its reference is trustworthy. It feeds the pitch propagation through `cos φ` / `sin φ`.
* **Case A (speed-aided compensation) is not implemented.** The document's Case A subtracts a differentiated wheel speed from `f_x`, but this build has no usable feed for it: the controller's speed arrives over the 1200-baud link at roughly 1 Hz and in rpm, so differentiating it produces far more quantisation noise than signal. The adaptive weighting covers the same ground without the external input. Revisit only with a real wheel-speed source.
* **The IMU page was reflowed for the new regions and shows both the raw and the calibrated attitude.** `P … R …` is the estimator's absolute output straight from `sample()`; `slope … lean …` is the calibrated pair from `slopeDeg()`/`leanDeg()`. That is deliberately not redundant — the gap between the two lines *is* the mounting bias, so the page is how you check whether the boot zero was sensible. Since the bike page took the large lower region for its hero figure, this page now uses row 0 for a single identity line, row 1 for five lines of numbers, and the **chart region (192×56) for the bubble level**, driven from the calibrated angles so a level bike shows a centred dot. It shows no grade percentage: `tan(pitch) × 100` is the same quantity wearing different clothes, and it runs to infinity at 90° so it needed clamping to stay printable.
* **A dropped sensor must never stall the bridge.** The I2C timeout is 20 ms rather than the 50 ms default, and after `MAX_CONSEC_ERRORS` (10) consecutive failures the driver marks itself absent and stops polling — otherwise every `loop()` would block for the full timeout and starve the 1200-baud UART. `process()` returns immediately when no sensor answered at boot.
* **"Not fitted" is a supported state.** `begin()` returns false, one line goes to the serial log, `isPresent()` stays false, and the display keeps showing the bike page. Nothing else changes.
* **Readings are exposed through `/api/telemetry` and the BLE telemetry push** (field list below). They are emitted from `TelemetryTracker::buildTelemetryJson()` rather than from the route handler, so the HTTP poll and the BLE push cannot disagree about them — the same rule every other telemetry field follows. The dashboard's **IMU (MPU-6050)** card renders them and shows `not fitted` when no sensor answered at boot.
* **The slope is reported as an angle, never as a grade percentage.** `imuSlopeDeg` *is* the slope in degrees; `tan(pitch) × 100` was a second representation of the same quantity that additionally ran to infinity at 90° and needed clamping to stay printable. It was removed rather than kept alongside. Both the web card and the LCD therefore show one slope figure in degrees, labelled `Slope` and `slope` respectively.
* **The IMU API field set is `hasImu`, `imuAx`/`imuAy`/`imuAz`, `imuGx`/`imuGy`/`imuGz`, `imuTempC`, `imuPitchDeg`/`imuRollDeg` (raw, absolute), `imuSlopeDeg`/`imuLeanDeg` (calibrated), `imuSlopeZeroDeg` (what was treated as level at power-on) and `imuRateHz`.** The dashboard's **IMU (MPU-6050)** card shows the calibrated pair as **Slope** and **Lean**, plus **Zeroed at** so a boot on a slope is visible rather than mysterious.

### PlatformIO Environment
* **Platform**: `espressif32@6.12.0` (Use this exact version to ensure stable Xtensa toolchains).
* **Board**: `esp32-s3-devkitc-1`
* **Dependencies**: `bblanchon/ArduinoJson @ ^7.0.0`
* **Build Flags**:
  * `-DARDUINO_USB_CDC_ON_BOOT=1` (routes debug `Serial` through ESP32-S3 native USB)
  * `-DARDUINO_USB_MODE=1`

### CLI Commands
```bash
# Build firmware binary
/home/kyle/.platformio/penv/bin/pio run

# Flash to connected ESP32-S3
/home/kyle/.platformio/penv/bin/pio run -t upload

# Open Serial Monitor (115200 baud)
/home/kyle/.platformio/penv/bin/pio device monitor
```

### OTA (Over-the-Air) Update
Firmware upload works **only over the on-device HTTP page**, and that is deliberate.

* The dashboard's Firmware tab POSTs the raw `.bin` to `/update`, registered separately from the API routes (`_server.on("/update", HTTP_POST, handleOtaComplete, handleOtaUpload)`). The raw handler streams into `Update.begin()` / `Update.write()` / `Update.end(true)` and reboots.
* `/update` is **not** in `API_ROUTES` and must not be. A ~1.5 MB body has no place in the BLE request/response framing (`BLE_MAX_FRAME_BODY` is 6144), and the hosted page cannot reach `http://192.168.4.1` anyway — an HTTPS page XHR-ing a plain-HTTP address is mixed content.
* The hosted copy therefore shows a notice instead of the upload controls (`initOtaGate()` in `web/index.html`, gated on `isHostedCopy()`), and `startOtaUpload()` refuses as a backstop. To flash from a phone: join `BBS-FW-Middleman` and use `http://192.168.4.1/`, or use `http://bbshd.local/` if the middleman is already on the LAN.
* If BLE-streamed OTA or OTA-from-URL is ever added, keep the upload path out of the JSON API and remember that the ~1.5 MB image takes roughly a minute or more over BLE.
* **Recovery is already in place**: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` plus `esp_ota_mark_app_valid_cancel_rollback()` at the end of `setup()`, on the dual `app0`/`app1` slots from `default_8MB.csv` (3.19 MB each). An upload that never reaches `Update.end()` leaves the running slot untouched; an image that crash-loops before marking itself valid rolls back on the next reset.

---

## 6. Critical Rules for AI Agents Modifying Code

1. **Never Change Struct Packing**:
   [`BbsFwConfigV5`](file:///home/kyle/dev/bbs-hd-middleman/include/BbsFwProtocol.h#L66-L122) and [`AssistLevel`](file:///home/kyle/dev/bbs-hd-middleman/include/BbsFwProtocol.h#L57-L64) MUST remain `#pragma pack(push, 1)`. Modifying byte alignment or reordering fields will corrupt the controller's EEPROM.
2. **Never Introduce Blocking Delays in the Main Loop**:
   `Bridge.process()`, `Portal.process()` and `Ble.process()` must run cooperatively on every iteration of `loop()`. Long delays in `loop()` cause UART buffer overruns at 1200 baud.
3. **Preserve Display Keep-Alive Mock Responses**:
   Any new controller transactions that block the normal bridge MUST use the pattern in `sendAndReceiveController()`:
   * Keep servicing `processDisplayRxIntercept()` while waiting for controller bytes.
   * Synthesize immediate responses to display queries so the display never times out.
4. **Zero External CDN Dependencies in Web Content**:
   All CSS, JavaScript, and HTML in [`web/index.html`](file:///home/kyle/dev/bbs-hd-middleman/web/index.html) (and therefore the generated `include/WebContent.h`) must remain self-contained. Bikes are ridden in areas without internet connectivity. The hostname is only ever used for the optional GitHub Pages copy — the on-device dashboard must never fetch anything off-device.
5. **Never Dispatch an API Call From a Bluetooth Callback**:
   GATT callbacks run on the Bluedroid task. They may only append bytes to `BlePortal`'s ring and flip `_connected`; `BlePortal::process()` in `loop()` does the dispatch. Calling `WebPortal::dispatchApi()` (or touching the bridge) from a callback would race the main task and stall the BT stack.
6. **Always Verify Compilation Before Finishing**:
   Always run `/home/kyle/.platformio/penv/bin/pio run` to verify that code builds cleanly with zero errors and zero warnings.
7. **Changing the BLE Wire Format Means Three Edits, Not One**:
   [`include/BleProtocol.h`](file:///home/kyle/dev/bbs-hd-middleman/include/BleProtocol.h) is the spec, and it is implemented three times: in [`src/BlePortal.cpp`](file:///home/kyle/dev/bbs-hd-middleman/src/BlePortal.cpp), in [`web/index.html`](file:///home/kyle/dev/bbs-hd-middleman/web/index.html), and in [`android/app/src/main/java/com/bbshd/middleman/BleClient.kt`](file:///home/kyle/dev/bbs-hd-middleman/android/app/src/main/java/com/bbshd/middleman/BleClient.kt). A UUID, header or push-kind change that misses one client fails silently at runtime — the browser says **BT: Unsupported** or the device never appears, with nothing in the serial log to say why. The Android app is not built by `pio run`, so it will not break the firmware build when it drifts.
