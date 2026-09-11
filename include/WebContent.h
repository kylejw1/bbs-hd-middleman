#ifndef WEB_CONTENT_H
#define WEB_CONTENT_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BBS-HD Smart Middleman</title>
<style>
:root {
  --bg-primary: #0f172a;
  --bg-card: #1e293b;
  --bg-card-alt: #334155;
  --accent-cyan: #06b6d4;
  --accent-emerald: #10b981;
  --accent-amber: #f59e0b;
  --accent-rose: #f43f5e;
  --text-main: #f8fafc;
  --text-muted: #94a3b8;
  --border: #334155;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
body { background: var(--bg-primary); color: var(--text-main); min-height: 100vh; display: flex; flex-direction: column; }
header { background: #0b1120; border-bottom: 1px solid var(--border); padding: 12px 20px; display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 10px; }
.logo-title { display: flex; align-items: center; gap: 12px; }
.logo-title h1 { font-size: 1.25rem; font-weight: 700; color: var(--accent-cyan); letter-spacing: 0.5px; }
.badge { font-size: 0.75rem; padding: 3px 8px; border-radius: 9999px; font-weight: 600; text-transform: uppercase; }
.badge-active { background: rgba(16, 185, 129, 0.2); color: var(--accent-emerald); border: 1px solid var(--accent-emerald); }
.badge-warning { background: rgba(245, 158, 11, 0.2); color: var(--accent-amber); border: 1px solid var(--accent-amber); }
.badge-error { background: rgba(244, 63, 94, 0.2); color: var(--accent-rose); border: 1px solid var(--accent-rose); }
.badge-neutral { background: var(--bg-card-alt); color: var(--text-muted); }
.status-pill-group { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
nav { background: #131d31; display: flex; overflow-x: auto; border-bottom: 1px solid var(--border); padding: 0 10px; }
nav button { background: none; border: none; color: var(--text-muted); padding: 12px 16px; font-weight: 600; font-size: 0.9rem; cursor: pointer; white-space: nowrap; transition: 0.2s; border-bottom: 3px solid transparent; }
nav button:hover { color: var(--text-main); }
nav button.active { color: var(--accent-cyan); border-bottom-color: var(--accent-cyan); }
main { flex: 1; padding: 20px; max-width: 1200px; margin: 0 auto; width: 100%; }
.tab-content { display: none; }
.tab-content.active { display: block; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; margin-bottom: 20px; }
.card { background: var(--bg-card); border: 1px solid var(--border); border-radius: 12px; padding: 20px; position: relative; }
.card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
.card-title { font-size: 1rem; font-weight: 600; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.5px; }
.gauge-val { font-size: 3rem; font-weight: 800; line-height: 1; color: var(--text-main); }
.gauge-unit { font-size: 1.25rem; font-weight: 500; color: var(--text-muted); margin-left: 6px; }
.gauge-sub { margin-top: 8px; font-size: 0.9rem; color: var(--text-muted); }
.bar-track { background: var(--bg-card-alt); height: 10px; border-radius: 5px; margin-top: 14px; overflow: hidden; }
.bar-fill { height: 100%; border-radius: 5px; transition: width 0.3s ease; }
.fill-cyan { background: linear-gradient(90deg, #06b6d4, #3b82f6); }
.fill-emerald { background: linear-gradient(90deg, #10b981, #059669); }
.fill-amber { background: linear-gradient(90deg, #f59e0b, #d97706); }
.fill-rose { background: linear-gradient(90deg, #f43f5e, #e11d48); }
.form-group { margin-bottom: 14px; }
.form-group label { display: block; font-size: 0.85rem; font-weight: 600; margin-bottom: 6px; color: var(--text-muted); }
.form-group small { display: block; font-size: 0.75rem; color: #64748b; margin-top: 4px; }
.form-control { width: 100%; background: #0f172a; border: 1px solid var(--border); border-radius: 8px; padding: 10px 12px; color: var(--text-main); font-size: 0.95rem; }
.form-control:focus { outline: none; border-color: var(--accent-cyan); box-shadow: 0 0 0 2px rgba(6, 182, 212, 0.2); }
.form-row { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.btn { padding: 10px 18px; border-radius: 8px; font-weight: 600; font-size: 0.9rem; cursor: pointer; border: none; transition: 0.2s; display: inline-flex; align-items: center; justify-content: center; gap: 8px; }
.btn:disabled { opacity: 0.5; cursor: not-allowed; }
.btn-primary { background: var(--accent-cyan); color: #0f172a; }
.btn-primary:hover:not(:disabled) { background: #22d3ee; }
.btn-success { background: var(--accent-emerald); color: #0f172a; }
.btn-success:hover:not(:disabled) { background: #34d399; }
.btn-danger { background: var(--accent-rose); color: white; }
.btn-danger:hover:not(:disabled) { background: #fb7185; }
.btn-secondary { background: var(--bg-card-alt); color: var(--text-main); }
.btn-secondary:hover:not(:disabled) { background: #475569; }
.btn-group { display: flex; gap: 8px; flex-wrap: wrap; }
.pas-selector { display: flex; gap: 6px; flex-wrap: wrap; margin-top: 10px; }
.pas-btn { flex: 1; min-width: 44px; height: 44px; background: var(--bg-card-alt); border: 2px solid transparent; border-radius: 8px; color: var(--text-main); font-weight: 700; font-size: 1.1rem; cursor: pointer; }
.pas-btn.active { border-color: var(--accent-cyan); background: rgba(6, 182, 212, 0.2); color: var(--accent-cyan); }
.action-banner { background: #131d31; border: 1px solid var(--border); border-radius: 12px; padding: 16px 20px; display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 12px; margin-bottom: 20px; }
.table-wrapper { overflow-x: auto; margin-top: 10px; }
table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
th, td { padding: 10px 8px; text-align: center; border-bottom: 1px solid var(--border); }
th { background: #0f172a; color: var(--text-muted); font-weight: 600; text-transform: uppercase; }
tr:hover td { background: rgba(255, 255, 255, 0.02); }
td input[type="number"] { width: 60px; background: #0f172a; border: 1px solid var(--border); border-radius: 6px; padding: 6px 4px; color: var(--text-main); text-align: center; }
td input[type="checkbox"] { transform: scale(1.2); accent-color: var(--accent-cyan); cursor: pointer; }
.log-stream { background: #090e17; border: 1px solid var(--border); border-radius: 8px; padding: 12px; height: 350px; overflow-y: auto; font-family: monospace; font-size: 0.82rem; }
.log-line { padding: 4px 0; border-bottom: 1px solid rgba(255,255,255,0.05); display: flex; gap: 10px; }
.log-time { color: var(--text-muted); flex-shrink: 0; }
.log-info { color: #38bdf8; }
.log-warning { color: #fbbf24; }
.log-error { color: #f87171; }
.toast { position: fixed; bottom: 20px; right: 20px; padding: 12px 20px; border-radius: 8px; background: var(--bg-card); border: 1px solid var(--border); color: var(--text-main); box-shadow: 0 10px 15px -3px rgba(0,0,0,0.5); z-index: 100; opacity: 0; transform: translateY(20px); transition: 0.3s; pointer-events: none; }
.toast.show { opacity: 1; transform: translateY(0); }

/* Debug Console */
.debug-toolbar { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; margin-bottom: 10px; }
.debug-toggle { padding: 5px 12px; border-radius: 6px; border: 1px solid var(--border); background: var(--bg-card-alt); color: var(--text-main); font-size: 0.75rem; font-weight: 600; cursor: pointer; transition: 0.2s; }
.debug-toggle:hover { background: #475569; }
.debug-toggle.on { background: rgba(6, 182, 212, 0.2); border-color: var(--accent-cyan); color: var(--accent-cyan); }
.terminal { background: #060a12; border: 1px solid var(--border); border-radius: 8px; height: 520px; overflow-y: auto; padding: 10px 12px; font-family: "SF Mono", "Cascadia Code", Menlo, Consolas, monospace; font-size: 0.78rem; line-height: 1.45; }
.tline { display: flex; gap: 8px; white-space: nowrap; padding: 0.5px 0; }
.tline:hover { background: rgba(255,255,255,0.04); }
.t-time { color: #475569; flex-shrink: 0; }
.t-dir { flex-shrink: 0; width: 96px; font-weight: 700; text-align: right; }
.t-hex { color: #cbd5e1; letter-spacing: 0.5px; }
.t-ascii { color: #526072; }
.t-parsed { color: #a5b4fc; font-style: italic; white-space: normal; min-width: 180px; max-width: 320px; }
.t-drx { color: #34d399; }
.t-dtx { color: #22d3ee; }
.t-crx { color: #fbbf24; }
.t-ctx { color: #fb7185; }
.t-sys { color: #94a3b8; }
.t-dropped { color: var(--accent-rose); font-weight: 700; }
</style>
</head>
<body>

<header>
  <div class="logo-title">
    <h1>BBS-HD Middleman</h1>
    <span class="badge badge-active" id="badge-mode">Transparent Bridge</span>
    <span class="badge badge-neutral" id="badge-ctrl">BBS-HD</span>
  </div>
  <div class="status-pill-group">
    <span class="badge badge-neutral" id="status-wifi" title="Wi-Fi mode">Wi-Fi: --</span>
    <span class="badge badge-neutral" id="status-disp">Display: Offline</span>
    <span class="badge badge-neutral" id="status-ctrl">Motor: Offline</span>
    <button class="btn btn-secondary" style="padding:4px 10px; font-size:0.75rem;" onclick="openWifiModal()">Wi-Fi</button>
  </div>
</header>

<nav>
  <button class="active" onclick="switchTab('tab-dash')">Dashboard</button>
  <button onclick="switchTab('tab-basic')">Motor & Basic</button>
  <button onclick="switchTab('tab-pas-th')">PAS & Throttle</button>
  <button onclick="switchTab('tab-levels-std')">Standard Levels (0-9)</button>
  <button onclick="switchTab('tab-levels-sport')">Sport Levels (0-9)</button>
  <button onclick="switchTab('tab-sensors')">Sensors & Features</button>
  <button onclick="switchTab('tab-calibrate')">Calibration & Tools</button>
  <button onclick="switchTab('tab-events')">Live Event Log</button>
  <button onclick="switchTab('tab-debug')">Debug Console</button>
  <button onclick="switchTab('tab-firmware')">Firmware Update</button>
  <button onclick="switchTab('tab-pinout')">ESP32 Pinout</button>
</nav>

<main>
  <!-- TAB: DASHBOARD -->
  <div id="tab-dash" class="tab-content active">
    <div class="grid">
      <div class="card">
        <div class="card-header">
          <span class="card-title">Speed</span>
          <span id="speed-rpm" style="font-size:0.8rem; color:var(--text-muted)">0 RPM</span>
        </div>
        <div style="display:flex; align-items:baseline;">
          <span class="gauge-val" id="val-speed">0.0</span>
          <span class="gauge-unit" id="unit-speed">km/h</span>
        </div>
        <div class="bar-track">
          <div class="bar-fill fill-cyan" id="bar-speed" style="width: 0%;"></div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <span class="card-title">Power Output</span>
          <span id="val-amps" style="font-size:0.9rem; font-weight:700; color:var(--accent-emerald)">0.0 A</span>
        </div>
        <div style="display:flex; align-items:baseline;">
          <span class="gauge-val" id="val-watts">0</span>
          <span class="gauge-unit">W</span>
        </div>
        <div class="bar-track">
          <div class="bar-fill fill-emerald" id="bar-power" style="width: 0%;"></div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <span class="card-title">Battery Pack</span>
          <span id="val-volts" style="font-size:0.9rem; font-weight:700; color:var(--accent-cyan)">52.0 V</span>
        </div>
        <div style="display:flex; align-items:baseline;">
          <span class="gauge-val" id="val-battery">100</span>
          <span class="gauge-unit">%</span>
        </div>
        <div class="bar-track">
          <div class="bar-fill fill-cyan" id="bar-battery" style="width: 100%;"></div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <span class="card-title">Temperature & Status</span>
          <span id="val-status" class="badge badge-active">OK (0x00)</span>
        </div>
        <div style="margin-top:8px;">
          <div style="display:flex; justify-content:space-between; margin-bottom:8px;">
            <span style="color:var(--text-muted)">Controller Temp</span>
            <strong id="val-temp-ctrl">25 &deg;C</strong>
          </div>
          <div style="display:flex; justify-content:space-between;">
            <span style="color:var(--text-muted)">Motor Temp</span>
            <strong id="val-temp-motor">25 &deg;C</strong>
          </div>
        </div>
      </div>
    </div>

    <!-- Quick Controls -->
    <div class="card" style="margin-bottom:20px;">
      <div class="card-header">
        <span class="card-title">Direct Controls & Override</span>
        <div style="display:flex; gap:10px;">
          <button class="btn btn-secondary" id="btn-mode-toggle" onclick="toggleMode()">Mode: Standard</button>
          <button class="btn btn-secondary" id="btn-lights-toggle" onclick="toggleLights()">Headlight: OFF</button>
        </div>
      </div>
      <div class="card-title" style="margin-top:10px;">Assist Level Selector:</div>
      <div class="pas-selector">
        <button class="pas-btn" onclick="setPas(0)">0</button>
        <button class="pas-btn active" onclick="setPas(1)">1</button>
        <button class="pas-btn" onclick="setPas(2)">2</button>
        <button class="pas-btn" onclick="setPas(3)">3</button>
        <button class="pas-btn" onclick="setPas(4)">4</button>
        <button class="pas-btn" onclick="setPas(5)">5</button>
        <button class="pas-btn" onclick="setPas(6)">6</button>
        <button class="pas-btn" onclick="setPas(7)">7</button>
        <button class="pas-btn" onclick="setPas(8)">8</button>
        <button class="pas-btn" onclick="setPas(9)">9</button>
      </div>
    </div>
  </div>

  <!-- ACTION BANNER (Visible on Config Tabs) -->
  <div id="cfg-banner" class="action-banner" style="display:none;">
    <div>
      <strong style="font-size:1.05rem;">BBS-FW Controller Configuration</strong>
      <p style="font-size:0.8rem; color:var(--text-muted)">The middleman will seamlessly isolate the display while reading or flashing EEPROM.</p>
    </div>
    <div class="btn-group">
      <button class="btn btn-secondary" onclick="fetchConfigFromController()">Read From Controller</button>
      <button class="btn btn-primary" onclick="writeConfigToController()">Save To Controller</button>
    </div>
  </div>

  <!-- TAB: MOTOR & BASIC -->
  <div id="tab-basic" class="tab-content">
    <div class="grid">
      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Current & Limits</h3>
        <div class="form-group">
          <label>Max Current (Amps)</label>
          <input type="number" class="form-control" id="cfg-maxCurrent" min="5" max="33">
          <small>Safe BBSHD limit: 30A (stock controller max is 33A with bbs-fw).</small>
        </div>
        <div class="form-group">
          <label>Current Ramp Rate (A/s)</label>
          <input type="number" class="form-control" id="cfg-currentRamp" min="1" max="255">
          <small>Amperes per second rate of change. Lower = smoother acceleration.</small>
        </div>
        <div class="form-group">
          <label>Max Speed Limit (km/h)</label>
          <input type="number" class="form-control" id="cfg-maxSpeed" min="0" max="180">
        </div>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Battery Voltage & Wheel</h3>
        <div class="form-group">
          <label>Max Battery Voltage (V)</label>
          <input type="number" step="0.1" class="form-control" id="cfg-maxBatteryVolts" min="1" max="100">
          <small>54.6V for 48V pack (13S), 58.8V for 52V pack (14S).</small>
        </div>
        <div class="form-group">
          <label>Low Voltage Cutoff LVC (V)</label>
          <input type="number" class="form-control" id="cfg-lowCutoffVolts" min="1" max="100">
          <small>39V-41V for 52V pack, 36V-38V for 48V pack.</small>
        </div>
        <div class="form-row">
          <div class="form-group">
            <label>Wheel Size (inch)</label>
            <input type="number" step="0.1" class="form-control" id="cfg-wheelSizeInch" min="10" max="36">
          </div>
          <div class="form-group">
            <label>Speed Sensor Signals</label>
            <input type="number" class="form-control" id="cfg-speedSensorSignals" min="1" max="10">
          </div>
        </div>
        <div class="form-group">
          <label>Display Units</label>
          <select class="form-control" id="cfg-freedomUnits">
            <option value="" selected></option>
            <option value="0">Metric (km/h)</option>
            <option value="1">Imperial (mph)</option>
          </select>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: PAS & THROTTLE -->
  <div id="tab-pas-th" class="tab-content">
    <div class="grid">
      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Pedal Assist (PAS) Tuning</h3>
        <div class="form-group">
          <label>PAS Start Delay (magnet pulses)</label>
          <input type="number" class="form-control" id="cfg-pasStartDelay" min="0" max="24">
          <small>Pulses needed before assist engages. 2-3 provides responsive startup.</small>
        </div>
        <div class="form-group">
          <label>PAS Stop Delay (ms)</label>
          <input type="number" step="10" class="form-control" id="cfg-pasStopDelayMs" min="50" max="1000">
          <small>Delay before motor stops after pedaling stops. 150-250ms is optimal.</small>
        </div>
        <div class="form-group">
          <label>PAS Keep Current (%)</label>
          <input type="number" class="form-control" id="cfg-pasKeepCurrentPercent" min="10" max="100">
          <small>Current maintained during high cadence pedaling.</small>
        </div>
        <div class="form-group">
          <label>PAS Keep Current Cadence (RPM)</label>
          <input type="number" class="form-control" id="cfg-pasKeepCurrentCadenceRpm" min="0" max="255">
        </div>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Throttle Options</h3>
        <div class="form-row">
          <div class="form-group">
            <label>Start Voltage (mV)</label>
            <input type="number" class="form-control" id="cfg-throttleStartMv" min="200" max="2500">
          </div>
          <div class="form-group">
            <label>End Voltage (mV)</label>
            <input type="number" class="form-control" id="cfg-throttleEndMv" min="2500" max="5000">
          </div>
        </div>
        <div class="form-group">
          <label>Throttle Initial Power Kick (%)</label>
          <input type="number" class="form-control" id="cfg-throttleStartPercent" min="0" max="100">
        </div>
        <div class="form-group">
          <label>Global Speed Limit Mode</label>
          <select class="form-control" id="cfg-throttleGlobalSpdLimOpt">
            <option value="" selected></option>
            <option value="0">Disabled (Follows PAS)</option>
            <option value="1">Enabled (Custom Limit)</option>
            <option value="2">Standard Levels</option>
          </select>
        </div>
        <div class="form-group">
          <label>Global Speed Limit (%)</label>
          <input type="number" class="form-control" id="cfg-throttleGlobalSpdLimPercent" min="0" max="100">
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: STANDARD LEVELS MATRIX -->
  <div id="tab-levels-std" class="tab-content">
    <div class="card">
      <h3 class="card-title" style="margin-bottom:8px;">Standard Assist Levels (0 - 9)</h3>
      <p style="font-size:0.8rem; color:var(--text-muted); margin-bottom:14px;">Configure target motor current %, throttle %, cadence %, and override flags per assist level.</p>
      <div class="table-wrapper">
        <table id="tbl-levels-std">
          <thead>
            <tr>
              <th>Lvl</th>
              <th>Current %</th>
              <th>Max Throt %</th>
              <th>Cadence %</th>
              <th>Speed %</th>
              <th>PAS</th>
              <th>Throt</th>
              <th>Cruise</th>
              <th>Cad. Over</th>
              <th>Spd. Over</th>
              <th>PAS Var</th>
              <th>PAS Torq</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- TAB: SPORT LEVELS MATRIX -->
  <div id="tab-levels-sport" class="tab-content">
    <div class="card">
      <h3 class="card-title" style="margin-bottom:8px;">Sport Assist Levels (0 - 9)</h3>
      <p style="font-size:0.8rem; color:var(--text-muted); margin-bottom:14px;">Sport mode active levels (triggered via mode switch or display).</p>
      <div class="table-wrapper">
        <table id="tbl-levels-sport">
          <thead>
            <tr>
              <th>Lvl</th>
              <th>Current %</th>
              <th>Max Throt %</th>
              <th>Cadence %</th>
              <th>Speed %</th>
              <th>PAS</th>
              <th>Throt</th>
              <th>Cruise</th>
              <th>Cad. Over</th>
              <th>Spd. Over</th>
              <th>PAS Var</th>
              <th>PAS Torq</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- TAB: SENSORS & FEATURES -->
  <div id="tab-sensors" class="tab-content">
    <div class="grid">
      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Sensors & Cutoffs</h3>
        <div class="form-group">
          <label><input type="checkbox" id="cfg-useSpeedSensor"> External Wheel Speed Sensor</label>
        </div>
        <div class="form-group">
          <label><input type="checkbox" id="cfg-useShiftSensor"> Shift Sensor Active</label>
        </div>
        <div class="form-row">
          <div class="form-group">
            <label>Shift Cut Duration (ms)</label>
            <input type="number" class="form-control" id="cfg-shiftInterruptDurationMs" min="50" max="2000">
          </div>
          <div class="form-group">
            <label>Shift Current Cut (%)</label>
            <input type="number" class="form-control" id="cfg-shiftInterruptCurrentThreshold" min="0" max="100">
          </div>
        </div>
        <div class="form-group">
          <label>Temperature Sensors</label>
          <select class="form-control" id="cfg-temperatureSensor">
            <option value="" selected></option>
            <option value="0">Disabled</option>
            <option value="1">Controller Only</option>
            <option value="2">Motor Only</option>
            <option value="3">All (Controller & Motor)</option>
          </select>
        </div>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Auxiliary Features</h3>
        <div class="form-group">
          <label>Assist Mode Selection</label>
          <select class="form-control" id="cfg-assistModeSelect">
            <option value="" selected></option>
            <option value="0">Off (Fixed mode)</option>
            <option value="1">Standard (Display button)</option>
            <option value="2">Lights (Headlight switch)</option>
          </select>
          <small>How the rider switches between Standard and Sport mode profiles.</small>
        </div>
        <div class="form-group">
          <label>Lights Mode</label>
          <select class="form-control" id="cfg-lightsMode">
            <option value="" selected></option>
            <option value="0">Default (Display controlled)</option>
            <option value="1">Disabled</option>
            <option value="2">Always On</option>
            <option value="3">Brake Light</option>
          </select>
        </div>
        <div class="form-group">
          <label><input type="checkbox" id="cfg-usePushWalk"> Push / Walk Assist Enabled</label>
        </div>
        <div class="form-group">
          <label>Walk Mode Display Data Field</label>
          <select class="form-control" id="cfg-walkModeDisplay">
            <option value="" selected></option>
            <option value="0">Speed</option>
            <option value="1">Temperature (&deg;C)</option>
            <option value="2">Requested Power (W)</option>
            <option value="3">Battery Percent (%)</option>
          </select>
        </div>
        <div class="form-group">
          <label>Startup Assist Level (0 - 9)</label>
          <input type="number" class="form-control" id="cfg-assistStartupLevel" min="0" max="9">
        </div>
      </div>
    </div>
  </div>

  <!-- TAB: CALIBRATION & TOOLS -->
  <div id="tab-calibrate" class="tab-content">
    <div class="grid">
      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Battery Voltage ADC Calibration</h3>
        <p style="font-size:0.85rem; color:var(--text-muted); margin-bottom:12px;">Calibrate the controller's internal ADC so low voltage cutoff and battery percentages are pinpoint accurate.</p>
        <div class="form-group">
          <label>Measured Battery Voltage (from digital multimeter)</label>
          <input type="number" step="0.1" class="form-control" id="cal-volts" placeholder="e.g. 52.4">
        </div>
        <button class="btn btn-primary" onclick="calibrateVoltage()">Calibrate Voltage ADC</button>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Backup & Factory Reset</h3>
        <p style="font-size:0.85rem; color:var(--text-muted); margin-bottom:12px;">Export or import configurations as JSON files, or reset to safe BBS-FW defaults.</p>
        <div class="btn-group" style="margin-bottom:12px;">
          <button class="btn btn-secondary" onclick="exportJsonFile()">Export .json</button>
          <label class="btn btn-secondary" style="margin:0;">
            Import .json <input type="file" id="file-import" accept=".json" style="display:none" onchange="importJsonFile(event)">
          </label>
        </div>
        <button class="btn btn-danger" onclick="resetFactoryConfig()">Reset Controller to Defaults</button>
      </div>
    </div>
  </div>

  <!-- TAB: LIVE EVENT LOG -->
  <div id="tab-events" class="tab-content">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Live Controller Event Log</span>
        <button class="btn btn-secondary" style="padding:4px 10px; font-size:0.75rem;" onclick="clearEventDisplay()">Clear</button>
      </div>
      <div class="log-stream" id="log-stream"></div>
    </div>
  </div>

  <!-- TAB: DEBUG CONSOLE -->
  <div id="tab-debug" class="tab-content">
    <div class="card">
      <div class="card-header">
        <span class="card-title">Serial Port Debug Console</span>
        <span id="debug-stats" style="font-size:0.75rem; color:var(--text-muted);">disabled</span>
      </div>
      <p style="font-size:0.8rem; color:var(--text-muted); margin-bottom:12px;">
        Tracing is off by default to keep the UART bridge and Wi-Fi link light.
        Enable it only while actively diagnosing the bus.
      </p>
      <div class="debug-toolbar">
        <button class="debug-toggle" id="btn-debug-enable" onclick="toggleDebugEnabled()">Debug: OFF</button>
        <button class="debug-toggle on" id="btn-pause" onclick="toggleDebugPause()">Running</button>
        <button class="debug-toggle on" id="btn-autoscroll" onclick="toggleDebugAutoscroll()">Auto-scroll</button>
        <button class="btn btn-secondary" style="padding:4px 10px;font-size:0.75rem;" onclick="clearDebugConsole()">Clear</button>
        <span style="font-size:0.75rem;color:var(--text-muted);">Filter:</span>
        <button class="debug-toggle on" id="f-drx" onclick="toggleDebugFilter('drx')">DISP→</button>
        <button class="debug-toggle on" id="f-dtx" onclick="toggleDebugFilter('dtx')">→DISP</button>
        <button class="debug-toggle on" id="f-crx" onclick="toggleDebugFilter('crx')">CTRL→</button>
        <button class="debug-toggle on" id="f-ctx" onclick="toggleDebugFilter('ctx')">→CTRL</button>
        <button class="debug-toggle on" id="f-sys" onclick="toggleDebugFilter('sys')">SYS</button>
      </div>
      <div class="terminal" id="terminal"></div>
    </div>
  </div>

  <!-- TAB: FIRMWARE UPDATE -->
  <div id="tab-firmware" class="tab-content">
    <div class="card">
      <h3 class="card-title" style="margin-bottom:14px;">Firmware Update</h3>
      <p style="margin-bottom:12px; color:var(--text-muted);">
        Current firmware: <strong id="fw-version">...</strong> built <strong id="fw-build">...</strong>
      </p>
      <div class="form-group">
        <label>Select firmware .bin file</label>
        <input type="file" id="fw-file" accept=".bin" class="form-control" style="padding:8px;">
      </div>
      <div id="ota-progress-wrapper" style="display:none; margin:12px 0;">
        <div class="bar-track" style="height:14px;">
          <div class="bar-fill fill-cyan" id="ota-progress-bar" style="width:0%;"></div>
        </div>
        <p id="ota-progress-text" style="font-size:0.8rem; color:var(--text-muted); margin-top:6px;">0%</p>
      </div>
      <div class="btn-group">
        <button class="btn btn-primary" id="btn-ota-upload" onclick="startOtaUpload()">Upload Firmware</button>
      </div>
      <p style="font-size:0.75rem; color:var(--text-muted); margin-top:12px;">
        The device will reboot after a successful update. Reconnect to the Wi-Fi access point after ~10 seconds.
      </p>
    </div>
  </div>

  <!-- TAB: ESP32 PINOUT & SCHEMATIC -->
  <div id="tab-pinout" class="tab-content">
    <div class="card">
      <h3 class="card-title" style="margin-bottom:14px;">44-Pin ESP32-S3 Hardware Wiring Guide</h3>
      <div style="font-size:0.9rem; line-height:1.6; color:#cbd5e1;">
        <p><strong>Bafang 5-Pin Connector (Green Higo) Pinout:</strong></p>
        <ul style="margin:8px 0 16px 24px;">
          <li><strong style="color:var(--accent-rose)">Pin 1 (Orange/Brown/Red): BAT+ (48V-52V)</strong> &rarr; DO NOT connect directly to ESP32! Pass through to Display and connect to DC-DC 5V Buck Converter input.</li>
          <li><strong style="color:var(--text-muted)">Pin 2 (Black): GND</strong> &rarr; Common Ground for ESP32, Level Shifter, Display, and Controller.</li>
          <li><strong style="color:var(--accent-amber)">Pin 3 (Blue): P+ (Lock Key/Switch)</strong> &rarr; Pass through directly between Display and Controller (allows display to power on controller).</li>
          <li><strong style="color:var(--accent-cyan)">Pin 4 (Green): TXD</strong> &rarr; Serial Data (TTL 5V).</li>
          <li><strong style="color:var(--accent-emerald)">Pin 5 (Yellow): RXD</strong> &rarr; Serial Data (TTL 5V).</li>
        </ul>
        <p><strong>ESP32-S3 Pin Mapping (via 5V &harr; 3.3V Logic Level Shifter):</strong></p>
        <ul style="margin:8px 0 16px 24px;">
          <li><strong>Controller RX (GPIO 17)</strong> &larr; Controller TXD (Pin 5 on motor cable)</li>
          <li><strong>Controller TX (GPIO 18)</strong> &rarr; Controller RXD (Pin 4 on motor cable)</li>
          <li><strong>Display RX (GPIO 15)</strong> &larr; Display TXD (Pin 4 on display cable)</li>
          <li><strong>Display TX (GPIO 16)</strong> &rarr; Display RXD (Pin 5 on display cable)</li>
          <li><strong>Status LED (GPIO 48)</strong> &rarr; Activity Indicator</li>
        </ul>
      </div>
    </div>
  </div>
</main>

<div class="toast" id="toast"></div>

<script>
let activeConfig = null;
let configLoaded = false;   // true only after a successful controller read or JSON import
let formTorque = { std: [], sport: [] };  // torque amp has no UI box; preserve values read from the motor
let currentPas = 1;
let currentMode = 0; // 0 = standard, 1 = sport
let currentLights = false;

function showToast(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 3000);
}

function switchTab(tabId) {
  document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(el => el.classList.remove('active'));
  document.getElementById(tabId).classList.add('active');
  event.target.classList.add('active');

  const banner = document.getElementById('cfg-banner');
  if (['tab-basic', 'tab-pas-th', 'tab-levels-std', 'tab-levels-sport', 'tab-sensors'].includes(tabId)) {
    banner.style.display = 'flex';
    if (!activeConfig) fetchConfigFromController();
  } else {
    banner.style.display = 'none';
  }

  if (tabId === 'tab-firmware') {
    fetchFirmwareInfo();
  }

  updateDebugPolling();
}

// Build Assist Level Tables
function buildLevelTables() {
  ['std', 'sport'].forEach(type => {
    const tbody = document.querySelector(`#tbl-levels-${type} tbody`);
    tbody.innerHTML = '';
    for (let i = 0; i < 10; ++i) {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td><strong>${i}</strong></td>
        <td><input type="number" min="0" max="100" id="${type}-curr-${i}"></td>
        <td><input type="number" min="0" max="100" id="${type}-throt-${i}"></td>
        <td><input type="number" min="0" max="100" id="${type}-cad-${i}"></td>
        <td><input type="number" min="0" max="100" id="${type}-spd-${i}"></td>
        <td><input type="checkbox" id="${type}-pas-${i}"></td>
        <td><input type="checkbox" id="${type}-th-${i}"></td>
        <td><input type="checkbox" id="${type}-cr-${i}"></td>
        <td><input type="checkbox" id="${type}-oc-${i}"></td>
        <td><input type="checkbox" id="${type}-os-${i}"></td>
        <td><input type="checkbox" id="${type}-pv-${i}"></td>
        <td><input type="checkbox" id="${type}-pt-${i}"></td>
      `;
      tbody.appendChild(tr);
    }
  });
}
buildLevelTables();

// Live Telemetry Polling
async function pollTelemetry() {
  if (document.hidden) return;
  try {
    const res = await fetch('/api/telemetry');
    if (!res.ok) return;
    const d = await res.json();

    document.getElementById('val-speed').innerText = d.speedKph;
    document.getElementById('speed-rpm').innerText = d.speedRpm + ' RPM';
    document.getElementById('bar-speed').style.width = Math.min(100, (d.speedKph / 60) * 100) + '%';

    document.getElementById('val-watts').innerText = d.powerWatts;
    document.getElementById('val-amps').innerText = d.currentAmps + ' A';
    document.getElementById('bar-power').style.width = Math.min(100, (d.powerWatts / 1500) * 100) + '%';

    document.getElementById('val-battery').innerText = d.batteryPercent;
    document.getElementById('val-volts').innerText = d.batteryVoltage + ' V';
    document.getElementById('bar-battery').style.width = d.batteryPercent + '%';

    document.getElementById('val-temp-ctrl').innerText = d.controllerTempC + ' °C';
    document.getElementById('val-temp-motor').innerText = d.motorTempC + ' °C';

    const dispPill = document.getElementById('status-disp');
    dispPill.innerText = d.displayConnected ? 'Display: Live' : 'Display: Offline';
    dispPill.className = 'badge ' + (d.displayConnected ? 'badge-active' : 'badge-neutral');

    const ctrlPill = document.getElementById('status-ctrl');
    ctrlPill.innerText = d.controllerConnected ? 'Motor: Connected' : 'Motor: Offline';
    ctrlPill.className = 'badge ' + (d.controllerConnected ? 'badge-active' : 'badge-neutral');

    if (d.statusCode !== 0) {
      const statusNames = {
        0x01: 'Init',
        0x02: 'Error',
        0x03: 'Throttle Fault',
        0x04: 'Controller Fault',
        0x08: 'Hall Sensor',
        0x10: 'Overcurrent',
        0x20: 'Overvoltage',
        0x40: 'Overtemp',
        0x80: 'Undervoltage'
      };
      const label = statusNames[d.statusCode] || ('Code 0x' + d.statusCode.toString(16).toUpperCase());
      const cls = (d.statusCode === 0x01) ? 'badge-warning' : 'badge-error';
      document.getElementById('val-status').innerText = label;
      document.getElementById('val-status').className = 'badge ' + cls;
    } else {
      document.getElementById('val-status').innerText = 'OK (0x00)';
      document.getElementById('val-status').className = 'badge badge-active';
    }

    if (d.assistLevel !== currentPas) {
      currentPas = d.assistLevel;
      document.querySelectorAll('.pas-btn').forEach((btn, idx) => {
        btn.classList.toggle('active', idx === currentPas);
      });
    }

    currentMode = (d.operationMode === 'Sport') ? 1 : 0;
    document.getElementById('btn-mode-toggle').innerText = 'Mode: ' + d.operationMode;

    currentLights = d.lights;
    document.getElementById('btn-lights-toggle').innerText = 'Headlight: ' + (currentLights ? 'ON' : 'OFF');
  } catch (e) { }
}
// Backed off from 600 ms: the UART only produces new values far slower than
// that, and this halves the radio airtime the dashboard consumes.
setInterval(pollTelemetry, 1500);

// Live Event Log Polling
async function pollEvents() {
  if (document.hidden) return;
  try {
    const res = await fetch('/api/events');
    if (!res.ok) return;
    const list = await res.json();
    const container = document.getElementById('log-stream');
    container.innerHTML = '';
    list.forEach(item => {
      const div = document.createElement('div');
      div.className = 'log-line';
      const sec = Math.floor(item.time / 1000);
      div.innerHTML = `<span class="log-time">[${sec}s]</span> <span class="log-${item.level}">[${item.level.toUpperCase()}]</span> <span>${item.msg}</span>`;
      container.appendChild(div);
    });
  } catch (e) {}
}
setInterval(pollEvents, 3000);

// Refresh immediately when the tab becomes visible again; skip work while hidden.
document.addEventListener('visibilitychange', () => {
  if (document.hidden) return;
  pollTelemetry();
  pollEvents();
  if (debugEnabled) pollSerialTrace();
});

function clearEventDisplay() {
  document.getElementById('log-stream').innerHTML = '';
}

// PAS and Mode Controls
async function setPas(lvl) {
  try {
    await fetch('/api/cmd/pas', { method: 'POST', body: JSON.stringify({ level: lvl }), headers: {'Content-Type': 'application/json'} });
    currentPas = lvl;
    document.querySelectorAll('.pas-btn').forEach((btn, idx) => btn.classList.toggle('active', idx === lvl));
  } catch(e) {}
}

async function toggleMode() {
  const newMode = (currentMode === 1) ? 0 : 1;
  try {
    await fetch('/api/cmd/mode', { method: 'POST', body: JSON.stringify({ mode: newMode }), headers: {'Content-Type': 'application/json'} });
    currentMode = newMode;
    document.getElementById('btn-mode-toggle').innerText = 'Mode: ' + (newMode ? 'Sport' : 'Standard');
  } catch(e) {}
}

async function toggleLights() {
  const newSt = !currentLights;
  try {
    await fetch('/api/cmd/lights', { method: 'POST', body: JSON.stringify({ on: newSt }), headers: {'Content-Type': 'application/json'} });
    currentLights = newSt;
    document.getElementById('btn-lights-toggle').innerText = 'Headlight: ' + (newSt ? 'ON' : 'OFF');
  } catch(e) {}
}

// Config Read/Write
async function fetchConfigFromController() {
  showToast('Reading configuration from controller...');
  document.getElementById('badge-mode').innerText = 'Intercepting Config...';
  document.getElementById('badge-mode').className = 'badge badge-warning';

  try {
    const res = await fetch('/api/config');
    if (!res.ok) throw new Error('Controller did not respond');
    const cfg = await res.json();
    if (cfg.fromController !== true) throw new Error('Config was not read from the controller');
    activeConfig = cfg;
    configLoaded = true;
    populateConfigForm(activeConfig);
    showToast('Configuration loaded successfully!');
  } catch (e) {
    activeConfig = null;
    configLoaded = false;
    showToast('Could not read configuration from the controller.');
  } finally {
    document.getElementById('badge-mode').innerText = 'Transparent Bridge';
    document.getElementById('badge-mode').className = 'badge badge-active';
  }
}

function populateConfigForm(cfg) {
  formTorque = { std: new Array(10).fill(null), sport: new Array(10).fill(null) };
  document.getElementById('cfg-maxCurrent').value = cfg.maxCurrent;
  document.getElementById('cfg-currentRamp').value = cfg.currentRamp;
  document.getElementById('cfg-maxBatteryVolts').value = cfg.maxBatteryVolts;
  document.getElementById('cfg-lowCutoffVolts').value = cfg.lowCutoffVolts;
  document.getElementById('cfg-maxSpeed').value = cfg.maxSpeed;
  document.getElementById('cfg-wheelSizeInch').value = cfg.wheelSizeInch;
  document.getElementById('cfg-speedSensorSignals').value = cfg.speedSensorSignals;
  document.getElementById('cfg-freedomUnits').value = cfg.freedomUnits;

  document.getElementById('cfg-pasStartDelay').value = cfg.pasStartDelay;
  document.getElementById('cfg-pasStopDelayMs').value = cfg.pasStopDelayMs;
  document.getElementById('cfg-pasKeepCurrentPercent').value = cfg.pasKeepCurrentPercent;
  document.getElementById('cfg-pasKeepCurrentCadenceRpm').value = cfg.pasKeepCurrentCadenceRpm;

  document.getElementById('cfg-throttleStartMv').value = cfg.throttleStartMv;
  document.getElementById('cfg-throttleEndMv').value = cfg.throttleEndMv;
  document.getElementById('cfg-throttleStartPercent').value = cfg.throttleStartPercent;
  document.getElementById('cfg-throttleGlobalSpdLimOpt').value = cfg.throttleGlobalSpdLimOpt;
  document.getElementById('cfg-throttleGlobalSpdLimPercent').value = cfg.throttleGlobalSpdLimPercent;

  document.getElementById('cfg-useSpeedSensor').checked = cfg.useSpeedSensor;
  document.getElementById('cfg-useShiftSensor').checked = cfg.useShiftSensor;
  document.getElementById('cfg-shiftInterruptDurationMs').value = cfg.shiftInterruptDurationMs;
  document.getElementById('cfg-shiftInterruptCurrentThreshold').value = cfg.shiftInterruptCurrentThreshold;
  document.getElementById('cfg-temperatureSensor').value = cfg.temperatureSensor;
  document.getElementById('cfg-lightsMode').value = cfg.lightsMode;
  document.getElementById('cfg-usePushWalk').checked = cfg.usePushWalk;
  document.getElementById('cfg-walkModeDisplay').value = cfg.walkModeDisplay;
  document.getElementById('cfg-assistStartupLevel').value = cfg.assistStartupLevel;
  if (cfg.assistModeSelect !== undefined) {
    document.getElementById('cfg-assistModeSelect').value = cfg.assistModeSelect;
  }

  // Populate matrix tables
  if (cfg.standardLevels) {
    cfg.standardLevels.forEach((lvl, i) => {
      if (i > 9) return;
      document.getElementById(`std-curr-${i}`).value = lvl.current;
      document.getElementById(`std-throt-${i}`).value = lvl.maxThrottle;
      document.getElementById(`std-cad-${i}`).value = lvl.cadence;
      document.getElementById(`std-spd-${i}`).value = lvl.speed;
      document.getElementById(`std-pas-${i}`).checked = lvl.pas;
      document.getElementById(`std-th-${i}`).checked = lvl.throttle;
      document.getElementById(`std-cr-${i}`).checked = lvl.cruise;
      document.getElementById(`std-oc-${i}`).checked = lvl.overrideCadence;
      document.getElementById(`std-os-${i}`).checked = lvl.overrideSpeed;
      if (document.getElementById(`std-pv-${i}`)) document.getElementById(`std-pv-${i}`).checked = lvl.pasVariable || false;
      if (document.getElementById(`std-pt-${i}`)) document.getElementById(`std-pt-${i}`).checked = lvl.pasTorque || false;
      formTorque.std[i] = (typeof lvl.torqueAmp === 'number') ? lvl.torqueAmp : null;
    });
  }

  if (cfg.sportLevels) {
    cfg.sportLevels.forEach((lvl, i) => {
      if (i > 9) return;
      document.getElementById(`sport-curr-${i}`).value = lvl.current;
      document.getElementById(`sport-throt-${i}`).value = lvl.maxThrottle;
      document.getElementById(`sport-cad-${i}`).value = lvl.cadence;
      document.getElementById(`sport-spd-${i}`).value = lvl.speed;
      document.getElementById(`sport-pas-${i}`).checked = lvl.pas;
      document.getElementById(`sport-th-${i}`).checked = lvl.throttle;
      document.getElementById(`sport-cr-${i}`).checked = lvl.cruise;
      document.getElementById(`sport-oc-${i}`).checked = lvl.overrideCadence;
      document.getElementById(`sport-os-${i}`).checked = lvl.overrideSpeed;
      if (document.getElementById(`sport-pv-${i}`)) document.getElementById(`sport-pv-${i}`).checked = lvl.pasVariable || false;
      if (document.getElementById(`sport-pt-${i}`)) document.getElementById(`sport-pt-${i}`).checked = lvl.pasTorque || false;
      formTorque.sport[i] = (typeof lvl.torqueAmp === 'number') ? lvl.torqueAmp : null;
    });
  }
}

// Read a numeric box as a number, or null when it is empty/invalid. Nothing in
// the form is pre-filled, so an untouched box must never silently become 0.
function formInt(id) {
  const el = document.getElementById(id);
  if (!el) return null;
  const v = el.value.trim();
  if (v === '') return null;
  const n = parseInt(v, 10);
  return isNaN(n) ? null : n;
}

function formFloat(id) {
  const el = document.getElementById(id);
  if (!el) return null;
  const v = el.value.trim();
  if (v === '') return null;
  const n = parseFloat(v);
  return isNaN(n) ? null : n;
}

function collectConfigFromForm() {
  const cfg = {
    maxCurrent: formInt('cfg-maxCurrent'),
    currentRamp: formInt('cfg-currentRamp'),
    maxBatteryVolts: formFloat('cfg-maxBatteryVolts'),
    lowCutoffVolts: formInt('cfg-lowCutoffVolts'),
    maxSpeed: formInt('cfg-maxSpeed'),
    wheelSizeInch: formFloat('cfg-wheelSizeInch'),
    speedSensorSignals: formInt('cfg-speedSensorSignals'),
    freedomUnits: formInt('cfg-freedomUnits'),
    pasStartDelay: formInt('cfg-pasStartDelay'),
    pasStopDelayMs: formInt('cfg-pasStopDelayMs'),
    pasKeepCurrentPercent: formInt('cfg-pasKeepCurrentPercent'),
    pasKeepCurrentCadenceRpm: formInt('cfg-pasKeepCurrentCadenceRpm'),
    throttleStartMv: formInt('cfg-throttleStartMv'),
    throttleEndMv: formInt('cfg-throttleEndMv'),
    throttleStartPercent: formInt('cfg-throttleStartPercent'),
    throttleGlobalSpdLimOpt: formInt('cfg-throttleGlobalSpdLimOpt'),
    throttleGlobalSpdLimPercent: formInt('cfg-throttleGlobalSpdLimPercent'),
    useSpeedSensor: document.getElementById('cfg-useSpeedSensor').checked,
    useShiftSensor: document.getElementById('cfg-useShiftSensor').checked,
    shiftInterruptDurationMs: formInt('cfg-shiftInterruptDurationMs'),
    shiftInterruptCurrentThreshold: formInt('cfg-shiftInterruptCurrentThreshold'),
    temperatureSensor: formInt('cfg-temperatureSensor'),
    lightsMode: formInt('cfg-lightsMode'),
    usePushWalk: document.getElementById('cfg-usePushWalk').checked,
    walkModeDisplay: formInt('cfg-walkModeDisplay'),
    assistStartupLevel: formInt('cfg-assistStartupLevel'),
    assistModeSelect: formInt('cfg-assistModeSelect'),
    standardLevels: [],
    sportLevels: []
  };

  ['std', 'sport'].forEach(type => {
    const targetArr = (type === 'std') ? cfg.standardLevels : cfg.sportLevels;
    for (let i = 0; i < 10; ++i) {
      targetArr.push({
        current: formInt(`${type}-curr-${i}`),
        maxThrottle: formInt(`${type}-throt-${i}`),
        cadence: formInt(`${type}-cad-${i}`),
        speed: formInt(`${type}-spd-${i}`),
        pas: document.getElementById(`${type}-pas-${i}`).checked,
        throttle: document.getElementById(`${type}-th-${i}`).checked,
        cruise: document.getElementById(`${type}-cr-${i}`).checked,
        overrideCadence: document.getElementById(`${type}-oc-${i}`).checked,
        overrideSpeed: document.getElementById(`${type}-os-${i}`).checked,
        pasVariable: document.getElementById(`${type}-pv-${i}`) ? document.getElementById(`${type}-pv-${i}`).checked : false,
        pasTorque: document.getElementById(`${type}-pt-${i}`) ? document.getElementById(`${type}-pt-${i}`).checked : false,
        // Torque amplification has no input box; carry over the value the
        // controller reported instead of overwriting it with a made-up default.
        torqueAmp: formTorque[type][i]
      });
    }
  });

  return cfg;
}

// Returns the name of the first required field that was never read/entered,
// or null when the form holds a complete configuration.
function findMissingConfigField(cfg) {
  const scalars = [
    'maxCurrent', 'currentRamp', 'maxBatteryVolts', 'lowCutoffVolts', 'maxSpeed',
    'wheelSizeInch', 'speedSensorSignals', 'freedomUnits', 'pasStartDelay',
    'pasStopDelayMs', 'pasKeepCurrentPercent', 'pasKeepCurrentCadenceRpm',
    'throttleStartMv', 'throttleEndMv', 'throttleStartPercent',
    'throttleGlobalSpdLimOpt', 'throttleGlobalSpdLimPercent',
    'shiftInterruptDurationMs', 'shiftInterruptCurrentThreshold',
    'temperatureSensor', 'lightsMode', 'walkModeDisplay', 'assistStartupLevel',
    'assistModeSelect'
  ];
  for (const key of scalars) {
    if (cfg[key] === null || cfg[key] === undefined || Number.isNaN(cfg[key])) return key;
  }
  for (const type of ['standardLevels', 'sportLevels']) {
    for (let i = 0; i < cfg[type].length; ++i) {
      for (const key of ['current', 'maxThrottle', 'cadence', 'speed', 'torqueAmp']) {
        const v = cfg[type][i][key];
        if (v === null || v === undefined || Number.isNaN(v)) return `${type} level ${i} ${key}`;
      }
    }
  }
  return null;
}

async function writeConfigToController() {
  if (!configLoaded) {
    showToast('Read the configuration from the controller first.');
    return;
  }

  const cfg = collectConfigFromForm();
  const missing = findMissingConfigField(cfg);
  if (missing) {
    showToast('Cannot save: "' + missing + '" is empty. Read from the controller first.');
    return;
  }

  showToast('Flashing configuration to EEPROM...');
  document.getElementById('badge-mode').innerText = 'Writing Config...';
  document.getElementById('badge-mode').className = 'badge badge-warning';

  try {
    const res = await fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg)
    });
    const result = await res.json();
    if (result.success) {
      showToast('Configuration successfully written to controller!');
    } else {
      showToast('Error: Controller rejected configuration!');
    }
  } catch (e) {
    showToast('Failed to write configuration!');
  } finally {
    document.getElementById('badge-mode').innerText = 'Transparent Bridge';
    document.getElementById('badge-mode').className = 'badge badge-active';
  }
}

async function calibrateVoltage() {
  const volts = parseFloat(document.getElementById('cal-volts').value);
  if (isNaN(volts) || volts < 20 || volts > 70) {
    alert('Please enter a valid battery voltage between 20V and 70V');
    return;
  }
  showToast('Calibrating voltage ADC...');
  try {
    const res = await fetch('/api/calibrate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ voltage: volts })
    });
    const data = await res.json();
    if (data.success) {
      showToast('Voltage calibration successful!');
    } else {
      showToast('Controller calibration failed.');
    }
  } catch(e) {
    showToast('Error communicating with controller!');
  }
}

async function resetFactoryConfig() {
  if (!confirm('Are you sure you want to reset the controller configuration to factory BBS-FW defaults?')) return;
  showToast('Resetting controller...');
  try {
    const res = await fetch('/api/reset', { method: 'POST' });
    const data = await res.json();
    if (data.success) {
      showToast('Controller reset completed.');
      fetchConfigFromController();
    } else {
      showToast('Reset failed.');
    }
  } catch(e) {
    showToast('Communication error.');
  }
}

function exportJsonFile() {
  const cfg = collectConfigFromForm();
  const blob = new Blob([JSON.stringify(cfg, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'bbs_hd_profile.json';
  a.click();
}

function importJsonFile(evt) {
  const file = evt.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = e => {
    try {
      const cfg = JSON.parse(e.target.result);
      populateConfigForm(cfg);
      configLoaded = true;
      showToast('Loaded profile from file.');
    } catch(err) {
      alert('Invalid JSON configuration file');
    }
  };
  reader.readAsText(file);
}

// ---------- Debug Console ----------
// Tracing is OFF by default. The firmware ring buffer is only enabled on
// demand, and the UI only polls while it is enabled AND this tab is visible,
// so a weak Wi-Fi link is never saturated by background trace traffic.
let debugEnabled = false;
let debugTimer = null;
let lastTraceSeq = 0;
let tracePaused = false;
let traceAutoscroll = true;
let traceFilters = { drx: true, dtx: true, crx: true, ctx: true, sys: true };
const dirLabels = { drx: 'DISP→MCU', dtx: 'MCU→DISP', crx: 'CTRL→MCU', ctx: 'MCU→CTRL', sys: 'SYSTEM' };
const dirCls = { drx: 't-drx', dtx: 't-dtx', crx: 't-crx', ctx: 't-ctx', sys: 't-sys' };
const MAX_TERMINAL_LINES = 400;

function bytesToHex(bytes) {
  let h = '';
  for (let i = 0; i < bytes.length; ++i) {
    h += ' ' + bytes[i].toString(16).toUpperCase().padStart(2, '0');
  }
  return h.trim();
}

function bytesToAscii(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; ++i) {
    const c = bytes[i];
    s += (c >= 32 && c < 127) ? String.fromCharCode(c) : '.';
  }
  return s;
}

function escapeHtml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function formatMs(ms) {
  const sec = Math.floor(ms / 1000);
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  const r = ms % 1000;
  return m + ':' + s.toString().padStart(2, '0') + '.' + r.toString().padStart(3, '0');
}

function appendTerminalLine(html, cls) {
  const term = document.getElementById('terminal');
  const div = document.createElement('div');
  div.className = 'tline' + (cls ? ' ' + cls : '');
  div.innerHTML = html;
  term.appendChild(div);

  // Trim old lines
  while (term.children.length > MAX_TERMINAL_LINES) {
    term.removeChild(term.firstChild);
  }

  if (traceAutoscroll && !tracePaused) {
    term.scrollTop = term.scrollHeight;
  }
}

function toggleDebugPause() {
  tracePaused = !tracePaused;
  const btn = document.getElementById('btn-pause');
  btn.textContent = tracePaused ? 'Paused' : 'Running';
  btn.classList.toggle('on', !tracePaused);
}

function toggleDebugAutoscroll() {
  traceAutoscroll = !traceAutoscroll;
  const btn = document.getElementById('btn-autoscroll');
  btn.classList.toggle('on', traceAutoscroll);
}

function toggleDebugFilter(dir) {
  traceFilters[dir] = !traceFilters[dir];
  const btn = document.getElementById('f-' + dir);
  btn.classList.toggle('on', traceFilters[dir]);
}

function clearDebugConsole() {
  document.getElementById('terminal').innerHTML = '';
  lastTraceSeq = 0;
}

function debugTabVisible() {
  const active = document.querySelector('.tab-content.active');
  return active !== null && active.id === 'tab-debug';
}

function setDebugUiState() {
  const btn = document.getElementById('btn-debug-enable');
  if (btn) {
    btn.textContent = debugEnabled ? 'Debug: ON' : 'Debug: OFF';
    btn.classList.toggle('on', debugEnabled);
  }
  const stats = document.getElementById('debug-stats');
  if (stats && !debugEnabled) stats.innerText = 'disabled';
}

// Only poll while tracing is enabled AND the console tab is on screen.
function updateDebugPolling() {
  const shouldPoll = debugEnabled && debugTabVisible();
  if (shouldPoll && debugTimer === null) {
    pollSerialTrace();
    debugTimer = setInterval(pollSerialTrace, 500);
  } else if (!shouldPoll && debugTimer !== null) {
    clearInterval(debugTimer);
    debugTimer = null;
  }
  setDebugUiState();
}

async function toggleDebugEnabled() {
  const next = !debugEnabled;
  try {
    const res = await fetch('/api/debug', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ enabled: next })
    });
    if (!res.ok) throw new Error('debug toggle failed');
    debugEnabled = next;
  } catch (e) {
    showToast('Could not change debug tracing state.');
    return;
  }

  if (debugEnabled) {
    lastTraceSeq = 0;
    document.getElementById('terminal').innerHTML = '';
  }
  updateDebugPolling();
}

// Known Bafang opcode & command descriptions for the parsed debug column
const BAFANG_PAS_CODES = {0x00:0, 0x01:1, 0x0b:2, 0x0c:3, 0x0d:4, 0x02:5, 0x15:6, 0x16:7, 0x17:8, 0x03:9, 0x06:6};
const BAFANG_PAS_NAMES = ['0','1','2','3','4','5','6 (Walk)','6','7','8','9'];

function parsePacket(bytes, dir) {
  if (!bytes || bytes.length < 2) return null;
  // Display -> Controller reads (0x11 prefix)
  if (bytes[0] === 0x11) {
    switch (bytes[1]) {
      case 0x08: return '⤓ Status';
      case 0x0a: return '⤓ Current';
      case 0x11: return '⤓ Battery %';
      case 0x20: return '⤓ Speed';
      case 0x21: return '⤓ Unknown (0x21)';
      case 0x22: return '⤓ Range';
      case 0x24: return '⤓ Voltage';
      case 0x25: return '⤓ Unknown (0x25)';
      case 0x31: return '⤓ Moving?';
    }
  }
  // Display -> Controller writes (0x16 prefix)
  if (bytes[0] === 0x16) {
    switch (bytes[1]) {
      case 0x0b: {
        let lvl = (bytes.length >= 3) ? (BAFANG_PAS_CODES[bytes[2]] ?? bytes[2]) : '?';
        return '✎ PAS → Level ' + lvl;
      }
      case 0x0c: {
        let mode = (bytes.length >= 3) ? (bytes[2] === 0x04 ? 'Sport' : 'Standard') : '?';
        return '✎ Mode → ' + mode;
      }
      case 0x1a: {
        let st = (bytes.length >= 3) ? (bytes[2] === 0xf1 ? 'ON' : 'OFF') : '?';
        return '✎ Lights → ' + st;
      }
      case 0x1f: return '✎ Speed Limit';
    }
  }
  // Config tool reads (0x01 prefix)
  if (bytes[0] === 0x01) {
    switch (bytes[1]) {
      case 0x01: {
        if (bytes.length >= 8 && dir === 'crx') {
          return 'FW: v' + bytes[2] + '.' + bytes[3] + '.' + bytes[4] + ' cfg=v' + bytes[5] + ' type=' + bytes[6];
        }
        return 'FW Version query';
      }
      case 0x03: return 'Read Config';
      case 0x04: return 'Read Status';
    }
  }
  // Config tool writes (0x02 prefix)
  if (bytes[0] === 0x02) {
    switch (bytes[1]) {
      case 0xf0: return 'EventLog ' + ((bytes.length >= 3 && bytes[2]) ? 'ON' : 'OFF');
      case 0xf1: return 'Write Config';
      case 0xf2: return 'Reset Config';
      case 0xf3: {
        if (bytes.length >= 5) {
          let v = ((bytes[2] << 8) | bytes[3]) / 100.0;
          return 'Calibrate V → ' + v.toFixed(1) + 'V';
        }
        return 'Calibrate Voltage';
      }
    }
  }
  // Event log frames
  if (bytes[0] === 0xee && bytes.length >= 3) {
    return 'Event #' + bytes[1];
  }
  if (bytes[0] === 0xed && bytes.length >= 5) {
    let d = (bytes[2] << 8) | bytes[3];
    return 'Event #' + bytes[1] + ' data=' + d;
  }
  // Controller response parsing (crx = controller→middleman)
  if (dir === 'crx') {
    // These are sniffed response bodies; context depends on the last display query
    if (bytes.length === 1) return 'Status: 0x' + bytes[0].toString(16).toUpperCase();
    if (bytes.length === 2) return 'Amps×2=' + bytes[0] + ' (' + (bytes[0]/2).toFixed(1) + 'A) / Bat%=' + bytes[0];
    if (bytes.length === 3) {
      let rpm = (bytes[0] << 8) | bytes[1];
      return 'RPM=' + rpm + ' / V×10=' + ((bytes[0] << 8) | bytes[1]);
    }
    if (bytes.length === 4 && bytes[0] === 0x02) {
      if (bytes[1] === 0xf1) return 'Config write: ' + (bytes[2] ? 'OK' : 'FAIL');
    }
  }
  return null;
}

// Group consecutive byte events by source direction so multi-byte packets
// (e.g. 11 08) stay together for the parsePacket column.  Forwarded copies
// (ctx=MCU→CTRL, dtx=MCU→DISP) are always single-byte.
function renderTraceEvents(bytes, texts) {
  function flushGroup(group) {
    if (!group) return;
    const hex = bytesToHex(group.bytes);
    const asc = bytesToAscii(group.bytes);
    const tag = group.dir;
    const parsed = parsePacket(group.bytes, tag) || '';
    const ln = '<span class="t-time">' + formatMs(group.startTs) + '</span>'
      + '<span class="t-dir ' + dirCls[tag] + '">' + dirLabels[tag] + '</span>'
      + '<span class="t-hex">' + hex + '</span>'
      + '  <span class="t-ascii">|' + asc + '|</span>'
      + (parsed ? '  <span class="t-parsed">' + escapeHtml(parsed) + '</span>' : '');
    appendTerminalLine(ln, 't-' + tag);
  }

  // Merge and sort by sequence number
  const all = [];
  bytes.forEach(b => { if (traceFilters[b.d]) all.push({...b, kind: 'b'}); });
  texts.forEach(t => { if (traceFilters.sys) all.push({...t, kind: 't'}); });
  all.sort((a, b) => a.s - b.s);

  // Group source bytes (drx, crx) when consecutive in same direction.
  // Forwarded copies (dtx, ctx) are always flushed as single-bytes so they
  // don't interleave with the source groups.
  let group = null;
  for (let i = 0; i < all.length; ++i) {
    const evt = all[i];
    if (evt.kind === 't') {
      flushGroup(group); group = null;
      const ln = '<span class="t-time">' + formatMs(evt.t) + '</span>'
        + '<span class="t-dir t-sys">SYSTEM</span>'
        + '<span style="color:#94a3b8;">' + escapeHtml(evt.msg) + '</span>';
      appendTerminalLine(ln, 't-sys');
    } else {
      const isSource = (evt.d === 'drx' || evt.d === 'crx');
      if (isSource && group && group.dir === evt.d) {
        // Extend current source group
        group.bytes.push(evt.b);
      } else {
        // Flush old group, start new
        flushGroup(group);
        group = { dir: evt.d, startTs: evt.t, bytes: [evt.b] };
      }
    }
  }
  flushGroup(group);
}

async function pollSerialTrace() {
  if (!debugEnabled || tracePaused || document.hidden) return;
  try {
    const url = '/api/serial-trace?after=' + lastTraceSeq;
    const res = await fetch(url);
    if (!res.ok) return;
    const data = await res.json();

    const nBytes = (data.bytes || []).length;
    const nTexts = (data.texts || []).length;

    if (nBytes > 0 || nTexts > 0) {
      renderTraceEvents(data.bytes || [], data.texts || []);
    }

    if (data.seq !== undefined) {
      lastTraceSeq = data.seq;
    }

    // Show stats
    const term = document.getElementById('terminal');
    const statsEl = document.getElementById('debug-stats');
    statsEl.innerText = term.children.length + ' lines' + (data.dropped > 0 ? ' (' + data.dropped + ' dropped)' : '');
  } catch (e) {}
}
// No global interval: polling is started/stopped by updateDebugPolling().

// ---------- Firmware Update ----------
async function fetchFirmwareInfo() {
  try {
    const res = await fetch('/api/info');
    const info = await res.json();
    document.getElementById('fw-version').textContent = info.fwVersion || 'unknown';
    document.getElementById('fw-build').textContent = info.fwBuild || 'unknown';

    const pill = document.getElementById('status-wifi');
    if (pill) {
      pill.textContent = 'Wi-Fi: ' + (info.wifiMode || '?');
      pill.title = 'STA IP: ' + (info.staIP || '-')
        + '  |  AP IP: ' + (info.apIP || 'Off')
        + '  |  http://' + (info.mdnsHost || 'bbshd.local') + '/';
    }
  } catch(e) {}
}
fetchFirmwareInfo();

function startOtaUpload() {
  const fileInput = document.getElementById('fw-file');
  const file = fileInput.files[0];
  if (!file) {
    showToast('Please select a firmware .bin file first');
    return;
  }

  if (!file.name.endsWith('.bin')) {
    showToast('Only .bin firmware files are accepted');
    return;
  }

  const btn = document.getElementById('btn-ota-upload');
  btn.disabled = true;
  btn.textContent = 'Uploading...';

  const progressWrapper = document.getElementById('ota-progress-wrapper');
  const progressBar = document.getElementById('ota-progress-bar');
  const progressText = document.getElementById('ota-progress-text');
  progressWrapper.style.display = 'block';

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);

  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      progressBar.style.width = pct + '%';
      progressText.textContent = pct + '% (' + formatSize(e.loaded) + ' / ' + formatSize(e.total) + ')';
    }
  };

  xhr.onload = function() {
    if (xhr.status === 200) {
      showToast('Update complete! Device is rebooting, reconnect in ~10 seconds.');
      progressText.textContent = 'Done! Rebooting...';
    } else {
      showToast('Update failed: ' + (xhr.responseText || 'Unknown error'));
      progressText.textContent = 'Failed';
      btn.disabled = false;
      btn.textContent = 'Upload Firmware';
    }
  };

  xhr.onerror = function() {
    showToast('Network error during upload');
    progressText.textContent = 'Network error';
    btn.disabled = false;
    btn.textContent = 'Upload Firmware';
  };

  xhr.send(file);
}

function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
  return (bytes / 1048576).toFixed(1) + ' MB';
}

function openWifiModal() {
  const ssid = prompt("Connect Middleman to Wi-Fi Network (SSID):", "");
  if (!ssid) return;
  const pass = prompt("Wi-Fi Password:", "");
  fetch('/api/wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: ssid, pass: pass || "" })
  }).then(() => {
    showToast('Joining ' + ssid + '... if it fails the fallback AP returns. Reach the UI at http://bbshd.local/');
    setTimeout(fetchFirmwareInfo, 15000);  // refresh the Wi-Fi pill once the join settles
  });
}
</script>
</body>
</html>
)rawliteral";

#endif // WEB_CONTENT_H
