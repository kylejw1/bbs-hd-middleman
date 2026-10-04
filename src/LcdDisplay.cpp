#include "LcdDisplay.h"
#include "Telemetry.h"
#include "SerialBridge.h"
#include "BbsFwProtocol.h"
#include "DebugLog.h"

// The global singleton, regardless of whether a display is compiled in.
LcdDisplay Display;

#if defined(LCD_DISPLAY_ST7789) || defined(LCD_DISPLAY_GC9A01)

// =============================================================================
// Constructor
// =============================================================================
LcdDisplay::LcdDisplay()
    : _tft()
    , _sprRow0(&_tft)
    , _sprRow1(&_tft)
    , _sprChart(&_tft)
    , _last{}
    , _initialised(false)
    , _spritesOk(false)
    , _lastRefreshMs(0)
    , _dirtyRow0(true)
    , _dirtyRow1(true)
    , _dirtyChart(true)
{
    memset(&_last, 0xFF, sizeof(_last)); // force first paint
    _last.currentAmps = -1.0f;
}

// =============================================================================
// begin()
// =============================================================================
void LcdDisplay::begin() {
    const uint32_t heapBefore = ESP.getFreeHeap();

    _tft.init();
    _tft.setRotation(LCD_ROTATION);
    _tft.fillScreen(LcdColor::BG);
    _tft.setSwapBytes(true);

    // Sprites are the real memory cost of this feature -- two text rows plus the
    // chart, which on the round target is ~115 KB. Allocate them, and if any
    // allocation fails disable the display outright: pushing an unallocated
    // sprite is worse than showing nothing.
    //
    // These go to the USB console rather than Debug.trace because tracing is off
    // by default (DEBUG_TRACE_ENABLED_DEFAULT 0), and this is the only boot-time
    // evidence that the panel came up or why it did not.
    _spritesOk  = (_sprRow0.createSprite(W, ROW0_H) != nullptr);
    _spritesOk &= (_sprRow1.createSprite(W, ROW1_H) != nullptr);
    _spritesOk &= (_sprChart.createSprite(CHART_W, CHART_H) != nullptr);

    const uint32_t spriteBytes =
        ((uint32_t)W * ROW0_H + (uint32_t)W * ROW1_H + (uint32_t)CHART_W * CHART_H) * 2;

    if (!_spritesOk) {
        Serial.printf("[LCD] sprite alloc failed (%u B needed, free heap %u) - display disabled\n",
                      spriteBytes, ESP.getFreeHeap());
        return;
    }

    _sprRow0.setSwapBytes(true);
    _sprRow1.setSwapBytes(true);
    _sprChart.setSwapBytes(true);

    _initialised = true;
    Serial.printf("[LCD] %s ready %dx%d, sprites %u B, free heap %u -> %u\n",
                  LCD_DISPLAY_NAME, W, H, spriteBytes, heapBefore, ESP.getFreeHeap());
}

// =============================================================================
// process()  — called from loop(); rate-limited to LCD_REFRESH_MS
// =============================================================================
void LcdDisplay::process() {
    if (!_initialised) return;

    uint32_t now = millis();
    if (now - _lastRefreshMs < LCD_REFRESH_MS) return;
    _lastRefreshMs = now;

    _snapshotAndDiff();

    if (_dirtyRow0) {
        _renderRow0();
        _dirtyRow0 = false;
    }
    if (_dirtyRow1) {
        _renderRow1();
        _dirtyRow1 = false;
    }
    if (_dirtyChart) {
        _renderChart();
        _dirtyChart = false;
    }
}

