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
          <input type="number" class="form-control" id="cfg-maxCurrent" min="5" max="33" value="30">
          <small>Safe BBSHD limit: 30A (stock controller max is 33A with bbs-fw).</small>
        </div>
        <div class="form-group">
          <label>Current Ramp Rate (A/s)</label>
          <input type="number" class="form-control" id="cfg-currentRamp" min="1" max="255" value="50">
          <small>Amperes per second rate of change. Lower = smoother acceleration.</small>
        </div>
        <div class="form-group">
          <label>Max Speed Limit (km/h)</label>
          <input type="number" class="form-control" id="cfg-maxSpeed" min="0" max="180" value="50">
        </div>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Battery Voltage & Wheel</h3>
        <div class="form-group">
          <label>Max Battery Voltage (V)</label>
          <input type="number" step="0.1" class="form-control" id="cfg-maxBatteryVolts" min="1" max="100" value="58.8">
          <small>54.6V for 48V pack (13S), 58.8V for 52V pack (14S).</small>
        </div>
        <div class="form-group">
          <label>Low Voltage Cutoff LVC (V)</label>
          <input type="number" class="form-control" id="cfg-lowCutoffVolts" min="1" max="100" value="41">
          <small>39V-41V for 52V pack, 36V-38V for 48V pack.</small>
        </div>
        <div class="form-row">
          <div class="form-group">
            <label>Wheel Size (inch)</label>
            <input type="number" step="0.1" class="form-control" id="cfg-wheelSizeInch" min="10" max="36" value="27.5">
          </div>
          <div class="form-group">
            <label>Speed Sensor Signals</label>
            <input type="number" class="form-control" id="cfg-speedSensorSignals" min="1" max="10" value="1">
          </div>
        </div>
        <div class="form-group">
          <label>Display Units</label>
          <select class="form-control" id="cfg-freedomUnits">
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
          <input type="number" class="form-control" id="cfg-pasStartDelay" min="0" max="24" value="3">
          <small>Pulses needed before assist engages. 2-3 provides responsive startup.</small>
        </div>
        <div class="form-group">
          <label>PAS Stop Delay (ms)</label>
          <input type="number" step="10" class="form-control" id="cfg-pasStopDelayMs" min="50" max="1000" value="200">
          <small>Delay before motor stops after pedaling stops. 150-250ms is optimal.</small>
        </div>
        <div class="form-group">
          <label>PAS Keep Current (%)</label>
          <input type="number" class="form-control" id="cfg-pasKeepCurrentPercent" min="10" max="100" value="80">
          <small>Current maintained during high cadence pedaling.</small>
        </div>
        <div class="form-group">
          <label>PAS Keep Current Cadence (RPM)</label>
          <input type="number" class="form-control" id="cfg-pasKeepCurrentCadenceRpm" min="0" max="255" value="255">
        </div>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Throttle Options</h3>
        <div class="form-row">
          <div class="form-group">
            <label>Start Voltage (mV)</label>
            <input type="number" class="form-control" id="cfg-throttleStartMv" min="200" max="2500" value="1100">
          </div>
          <div class="form-group">
            <label>End Voltage (mV)</label>
            <input type="number" class="form-control" id="cfg-throttleEndMv" min="2500" max="5000" value="3600">
          </div>
        </div>
        <div class="form-group">
          <label>Throttle Initial Power Kick (%)</label>
          <input type="number" class="form-control" id="cfg-throttleStartPercent" min="0" max="100" value="5">
        </div>
        <div class="form-group">
          <label>Global Speed Limit Mode</label>
          <select class="form-control" id="cfg-throttleGlobalSpdLimOpt">
            <option value="0">Disabled (Follows PAS)</option>
            <option value="1">Enabled (Custom Limit)</option>
            <option value="2">Standard Levels</option>
          </select>
        </div>
        <div class="form-group">
          <label>Global Speed Limit (%)</label>
          <input type="number" class="form-control" id="cfg-throttleGlobalSpdLimPercent" min="0" max="100" value="100">
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
          <label><input type="checkbox" id="cfg-useSpeedSensor" checked> External Wheel Speed Sensor</label>
        </div>
        <div class="form-group">
          <label><input type="checkbox" id="cfg-useShiftSensor" checked> Shift Sensor Active</label>
        </div>
        <div class="form-row">
          <div class="form-group">
            <label>Shift Cut Duration (ms)</label>
            <input type="number" class="form-control" id="cfg-shiftInterruptDurationMs" min="50" max="2000" value="450">
          </div>
          <div class="form-group">
            <label>Shift Current Cut (%)</label>
            <input type="number" class="form-control" id="cfg-shiftInterruptCurrentThreshold" min="0" max="100" value="15">
          </div>
        </div>
        <div class="form-group">
          <label>Temperature Sensors</label>
          <select class="form-control" id="cfg-temperatureSensor">
            <option value="0">Disabled</option>
            <option value="1">Controller Only</option>
            <option value="2">Motor Only</option>
            <option value="3" selected>All (Controller & Motor)</option>
          </select>
        </div>
      </div>

      <div class="card">
        <h3 class="card-title" style="margin-bottom:14px;">Auxiliary Features</h3>
        <div class="form-group">
          <label>Lights Mode</label>
          <select class="form-control" id="cfg-lightsMode">
            <option value="0">Default (Display controlled)</option>
            <option value="1">Disabled</option>
            <option value="2">Always On</option>
            <option value="3">Brake Light</option>
          </select>
        </div>
        <div class="form-group">
          <label><input type="checkbox" id="cfg-usePushWalk" checked> Push / Walk Assist Enabled</label>
        </div>
        <div class="form-group">
          <label>Walk Mode Display Data Field</label>
          <select class="form-control" id="cfg-walkModeDisplay">
            <option value="0">Speed</option>
            <option value="1">Temperature (&deg;C)</option>
            <option value="2">Requested Power (W)</option>
            <option value="3">Battery Percent (%)</option>
          </select>
        </div>
        <div class="form-group">
          <label>Startup Assist Level (0 - 9)</label>
          <input type="number" class="form-control" id="cfg-assistStartupLevel" min="0" max="9" value="1">
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
          <input type="number" step="0.1" class="form-control" id="cal-volts" placeholder="e.g. 52.4" value="52.0">
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
          <li><strong>Controller RX (GPIO 18)</strong> &larr; Controller TXD (Pin 5 on motor cable)</li>
          <li><strong>Controller TX (GPIO 17)</strong> &rarr; Controller RXD (Pin 4 on motor cable)</li>
          <li><strong>Display RX (GPIO 16)</strong> &larr; Display TXD (Pin 4 on display cable)</li>
          <li><strong>Display TX (GPIO 15)</strong> &rarr; Display RXD (Pin 5 on display cable)</li>
          <li><strong>Status LED (GPIO 48)</strong> &rarr; Activity Indicator</li>
        </ul>
      </div>
    </div>
  </div>
