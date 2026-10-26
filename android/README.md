# Middleman — Android companion app

A native Android dashboard for the ESP32 middleman. One screen, big numbers,
fed live over Bluetooth LE. This is the thing you mount on the bars; the web
dashboard remains the place for configuration.

**Status:** builds, installs, and speaks the real wire protocol. It has been
compiled and signed here but not yet ridden with a bike attached, so treat the
first ride as the shakedown.

---

## Install it on your phone, without the Play Store

Yes — Android has never required the Play Store. Sideloading is a normal,
supported path; the store only matters when you want to distribute an app to
strangers.

1. Get `middleman-debug.apk` onto the phone (USB cable, Google Drive, email to
   yourself, whatever is easiest).
2. Tap the file in Files / Downloads.
3. Android will say *"For your security, your phone is not allowed to install
   unknown apps from this source"* the first time. Tap **Settings**, allow that
   one app (Files, Chrome, Drive — whichever opened it), and go back.
4. **Install**. Play Protect may warn that the developer is unverified; choose
   *Install anyway*.

Or, with the phone plugged in and USB debugging enabled:

```bash
adb install -r android/dist/middleman-debug.apk
```

That is the whole process. There is no account, no $25 registration, no review
queue, and no waiting.

### Two things that will bite you later

**A different signing key means you must uninstall first.** Android refuses to
update an app in place if the new APK is signed with a different key. This APK is
signed with a debug key generated on the build machine. If you rebuild it
yourself — on this machine, another machine, or in Android Studio — the result is
signed with a *different* key, and installing it over the top fails with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`. Fix:

```bash
adb uninstall com.bbshd.middleman
```

If you want to keep updating in place, generate a release keystore once and sign
every build with it (`keytool -genkeypair ...`, then point `signingConfig` at it).
Committing a keystore to a public repo is not advisable, so nothing here does
that for you.

**There is no foreground service.** The BLE link lives in the activity. That is
deliberate — a service would need a persistent notification and battery
exemption permissions — but it means Android may tear the connection down when
the app is backgrounded for a long time. Reopening the app reconnects to the
remembered device automatically.

---

## Build it

### With the bundled script (no Android Studio needed)

The script bootstraps a JDK, the Android SDK and Gradle into `android/.toolchain`
so a machine with no Android tooling at all can still build this. It downloads
roughly 500 MB and unpacks to about 1.7 GB (the Gradle dependency cache is most
of it); `rm -rf android/.toolchain` removes all of it.

```bash
android/tools/setup-toolchain.sh
android/tools/build.sh :app:assembleDebug
```

The APK lands in `android/app/build/outputs/apk/debug/app-debug.apk`.

### With Android Studio

Open the `android/` directory as a project, let it sync, and press Run. Nothing
else is required — there are no third-party dependencies to resolve beyond the
Android Gradle Plugin itself.

### Requirements this pins

| Piece | Version | Why |
| :--- | :--- | :--- |
| compileSdk / targetSdk | 34 | Matches the permissions model in `AndroidManifest.xml`. |
| minSdk | 26 | Android 8.0. Buys `adaptive` resources and modern BLE APIs without legacy paths. |
| AGP | 8.5.2 | Last series that pairs with Gradle 8.7 and Kotlin 1.9.x. |
| Java | 17 | What AGP 8.x requires. |

---

## What it shows

The two figures a rider acts on get the top third of the screen; everything else
is a grid of bare values underneath, plus the tail of the event log.

| Hero | Source field |
| :--- | :--- |
| Slope, in degrees, signed | `imuSlopeDeg` (calibrated: 0° is level on *this* bike) |
| Speed, km/h | `speedKph` |

| Cell | Source field |
| :--- | :--- |
| Watts | `powerWatts` |
| Amps | `currentAmps` |
| Battery | `batteryPercent` |
| Volts | `batteryVoltage` |
| Cadence | `cadenceRpm` |
| Motor RPM | `motorRpm` |
| Target current | `targetCurrentPercent` |
| Target speed | `targetSpeedPercent` |
| Assist | `assistLevel` |
| Mode | `operationMode` |
| Status | `statusCode` (`OK`, or `E<n>`) |
| Lights | `lights` |

The slope goes amber past ±8°, because that is where gearing decisions get made.
When no MPU-6050 answered at boot the slope shows `--` rather than a fabricated
`+0.0°`.

The cadence / motor RPM / target rows show `--` when `hasTargetTelemetry` is
false. That flag means the firmware's `0xEC` debug frame has not arrived in the
last four seconds — a stale number left over from a previous link is worse than
no number.

### What it deliberately does not do

- **Config editing.** Assist levels, limits and the rest stay on the web
  dashboard. Reproducing a 154/192-byte struct editor (twice, version-gated) is a
  large amount of UI for something you do parked, not riding.
- **Firmware upload.** The `~1.5 MB` image has no place in BLE request framing.
- **Wi-Fi / HTTP.** The app is BLE-only, which is the transport that works with no
  router, no phone hotspot, and no network at all.

### Why an app at all, when `web/index.html` already does BLE?

Because that page can only do it in Chrome. Web Bluetooth does not exist in an
Android WebView, and the hosted copy needs HTTPS (GitHub Pages) to be a secure
context at all. A native app has no such dependency: it works offline forever,
keeps the screen awake while riding, reconnects to a remembered device, and does
not disappear when someone clears browsing data.

---

## How it talks to the middleman

It implements exactly the framing in
[`include/BleProtocol.h`](../include/BleProtocol.h), which `web/index.html` also
implements. **All three have to agree** — change the UUIDs or the layout and you
must change every copy or discovery silently stops working.

```
request   u16 seq | u8 method | u8 rsvd | u16 pathLen | u16 bodyLen | path | body
response  u16 seq | u16 status | u16 totalLen | body slice
```

Little endian. Even a short path exceeds one ATT packet at the default MTU, so
both directions are byte streams: the writer cuts frames into `MTU - 3` slices and
waits for each ack, and the reader reassembles until it has `totalLen` bytes. The
response header repeats on every slice, so each notification is self-describing.

Replies and unsolicited pushes share the response characteristic and are told
apart by sequence range: requests are `1..0x7FFF`, pushes are `0x8000..0xFFFF`.

| Push | `status` | Cadence |
| :--- | :--- | :--- |
| Telemetry | `1` | every second while connected and authenticated |
| Event log | `2` | only when the log actually changes |

Push reassembly uses a single buffer keyed on `seq`, mirroring the browser
client. Each push gets a fresh sequence number, so a message whose chunks were
partly lost is abandoned when the next one starts rather than corrupting a shared
buffer.

### Authentication

The device answers exactly one request before it is authenticated:
`POST /api/auth`. A device with no PIN configured accepts `{}` and returns
`pinRequired: false`, so the handshake is a no-op in the normal case.

If a PIN is set, the device returns `401`, the app prompts once, and the entered
PIN is stored for next time. A rejected PIN is deliberately **not** retried — the
firmware counts failed attempts and locks the transport for 30 seconds after five.

### Threading

Every GATT operation and every field that describes the connection is confined to
the main thread. Callbacks arrive on a Bluedroid binder thread and are re-posted
to the main looper immediately, which serialises them behind whatever the UI is
doing. That is why there is no lock on the write queue or the inflight map.

Do not "optimise" that away by touching `gatt` or the queues from a callback.

---

## Layout

```
android/
├── README.md                      # this file
├── settings.gradle.kts
├── build.gradle.kts               # pinned plugin versions
├── gradle.properties
├── gradlew, gradle/wrapper/       # for anyone without the local toolchain
├── tools/
│   ├── setup-toolchain.sh         # JDK + SDK + Gradle into .toolchain/
│   └── build.sh                   # builds with the local toolchain
└── app/
    ├── build.gradle.kts
    └── src/main/
        ├── AndroidManifest.xml    # BLE permissions, split at API 31
        ├── java/com/bbshd/middleman/
        │   ├── MainActivity.kt    # the dashboard; builds its own grid
        │   ├── BleClient.kt       # scan, connect, framing, auth, reassembly
        │   └── Telemetry.kt       # `/api/telemetry` and `/api/events` parsing
        └── res/
            ├── layout/activity_main.xml
            ├── drawable/ic_launcher.xml
            └── values/{colors,strings,themes}.xml