// =============================================================================
// _snapshotAndDiff() — pull fresh telemetry, compare with last, set dirty flags
// =============================================================================
void LcdDisplay::_snapshotAndDiff() {
    // --- Read live telemetry via direct getters (all are simple value reads) ---
    bool    hasTgt  = Telemetry.hasTargetTelemetry();
    uint8_t cadRpm  = hasTgt ? (uint8_t)(Telemetry.getCachedCadenceRpmX10() / 10) : 0;
    uint8_t motRpm  = hasTgt ? (uint8_t)(Telemetry.getCachedMotorRpmX10()   / 10) : 0;
    uint8_t tgtI    = hasTgt ? Telemetry.getCachedTargetCurrentPct()              : 0;
    uint8_t tgtS    = hasTgt ? Telemetry.getCachedTargetSpeedPct()                : 0;
    uint8_t battPct = Telemetry.getCachedBatteryPercent();
    float   curAmps = Telemetry.getCachedCurrentAmps();
    uint16_t watts  = (uint16_t)constrain(curAmps * Telemetry.getCachedVoltage(), 0.0f, 9999.0f);
    uint8_t  pal    = Telemetry.getCachedAssistLevel();
    uint8_t  opMode = Telemetry.getCachedOperationMode(); // 0=Std 1=Sport

    // Config curve data — cached from the last readConfig/writeConfig
    uint8_t maxI    = Bridge.getConfigMaxCurrentAmps();
    uint8_t keepPct = Bridge.getConfigPasKeepCurrentPct();
    uint8_t keepRpm = Bridge.getConfigPasKeepCurrentRpm();

    // --- Detect row-0 changes ---
    if (hasTgt  != _last.hasTargetTelemetry ||
        cadRpm  != _last.cadenceRpm         ||
        motRpm  != _last.motorRpm           ||
        tgtI    != _last.targetCurrentPct   ||
        tgtS    != _last.targetSpeedPct) {
        _dirtyRow0 = true;
    }

    // --- Detect row-1 changes ---
    if (battPct != _last.batteryPct          ||
        fabsf(curAmps - _last.currentAmps) > 0.05f ||
        watts   != _last.powerWatts          ||
        pal     != _last.assistLevel         ||
        opMode  != _last.operationMode) {
        _dirtyRow1 = true;
    }

    // --- Detect chart changes ---
    bool configChanged = (maxI    != _last.maxCurrentAmps         ||
                          keepPct != _last.pasKeepCurrentPct       ||
                          keepRpm != _last.pasKeepCurrentCadenceRpm||
                          opMode  != _last.operationMode);
    bool cursorChanged = _dirtyRow0; // cadence changed → cursor moves

    // Per-level snapshot for the active mode
    bool levelChanged = false;
    for (int i = 0; i < 10; ++i) {
        uint8_t cur = Bridge.getLevelCurrentPct(opMode, i);
        uint8_t cad = Bridge.getLevelCadencePct(opMode, i);
        uint8_t flg = Bridge.getLevelFlags(opMode, i);
        if (cur != _last.levelCurrentPct[i] ||
            cad != _last.levelCadencePct[i] ||
            flg != _last.levelFlags[i]) {
            levelChanged = true;
            _last.levelCurrentPct[i] = cur;
            _last.levelCadencePct[i] = cad;
            _last.levelFlags[i]      = flg;
        }
    }

    if (configChanged || levelChanged || cursorChanged) {
        _dirtyChart = true;
    }

    // --- Commit snapshot ---
    _last.hasTargetTelemetry       = hasTgt;
    _last.cadenceRpm               = cadRpm;
    _last.motorRpm                 = motRpm;
    _last.targetCurrentPct         = tgtI;
    _last.targetSpeedPct           = tgtS;
    _last.batteryPct               = battPct;
    _last.currentAmps              = curAmps;
    _last.powerWatts               = watts;
    _last.assistLevel              = pal;
    _last.operationMode            = opMode;
    _last.maxCurrentAmps           = maxI;
    _last.pasKeepCurrentPct        = keepPct;
    _last.pasKeepCurrentCadenceRpm = keepRpm;
}


// =============================================================================
// _renderRow0()  — cadence | motorRpm | targetI% | targetSpd%
//
// ST7789 draws all four tiles across; GC9A01 only fits two inside the circle
// and moves target-speed to row 1 (see _renderRow1 / _drawStatusLine).
// =============================================================================
void LcdDisplay::_renderRow0() {
    _sprRow0.fillSprite(LcdColor::BG);

    char buf[16];

    // Tile 0: Cadence RPM
    if (_last.hasTargetTelemetry) {
        _fmtInt(buf, sizeof(buf), _last.cadenceRpm);
    } else {
        strcpy(buf, "--");
    }
    _drawTile(_sprRow0, TILE_X0 + 0*TILE_W, 0, TILE_W, ROW0_H, "cad", buf, LcdColor::ACCENT_CYAN);

    // Tile 1: Motor RPM
    if (_last.hasTargetTelemetry) {
        _fmtInt(buf, sizeof(buf), _last.motorRpm);
    } else {
        strcpy(buf, "--");
    }
    _drawTile(_sprRow0, TILE_X0 + 1*TILE_W, 0, TILE_W, ROW0_H, "mRPM", buf, LcdColor::ACCENT_GREEN);

#if defined(LCD_DISPLAY_ST7789)
    // Tile 2: Target current %
    if (_last.hasTargetTelemetry) {
        snprintf(buf, sizeof(buf), "%d%%", _last.targetCurrentPct);
    } else {
        strcpy(buf, "--%");
    }
    _drawTile(_sprRow0, TILE_X0 + 2*TILE_W, 0, TILE_W, ROW0_H, "I tgt", buf, LcdColor::ACCENT_YELL);

    // Tile 3: Target speed %
    if (_last.hasTargetTelemetry) {
        snprintf(buf, sizeof(buf), "%d%%", _last.targetSpeedPct);
    } else {
        strcpy(buf, "--%");
    }
    _drawTile(_sprRow0, TILE_X0 + 3*TILE_W, 0, TILE_W, ROW0_H, "S tgt", buf, LcdColor::TEXT_WHITE);
#endif

    _sprRow0.pushSprite(0, ROW0_Y);
}