</main>

<div class="toast" id="toast"></div>

<script>
let activeConfig = null;
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
        <td><input type="number" min="0" max="100" id="${type}-curr-${i}" value="${i === 0 ? 0 : 10 + i * 10}"></td>
        <td><input type="number" min="0" max="100" id="${type}-throt-${i}" value="100"></td>
        <td><input type="number" min="0" max="100" id="${type}-cad-${i}" value="100"></td>
        <td><input type="number" min="0" max="100" id="${type}-spd-${i}" value="${i === 0 ? 100 : 40 + i * 6}"></td>
        <td><input type="checkbox" id="${type}-pas-${i}" ${i > 0 ? 'checked' : ''}></td>
        <td><input type="checkbox" id="${type}-th-${i}" checked></td>
        <td><input type="checkbox" id="${type}-cr-${i}"></td>
        <td><input type="checkbox" id="${type}-oc-${i}" ${type === 'sport' ? 'checked' : ''}></td>
        <td><input type="checkbox" id="${type}-os-${i}"></td>
      `;
      tbody.appendChild(tr);
    }
  });
}
buildLevelTables();

// Live Telemetry Polling
async function pollTelemetry() {
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
      document.getElementById('val-status').innerText = 'Err 0x' + d.statusCode.toString(16).toUpperCase();
      document.getElementById('val-status').className = 'badge badge-error';
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
setInterval(pollTelemetry, 600);

// Live Event Log Polling
async function pollEvents() {
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
setInterval(pollEvents, 1200);

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
    if (!res.ok) throw new Error('Failed to read config');
    activeConfig = await res.json();
    populateConfigForm(activeConfig);
    showToast('Configuration loaded successfully!');
  } catch (e) {
    showToast('Error reading from controller!');
  } finally {
    document.getElementById('badge-mode').innerText = 'Transparent Bridge';
    document.getElementById('badge-mode').className = 'badge badge-active';
  }
}

function populateConfigForm(cfg) {
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
    });
  }
}

function collectConfigFromForm() {
  const cfg = {
    maxCurrent: parseInt(document.getElementById('cfg-maxCurrent').value),
    currentRamp: parseInt(document.getElementById('cfg-currentRamp').value),
    maxBatteryVolts: parseFloat(document.getElementById('cfg-maxBatteryVolts').value),
    lowCutoffVolts: parseInt(document.getElementById('cfg-lowCutoffVolts').value),
    maxSpeed: parseInt(document.getElementById('cfg-maxSpeed').value),
    wheelSizeInch: parseFloat(document.getElementById('cfg-wheelSizeInch').value),
    speedSensorSignals: parseInt(document.getElementById('cfg-speedSensorSignals').value),
    freedomUnits: parseInt(document.getElementById('cfg-freedomUnits').value),
    pasStartDelay: parseInt(document.getElementById('cfg-pasStartDelay').value),
    pasStopDelayMs: parseInt(document.getElementById('cfg-pasStopDelayMs').value),
    pasKeepCurrentPercent: parseInt(document.getElementById('cfg-pasKeepCurrentPercent').value),
    pasKeepCurrentCadenceRpm: parseInt(document.getElementById('cfg-pasKeepCurrentCadenceRpm').value),
    throttleStartMv: parseInt(document.getElementById('cfg-throttleStartMv').value),
    throttleEndMv: parseInt(document.getElementById('cfg-throttleEndMv').value),
    throttleStartPercent: parseInt(document.getElementById('cfg-throttleStartPercent').value),
    throttleGlobalSpdLimOpt: parseInt(document.getElementById('cfg-throttleGlobalSpdLimOpt').value),
    throttleGlobalSpdLimPercent: parseInt(document.getElementById('cfg-throttleGlobalSpdLimPercent').value),
    useSpeedSensor: document.getElementById('cfg-useSpeedSensor').checked,
    useShiftSensor: document.getElementById('cfg-useShiftSensor').checked,
    shiftInterruptDurationMs: parseInt(document.getElementById('cfg-shiftInterruptDurationMs').value),
    shiftInterruptCurrentThreshold: parseInt(document.getElementById('cfg-shiftInterruptCurrentThreshold').value),
    temperatureSensor: parseInt(document.getElementById('cfg-temperatureSensor').value),
    lightsMode: parseInt(document.getElementById('cfg-lightsMode').value),
    usePushWalk: document.getElementById('cfg-usePushWalk').checked,
    walkModeDisplay: parseInt(document.getElementById('cfg-walkModeDisplay').value),
    assistStartupLevel: parseInt(document.getElementById('cfg-assistStartupLevel').value),
    standardLevels: [],
    sportLevels: []
  };

  ['std', 'sport'].forEach(type => {
    const targetArr = (type === 'std') ? cfg.standardLevels : cfg.sportLevels;
    for (let i = 0; i < 10; ++i) {
      targetArr.push({
        current: parseInt(document.getElementById(`${type}-curr-${i}`).value),
        maxThrottle: parseInt(document.getElementById(`${type}-throt-${i}`).value),
        cadence: parseInt(document.getElementById(`${type}-cad-${i}`).value),
        speed: parseInt(document.getElementById(`${type}-spd-${i}`).value),
        pas: document.getElementById(`${type}-pas-${i}`).checked,
        throttle: document.getElementById(`${type}-th-${i}`).checked,
        cruise: document.getElementById(`${type}-cr-${i}`).checked,
        overrideCadence: document.getElementById(`${type}-oc-${i}`).checked,
        overrideSpeed: document.getElementById(`${type}-os-${i}`).checked,
        torqueAmp: 1.0
      });
    }
  });

  return cfg;
}

async function writeConfigToController() {
  const cfg = collectConfigFromForm();
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
      showToast('Loaded profile from file.');
    } catch(err) {
      alert('Invalid JSON configuration file');
    }
  };
  reader.readAsText(file);
}

function openWifiModal() {
  const ssid = prompt("Connect Middleman to Wi-Fi Network (SSID):", "");
  if (!ssid) return;
  const pass = prompt("Wi-Fi Password:", "");
  fetch('/api/wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: ssid, pass: pass || "" })
  }).then(() => showToast('Wi-Fi credentials saved. Restarting Wi-Fi...'));
}
</script>
</body>
</html>
)rawliteral";

#endif // WEB_CONTENT_H