```

There are **no third-party dependencies**. `org.json` is in the framework and the
UI is plain framework views, which keeps the APK tiny, the build fast, and the
supply chain empty. Resist adding Compose for a screen this size.

---

## Gotchas

- **`user.home` is not `$HOME`.** Both `sdkmanager` and AGP's debug-keystore lookup
  resolve through Java's `user.home`. On a machine where the real home directory is
  read-only, `setup-toolchain.sh` redirects it and `build.sh` hands AGP an explicit
  keystore path; without both you get *"IO exception while downloading manifest"*
  and *"Unable to create debug keystore"*.
- **The BLE service UUID is the discovery key.** A device that has not yet put it
  in its advertisement is still found, because the scan is unfiltered and also
  matches a name containing `BBSHD`. Filtering in hardware would hide a bike that
  is mid-boot, which is exactly when someone is standing there waiting.
- **`configChanges` is load-bearing on the activity.** The connection lives in the
  activity, so letting Android recreate it on rotation would drop the link — and a
  phone on the bars gets rotated.
- **Nothing here writes to the controller.** It is a read-only view over the same
  API the web dashboard uses; every write path still belongs to the web portal.

---

## Assist level: why one-shot injection fails, and the override that works

`POST /api/cmd/pas` writes one PAS frame to the controller. It does **not** stick.
The display re-asserts its own level on every poll cycle, ~1 s apart:

```
16 0b 00 21     <- the display re-asserting its own level (0x00 = level 0)
```

So an injected level reads back correctly for about a second and then reverts. This
is why the assist +/- buttons on the Dashboard are decorative against a connected
display: the display is the authority.

**The fix is a firmware-side override**, and it is what hill assist uses.
`POST /api/cmd/auto-assist {"enabled":true,"level":N}` puts the middleman in charge:
while the override is active it holds each display write frame until it is complete,
substitutes its own PAS level, recomputes the 8-bit sum checksum, and forwards the
modified frame. The display never sees the difference; the controller only ever sees
our level, so there is no oscillation.

Two consequences worth knowing:

* **The display's PAS buttons stop working while the override is active.** The
  middleman rewrites whatever the display sends, including a rider's button press.
  The display will also show a level that may not match what the controller is doing.
  Turn hill assist off to get the buttons back.
* **The override is a 15 second lease, not a setting.** The app renews it every 5 s.
  If the phone dies, is backgrounded, or walks out of range, the middleman expires the
  override and the display gets control back. Without that the bike would be stuck at
  the last commanded level with dead buttons.

## Hill assist (phone sensors)

A checkbox on the Dashboard. When on, the phone's own orientation sensor measures the
road grade and the app commands an assist level from it:

| Grade | Level |
| --- | --- |
| below 1 % (including downhill) | 1 |
| 1–2 % | 2 |
| 3 % | 4 |
| 8 % and above | 9 (clamped) |

`level = 1 + floor(grade%)`, clamped to 1..9.

**Mount the phone flat on the bars, screen up, top of the phone pointing forward.**
The measurement is the tilt of the device's +Y axis out of horizontal, so a
screen-outward mount puts that axis ~90 degrees out and the signal collapses. The
Zero button warns if it captured a near-vertical zero.

Press **Zero** while stopped on ground you know is level; the offset is relative to
that, exactly like the bike's own power-on zero. Grade is smoothed hard (alpha 0.12
on a ~50 Hz feed) because a level that twitches commands a motor that twitches, and
level changes need both 0.25 % of hysteresis and a 1.2 s dwell so a pothole cannot
change gear.

## Known limitation: one-shot PAS injection

The Dashboard's assist +/- controls still use the one-shot `POST /api/cmd/pas`, so
against a connected display they revert within about a second. They are useful
against a controller with no display attached. Making them work everywhere means
routing them through the override while hill assist is off, which needs a decision
about what the display should then show.