// =============================================================================
// _renderRow1()  — batt% | currentA | watts | PAS level
//
// GC9A01 keeps the two target percentages here instead, since its status line
// carries battery / current / watts / PAS.
// =============================================================================
void LcdDisplay::_renderRow1() {
    _sprRow1.fillSprite(LcdColor::BG);

    char buf[16];

#if defined(LCD_DISPLAY_ST7789)
    // Tile 0: Battery %
    uint16_t battColor = LcdColor::ACCENT_GREEN;
    if (_last.batteryPct < 20) battColor = LcdColor::ACCENT_RED;
    else if (_last.batteryPct < 40) battColor = LcdColor::ACCENT_YELL;
    snprintf(buf, sizeof(buf), "%d%%", _last.batteryPct);
    _drawTile(_sprRow1, TILE_X0 + 0*TILE_W, 0, TILE_W, ROW1_H, "batt", buf, battColor);

    // Tile 1: Current A (1 decimal)
    _fmtFloat1(buf, sizeof(buf), _last.currentAmps);
    _drawTile(_sprRow1, TILE_X0 + 1*TILE_W, 0, TILE_W, ROW1_H, "A", buf, LcdColor::TEXT_WHITE);

    // Tile 2: Watts
    _fmtInt(buf, sizeof(buf), _last.powerWatts);
    _drawTile(_sprRow1, TILE_X0 + 2*TILE_W, 0, TILE_W, ROW1_H, "W", buf, LcdColor::ACCENT_YELL);

    // Tile 3: PAS level + mode letter
    const char* modeChar = (_last.operationMode == 1) ? "S" : "P";
    snprintf(buf, sizeof(buf), "%s%d", modeChar, _last.assistLevel);
    _drawTile(_sprRow1, TILE_X0 + 3*TILE_W, 0, TILE_W, ROW1_H, "PAS", buf, LcdColor::ACCENT_CYAN);
#else
    // Tile 2: Target current %
    if (_last.hasTargetTelemetry) {
        snprintf(buf, sizeof(buf), "%d%%", _last.targetCurrentPct);
    } else {
        strcpy(buf, "--%");
    }
    _drawTile(_sprRow1, TILE_X0 + 0*TILE_W, 0, TILE_W, ROW1_H, "I tgt", buf, LcdColor::ACCENT_YELL);

    // Tile 3: Target speed %
    if (_last.hasTargetTelemetry) {
        snprintf(buf, sizeof(buf), "%d%%", _last.targetSpeedPct);
    } else {
        strcpy(buf, "--%");
    }
    _drawTile(_sprRow1, TILE_X0 + 1*TILE_W, 0, TILE_W, ROW1_H, "S tgt", buf, LcdColor::TEXT_WHITE);
#endif

    _sprRow1.pushSprite(0, ROW1_Y);
}

// =============================================================================
// _renderChart()  — PAS cadence/current curve for all 10 levels + live cursor
// =============================================================================
void LcdDisplay::_renderChart() {
    _sprChart.fillSprite(LcdColor::BG);

    // The curve occupies the top of the sprite. GC9A01 reserves the remainder for
    // a status line, because a 240 px circle has no room for two more tile rows.
    _drawCurve(_sprChart, CHART_W, CURVE_H,
               _last.cadenceRpm,
               _last.targetCurrentPct,
               _last.hasTargetTelemetry);

#if defined(LCD_DISPLAY_GC9A01)
    _drawStatusLine(_sprChart, CHART_W, CURVE_H + 6);
#endif

    _sprChart.pushSprite(CHART_X, CHART_Y);
}

#if defined(LCD_DISPLAY_GC9A01)
// =============================================================================
// _drawStatusLine() — GC9A01 only
//
// Battery / current / watts / PAS on one centred line. Kept deliberately short:
// a 192 px sprite centred at x=120 means the text ends up around ±70 px from
// centre, which is inside the r=120 circle at this height. A longer string would
// run under the bezel.
// =============================================================================
void LcdDisplay::_drawStatusLine(TFT_eSprite& spr, int16_t w, int16_t y) {
    char buf[40];
    const char* modeChar = (_last.operationMode == 1) ? "S" : "P";
    snprintf(buf, sizeof(buf), "%d%% %.1fA %uW %s%d",
             _last.batteryPct, _last.currentAmps,
             (unsigned)_last.powerWatts, modeChar, _last.assistLevel);

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(LcdColor::TEXT_WHITE, LcdColor::BG);

    int16_t tx = (w - spr.textWidth(buf)) / 2;
    if (tx < 0) tx = 0;
    spr.setCursor(tx, y);
    spr.print(buf);
}
#endif

// =============================================================================
// _drawCurve() — the core chart renderer
//
// X axis: cadence RPM, 0..LCD_CURVE_MAX_CADENCE_RPM
// Y axis: target current %, 0..100 (100 at top, 0 at bottom)
//
// For each of the 10 assist levels in the active mode:
//   - Compute the curve using _computePasCurrent() at N sample points
//   - Fill under the curve for the active level; just draw the line for others
//   - Lighter opacity (darker grey) for inactive levels
//
// The active assist level gets an accent-colour line + fill.
// A vertical magenta line marks the live cadence position if hasLive.
// A horizontal dotted line at liveTargetPct shows the firmware's actual output.
// =============================================================================
void LcdDisplay::_drawCurve(TFT_eSprite& spr, int16_t w, int16_t h,
                             uint8_t liveCadenceRpm, uint8_t liveTargetPct,
                             bool hasLive) {
    // Draw axis ticks (light grey)
    // X axis: bottom edge
    spr.drawFastHLine(0, h - 1, w, LcdColor::BORDER);
    // Y axis: left edge
    spr.drawFastVLine(0, 0, h, LcdColor::BORDER);

    // Draw each level, back to front. Level 0 first (so active level on top).
    for (int lvl = 9; lvl >= 0; --lvl) {
        bool isActive = (lvl == (int)_last.assistLevel);

        uint16_t lineColor  = isActive ? LcdColor::CURVE_LINE : LcdColor::BORDER;
        uint16_t fillColor  = isActive ? LcdColor::CURVE_BASE : 0; // only fill active

        int prevY = -1;
        for (int px = 0; px < w; ++px) {
            uint8_t cadRpm = (uint8_t)((px * LCD_CURVE_MAX_CADENCE_RPM) / (w - 1));
            float curPct   = _computePasCurrent(cadRpm, (uint8_t)lvl);

            // Map to screen Y: 100% → y=0, 0% → y=h-1
            int py = (int)(h - 1 - (curPct * (h - 1) / 100.0f));
            py = constrain(py, 0, h - 1);

            // Fill column from curve to bottom for the active level
            if (isActive && fillColor) {
                spr.drawFastVLine(px, py, h - py, fillColor);
            }

            // Draw the curve line (connect to previous point if not first)
            if (prevY >= 0 && prevY != py) {
                // Draw vertical segment to join consecutive points
                int ylo = min(py, prevY);
                int yhi = max(py, prevY);
                spr.drawFastVLine(px, ylo, yhi - ylo + 1, lineColor);
            } else {
                spr.drawPixel(px, py, lineColor);
            }
            prevY = py;
        }
    }

    // --- Live cadence cursor (vertical magenta line) ---
    if (hasLive && liveCadenceRpm <= LCD_CURVE_MAX_CADENCE_RPM) {
        int cursorX = (int)((liveCadenceRpm * (w - 1)) / LCD_CURVE_MAX_CADENCE_RPM);
        cursorX = constrain(cursorX, 0, w - 1);
        spr.drawFastVLine(cursorX, 0, h, LcdColor::CURSOR_CLR);

        // Also draw horizontal dotted line at the target current level
        int targetY = (int)(h - 1 - (liveTargetPct * (h - 1) / 100.0f));
        targetY = constrain(targetY, 0, h - 1);
        for (int px = 0; px < w; px += 4) {
            spr.drawPixel(px, targetY, LcdColor::CURSOR_CLR);
        }

        // Small crosshair marker at intersection
        spr.fillRect(cursorX - 2, targetY - 2, 5, 5, LcdColor::CURSOR_CLR);
    }
}

// =============================================================================
// _computePasCurrent()
//
// Mirrors the JS computePasTargetCurrent() logic from web/index.html.
// Uses the last-snapshotted config values from _last (set in _snapshotAndDiff).
//
// Returns current % in [0, 100].
//
// PAS model (v4/v5/v6 all share the same current curve):
//   - If level 0 or PAS disabled: 0%
//   - PAS Variable / PAS Torque: flat at maxCurPct (no keep-current ramp)
//   - maxCurPct   = levelCurrentPct[levelIdx]
//   - rampStartRpm = _last.pasKeepCurrentCadenceRpm
//   - rampEndRpm   = (levelCadencePct[levelIdx] * 120) / 100   (120 = BBSHD nominal max)
//   - minCurPct    = (pasKeepCurrentPct * maxCurPct) / 100
//   - cadence < rampStart  → maxCurPct
//   - cadence >= rampEnd   → minCurPct
//   - between              → linear taper
// =============================================================================
float LcdDisplay::_computePasCurrent(uint8_t cadenceRpm, uint8_t levelIdx) const {
    if (levelIdx == 0) return 0.0f;                          // level 0 = off
    if (!(_last.levelFlags[levelIdx] & ASSIST_FLAG_PAS)) return 0.0f;

    float maxCurPct = (float)_last.levelCurrentPct[levelIdx];

    // PAS Variable means the current is modulated by the throttle rather than
    // following the keep-current ramp, so the curve is flat at the level's
    // maximum. The web UI short-circuits the same way; without this the chart
    // draws a ramp-down the firmware never produces for these levels.
    if (_last.levelFlags[levelIdx] & ASSIST_FLAG_PAS_VARIABLE) {
        return constrain(maxCurPct, 0.0f, 100.0f);
    }

    float keepCurPct  = (float)_last.pasKeepCurrentPct;
    float minCurPct   = (keepCurPct * maxCurPct) / 100.0f;

    const float motorMaxCadence = 120.0f;
    float rampStartRpm = (float)_last.pasKeepCurrentCadenceRpm;
    float rampEndRpm   = ((float)_last.levelCadencePct[levelIdx] * motorMaxCadence) / 100.0f;

    if (keepCurPct >= 100.0f || rampEndRpm <= rampStartRpm) {
        return constrain(maxCurPct, 0.0f, 100.0f);
    }

    float cad = (float)cadenceRpm;
    if (cad <= rampStartRpm) {
        return constrain(maxCurPct, 0.0f, 100.0f);
    } else if (cad >= rampEndRpm) {
        return constrain(minCurPct, 0.0f, 100.0f);
    } else {
        float span  = rampEndRpm - rampStartRpm;
        float ratio = (cad - rampStartRpm) / span;
        float cur   = maxCurPct - ratio * (maxCurPct - minCurPct);
        return constrain(cur, 0.0f, 100.0f);
    }
}

// =============================================================================
// _drawTile()
//
// Draws a labelled numeric tile into sprite spr at the given rect.
// Uses the GLCD (font 1) for the dim label (6px high) and a larger font for
// the value. The TFT_eSPI built-in font 2 (14px) fits in ROW0_H=18.
// =============================================================================
void LcdDisplay::_drawTile(TFT_eSprite& spr,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           const char* label, const char* value,
                           uint16_t valueColor) {
    // Tile background + border
    spr.fillRect(x, y, w, h, LcdColor::TILE_BG);
    spr.drawRect(x, y, w, h, LcdColor::BORDER);

    // Dim label (font 1 = 6px high, proportional)
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(LcdColor::TEXT_DIM, LcdColor::TILE_BG);
    spr.setCursor(x + 2, y + 1);
    spr.print(label);

    // Value (font 2 = ~14px, fits in 18px row with 2px margin top and bottom)
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(valueColor, LcdColor::TILE_BG);

    // Centre-align the value text in the tile
    int16_t tw = spr.textWidth(value, 2);
    int16_t tx = x + (w - tw) / 2;
    int16_t ty = y + 7; // baseline offset for font 2 within 18-px row
    spr.setCursor(tx, ty);
    spr.print(value);
}

// =============================================================================
// _fmtInt / _fmtFloat1
// =============================================================================
void LcdDisplay::_fmtInt(char* buf, size_t len, int val) {
    snprintf(buf, len, "%d", val);
}

void LcdDisplay::_fmtFloat1(char* buf, size_t len, float val) {
    snprintf(buf, len, "%.1f", val);
}

#endif // LCD_DISPLAY_ST7789 || LCD_DISPLAY_GC9A01
