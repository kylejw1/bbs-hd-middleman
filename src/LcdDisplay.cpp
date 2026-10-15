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
#if LCD_IMU_PAGE
    , _page(LCD_START_PAGE)
    , _lastPageSwitchMs(0)
#endif
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
    // A direct-to-panel fill needs the real RGB565 value, not a palette index.
    _tft.fillScreen(LCD_PALETTE[PAL_BG]);
    _tft.setSwapBytes(true);

    // Sprites are the real memory cost of this feature. They run at 4bpp against
    // the palette in LcdDisplay.h -- 4x smaller than TFT_eSPI's default 16bpp,
    // which is what keeps the radios alive on a classic ESP32 (see the note
    // there). Allocate them, and if any allocation fails disable the display
    // outright: pushing an unallocated sprite is worse than showing nothing.
    //
    // These go to the USB console rather than Debug.trace because tracing is off
    // by default (DEBUG_TRACE_ENABLED_DEFAULT 0), and this is the only boot-time
    // evidence that the panel came up or why it did not.
    _sprRow0.setColorDepth(LCD_SPRITE_BPP);
    _sprRow1.setColorDepth(LCD_SPRITE_BPP);
    _sprChart.setColorDepth(LCD_SPRITE_BPP);

    _spritesOk  = (_sprRow0.createSprite(W, ROW0_H) != nullptr);
    _spritesOk &= (_sprRow1.createSprite(W, ROW1_H) != nullptr);
    _spritesOk &= (_sprChart.createSprite(CHART_W, CHART_H) != nullptr);

    const uint32_t spritePixels =
        (uint32_t)W * ROW0_H + (uint32_t)W * ROW1_H + (uint32_t)CHART_W * CHART_H;
    const uint32_t spriteBytes = spritePixels * LCD_SPRITE_BPP / 8;

    if (!_spritesOk) {
        Serial.printf("[LCD] sprite alloc failed (%u B needed, free heap %u) - display disabled\n",
                      spriteBytes, ESP.getFreeHeap());
        return;
    }

    // Only valid once createSprite() has allocated the sprite's colour map.
    _sprRow0.createPalette(LCD_PALETTE, 16);
    _sprRow1.createPalette(LCD_PALETTE, 16);
    _sprChart.createPalette(LCD_PALETTE, 16);

#if LCD_IMU_PAGE
    _lastPageSwitchMs = millis();   // bike page first
#endif
    _initialised = true;
    Serial.printf("[LCD] %s ready %dx%d, sprites %u B @ %dbpp, free heap %u -> %u\n",
                  LCD_DISPLAY_NAME, W, H, spriteBytes, LCD_SPRITE_BPP,
                  heapBefore, ESP.getFreeHeap());
}

// =============================================================================
// process()  — called from loop(); rate-limited to LCD_REFRESH_MS
// =============================================================================
void LcdDisplay::process() {
    if (!_initialised) return;

    uint32_t now = millis();

#if LCD_IMU_PAGE
    // Page selection. With LCD_PAGE_MS == 0 the display is pinned to
    // LCD_START_PAGE and never alternates; the cycling path below is intact and
    // simply never fires. When cycling IS on it only leaves page 0 while an
    // MPU-6050 actually answered on the bus, so a board without one behaves as
    // it did before.
    if (LCD_PAGE_MS > 0 && Mpu.isPresent()) {
        if (now - _lastPageSwitchMs >= LCD_PAGE_MS) {
            _lastPageSwitchMs = now;
            _page ^= 1;
            _invalidateSnapshot();
        }
    } else {
        const uint8_t want = (LCD_PAGE_MS > 0) ? 0 : (uint8_t)LCD_START_PAGE;
        if (_page != want) {
            _page = want;
            _invalidateSnapshot();
        }
    }

    // The IMU page carries a live bubble level, so it runs faster than the bike
    // page's lazy diff-and-push refresh.
    const uint32_t interval = (_page == 1) ? LCD_IMU_REFRESH_MS : LCD_REFRESH_MS;
    if (now - _lastRefreshMs < interval) return;
    _lastRefreshMs = now;

    if (_page == 1) {
        _renderImuPage();
        return;
    }
#else
    if (now - _lastRefreshMs < LCD_REFRESH_MS) return;
    _lastRefreshMs = now;
#endif

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

    // --- Detect row changes ---
    //
    // What each row displays differs by target, so what marks it dirty has to as
    // well. Getting the pairing wrong is invisible until a value sits frozen:
    // the sprite simply never repaints. That has caught this code out twice,
    // both times on the slope, both times because the value moved region and the
    // dirty check did not move with it. The mapping is:
    //
    //   GC9A01   row 0: cadence | target current %   row 1: hero slope | target spd %
    //   ST7789   row 0: cadence | mRPM | I% | S%     row 1: batt | A | W | PAS
    //
    // Slope is the one term that does not come from the 0xEC target frame, so it
    // needs its own comparison against what was last drawn.
    float slopeDeg = Mpu.isPresent() ? Mpu.slopeDeg() : 0.0f;
    // The hero figure prints %+.1f, so a runaway estimate would wrap an int16.
    if (slopeDeg < -320.0f) slopeDeg = -320.0f;
    if (slopeDeg >  320.0f) slopeDeg =  320.0f;
    const int16_t slopeDeg10 = (int16_t)roundf(slopeDeg * 10.0f);

#if defined(LCD_DISPLAY_GC9A01)
    const bool slopeChanged = (slopeDeg10 != _last.slopeDeg10);

    const bool row0Changed = (hasTgt != _last.hasTargetTelemetry ||
                              cadRpm != _last.cadenceRpm         ||
                              tgtI   != _last.targetCurrentPct);

    // The hero slope is drawn HERE, with target speed % underneath it.
    const bool row1Changed = (slopeChanged ||
                              tgtS != _last.targetSpeedPct);
#else
    const bool row0Changed = (hasTgt != _last.hasTargetTelemetry ||
                              cadRpm != _last.cadenceRpm         ||
                              motRpm != _last.motorRpm           ||
                              tgtI   != _last.targetCurrentPct   ||
                              tgtS   != _last.targetSpeedPct);

    const bool row1Changed = (battPct != _last.batteryPct                    ||
                              fabsf(curAmps - _last.currentAmps) > 0.05f     ||
                              watts   != _last.powerWatts                    ||
                              pal     != _last.assistLevel                   ||
                              opMode  != _last.operationMode);
#endif

    if (row0Changed) _dirtyRow0 = true;
    if (row1Changed) _dirtyRow1 = true;

    // --- Detect chart changes ---
    bool configChanged = (maxI    != _last.maxCurrentAmps         ||
                          keepPct != _last.pasKeepCurrentPct       ||
                          keepRpm != _last.pasKeepCurrentCadenceRpm||
                          opMode  != _last.operationMode);
    // Only cadence and the target current move the chart's cursor -- both live in
    // row 0 on either target. The slope is not drawn on the chart, so it must not
    // force a chart repaint.
    bool cursorChanged = row0Changed;

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
    _last.slopeDeg10               = slopeDeg10;
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
// _renderRow0()  — the two values flanking the hero slope (GC9A01), or the
//                  first of the strip's two four-across tile rows (ST7789).
// =============================================================================
void LcdDisplay::_renderRow0() {
    _sprRow0.fillSprite(PAL_BG);

    char buf[16];

#if defined(LCD_DISPLAY_GC9A01)
    // Bare values only: no tile frame, no label. The frame was the grid and the
    // label was a title, and on a 240 px circle both cost more room than the
    // numbers they annotated. Cadence left, target current right, with the hero
    // slope centred between them on the row below.
    _sprRow0.setTextDatum(MC_DATUM);
    _sprRow0.setTextFont(2);

    if (_last.hasTargetTelemetry) _fmtInt(buf, sizeof(buf), _last.cadenceRpm);
    else                          strcpy(buf, "--");
    _sprRow0.setTextColor(PAL_ACCENT_CYAN, PAL_BG);
    _sprRow0.drawString(buf, FLANK_L_X, ROW0_H / 2);

    if (_last.hasTargetTelemetry) snprintf(buf, sizeof(buf), "%d%%", _last.targetCurrentPct);
    else                          strcpy(buf, "--");
    _sprRow0.setTextColor(PAL_ACCENT_YELL, PAL_BG);
    _sprRow0.drawString(buf, FLANK_R_X, ROW0_H / 2);
#else
    // Tile 0: Cadence RPM
    if (_last.hasTargetTelemetry) {
        _fmtInt(buf, sizeof(buf), _last.cadenceRpm);
    } else {
        strcpy(buf, "--");
    }
    _drawTile(_sprRow0, TILE_X0 + 0*TILE_W, 0, TILE_W, ROW0_H, "cad", buf, PAL_ACCENT_CYAN);

    // Tile 1: Motor RPM
    if (_last.hasTargetTelemetry) {
        _fmtInt(buf, sizeof(buf), _last.motorRpm);
    } else {
        strcpy(buf, "--");
    }
    _drawTile(_sprRow0, TILE_X0 + 1*TILE_W, 0, TILE_W, ROW0_H, "mRPM", buf, PAL_ACCENT_GREEN);

    // Tile 2: Target current %
    if (_last.hasTargetTelemetry) {
        snprintf(buf, sizeof(buf), "%d%%", _last.targetCurrentPct);
    } else {
        strcpy(buf, "--%");
    }
    _drawTile(_sprRow0, TILE_X0 + 2*TILE_W, 0, TILE_W, ROW0_H, "I tgt", buf, PAL_ACCENT_YELL);

    // Tile 3: Target speed %
    if (_last.hasTargetTelemetry) {
        snprintf(buf, sizeof(buf), "%d%%", _last.targetSpeedPct);
    } else {
        strcpy(buf, "--%");
    }
    _drawTile(_sprRow0, TILE_X0 + 3*TILE_W, 0, TILE_W, ROW0_H, "S tgt", buf, PAL_TEXT_WHITE);
#endif

    _sprRow0.pushSprite(0, ROW0_Y);
}

// =============================================================================
// _renderRow1()  — the hero slope with target speed beneath it (GC9A01), or the
//                  strip's second tile row (ST7789).
// =============================================================================
void LcdDisplay::_renderRow1() {
    _sprRow1.fillSprite(PAL_BG);

    char buf[16];

#if defined(LCD_DISPLAY_GC9A01)
    // The hero: slope in degrees at font 6 (48 px against font 2's 16), centred.
    //
    // The value comes from _last.slopeDeg10 rather than straight from the IMU so
    // that what is drawn is exactly what the dirty check compared -- reading the
    // sensor again here could paint a value the diff never saw change.
    //
    // The degree sign is drawn as a primitive. TFT_eSPI's built-in fonts cover
    // ASCII 32..127 only, so there is no degree glyph to print, and the word
    // "deg" would be a title -- which is what this layout is removing.
    const bool imuOk = Mpu.isPresent();
    char num[16];
    if (imuOk) snprintf(num, sizeof(num), "%+.1f", _last.slopeDeg10 / 10.0f);
    else       strcpy(num, "--");

    _sprRow1.setTextFont(6);
    _sprRow1.setTextColor(PAL_ACCENT_GREEN, PAL_BG);

    const int16_t tw   = _sprRow1.textWidth(num);
    const int16_t degR = 5;
    const int16_t gap  = 5;
    const int16_t span = tw + (imuOk ? (gap + degR * 2) : 0);
    const int16_t x0   = (W - span) / 2;

    _sprRow1.setTextDatum(TL_DATUM);
    _sprRow1.drawString(num, x0, BIG_SLOPE_TOP);

    if (imuOk) {
        // Top-right of the number, which is where a degree symbol sits.
        const int16_t dx = x0 + tw + gap + degR;
        const int16_t dy = BIG_SLOPE_TOP + 12;
        _sprRow1.drawCircle(dx, dy, degR, PAL_ACCENT_GREEN);
        _sprRow1.drawCircle(dx, dy, degR - 1, PAL_ACCENT_GREEN);
    }

    // Target speed %, directly under the hero.
    if (_last.hasTargetTelemetry) snprintf(buf, sizeof(buf), "%d%%", _last.targetSpeedPct);
    else                          strcpy(buf, "--");
    _sprRow1.setTextFont(2);
    _sprRow1.setTextColor(PAL_TEXT_WHITE, PAL_BG);
    _sprRow1.setTextDatum(MC_DATUM);
    _sprRow1.drawString(buf, W / 2, SUB_VALUE_CY);
#else
    // Tile 0: Battery %
    uint8_t battColor = PAL_ACCENT_GREEN;
    if (_last.batteryPct < 20) battColor = PAL_ACCENT_RED;
    else if (_last.batteryPct < 40) battColor = PAL_ACCENT_YELL;
    snprintf(buf, sizeof(buf), "%d%%", _last.batteryPct);
    _drawTile(_sprRow1, TILE_X0 + 0*TILE_W, 0, TILE_W, ROW1_H, "batt", buf, battColor);

    // Tile 1: Current A (1 decimal)
    _fmtFloat1(buf, sizeof(buf), _last.currentAmps);
    _drawTile(_sprRow1, TILE_X0 + 1*TILE_W, 0, TILE_W, ROW1_H, "A", buf, PAL_TEXT_WHITE);

    // Tile 2: Watts
    _fmtInt(buf, sizeof(buf), _last.powerWatts);
    _drawTile(_sprRow1, TILE_X0 + 2*TILE_W, 0, TILE_W, ROW1_H, "W", buf, PAL_ACCENT_YELL);

    // Tile 3: PAS level + mode letter
    const char* modeChar = (_last.operationMode == 1) ? "S" : "P";
    snprintf(buf, sizeof(buf), "%s%d", modeChar, _last.assistLevel);
    _drawTile(_sprRow1, TILE_X0 + 3*TILE_W, 0, TILE_W, ROW1_H, "PAS", buf, PAL_ACCENT_CYAN);
#endif

    _sprRow1.pushSprite(0, ROW1_Y);
}

// =============================================================================
// _renderChart()  — the live cadence/current curve and the rider's position
// =============================================================================
void LcdDisplay::_renderChart() {
    _sprChart.fillSprite(PAL_BG);

    // Curve and cursor only, no text anywhere. The GC9A01 used to append a
    // battery / current / watts / PAS line under the curve; that put text in the
    // graph, and those values are not the set this page is for.
    _drawCurve(_sprChart, CHART_W, CURVE_H,
               _last.cadenceRpm,
               _last.targetCurrentPct,
               _last.hasTargetTelemetry);

    _sprChart.pushSprite(CHART_X, CHART_Y);
}

#if LCD_IMU_PAGE
// =============================================================================
// _renderImuPage() — page 1: the MPU-6050 readout
//
// Reuses the three bike-page sprite regions rather than allocating a full-screen
// sprite: another 240x240 buffer would be ~29 KB of heap even at 4bpp, and on a
// classic ESP32 the Wi-Fi stack needs what is left.
//
// Bubble level: the dot is the gravity vector projected onto the panel. 0.5 px
// per degree puts 30 degrees of tilt at the edge of the ring.
// =============================================================================
void LcdDisplay::_renderImuPage() {
    const Mpu6050::Sample& s = Mpu.sample();
    char buf[40];

    // --- Row 0: identity and bus health ---
    // One line, not two: the bike-page redesign left this sprite 22 px tall.
    _sprRow0.fillSprite(PAL_BG);
    _sprRow0.setTextDatum(MC_DATUM);
    _sprRow0.setTextFont(1);
    _sprRow0.setTextColor(PAL_ACCENT_CYAN, PAL_BG);
    snprintf(buf, sizeof(buf), "MPU-6050  0x%02X  WHO 0x%02X  %u Hz",
             Mpu.address(), (unsigned)Mpu.whoAmI(), (unsigned)Mpu.sampleRateHz());
    _sprRow0.drawString(buf, W / 2, ROW0_H / 2);
    _sprRow0.pushSprite(0, ROW0_Y);

    // --- Row 1: the numbers ---
    // Five lines in 88 px. Font 2 is 16 px tall so it gets 18 px of pitch, and
    // the three longer lines drop to font 1 (8 px) so they still fit the circle
    // as it narrows towards the bottom of the sprite.
    _sprRow1.fillSprite(PAL_BG);
    _sprRow1.setTextDatum(MC_DATUM);
    _sprRow1.setTextFont(2);
    _sprRow1.setTextColor(PAL_ACCENT_GREEN, PAL_BG);
    snprintf(buf, sizeof(buf), "Ax %+.3f  Ay %+.3f", s.ax, s.ay);
    _sprRow1.drawString(buf, W / 2, 10);
    snprintf(buf, sizeof(buf), "Az %+.3f  T %+.1f", s.az, s.tempC);
    _sprRow1.drawString(buf, W / 2, 28);

    _sprRow1.setTextFont(1);
    _sprRow1.setTextColor(PAL_ACCENT_YELL, PAL_BG);
    snprintf(buf, sizeof(buf), "G %+.1f %+.1f %+.1f dps", s.gx, s.gy, s.gz);
    _sprRow1.drawString(buf, W / 2, 44);

    // Raw sensor attitude, straight from the estimator. Kept on this page
    // precisely because it is the uncalibrated number: it shows how far the
    // board sits from level in its mount, which is what the zero below removes.
    _sprRow1.setTextColor(PAL_TEXT_WHITE, PAL_BG);
    snprintf(buf, sizeof(buf), "P %+.1f  R %+.1f", s.pitchDeg, s.rollDeg);
    _sprRow1.drawString(buf, W / 2, 58);

    // Calibrated attitude: mounting bias subtracted, so level on this bike reads
    // 0.0. This is the value the bike page shows as its hero figure.
    _sprRow1.setTextColor(PAL_ACCENT_CYAN, PAL_BG);
    snprintf(buf, sizeof(buf), "slope %+.1f  lean %+.1f", Mpu.slopeDeg(), Mpu.leanDeg());
    _sprRow1.drawString(buf, W / 2, 72);
    _sprRow1.pushSprite(0, ROW1_Y);

    // --- Chart region: the bubble level ---
    // It lives up here now because the bike page took the region below for its
    // hero figure. 192x56 is smaller than it used to be, hence r=22.
    _sprChart.fillSprite(PAL_BG);

    const int16_t cx = CHART_W / 2;
    const int16_t cy = CHART_H / 2;
    const int16_t r  = 22;
    _sprChart.drawCircle(cx, cy, r, PAL_BORDER);
    _sprChart.drawCircle(cx, cy, r / 2, PAL_BORDER);
    _sprChart.drawFastHLine(cx - r, cy, r * 2, PAL_BORDER);
    _sprChart.drawFastVLine(cx, cy - r, r * 2, PAL_BORDER);

    // Bubble level, on the CALIBRATED angles so that a bike sitting level shows
    // a centred dot. Driving it from the raw angles would park the dot off
    // centre by the mounting offset for the life of the install.
    // Horizontal is lean, vertical is slope; nose-up is negative dy so climbing
    // tilts the dot forward/up on screen.
    float dx =  Mpu.leanDeg()  * 0.5f;
    float dy = -Mpu.slopeDeg() * 0.5f;
    const float mag = sqrtf(dx * dx + dy * dy);
    if (mag > (float)(r - 3)) {
        const float k = (float)(r - 3) / mag;
        dx *= k;
        dy *= k;
    }
    _sprChart.fillCircle(cx + (int16_t)dx, cy + (int16_t)dy, 3, PAL_CURSOR_CLR);

    _sprChart.pushSprite(CHART_X, CHART_Y);
}
#endif // LCD_IMU_PAGE

#if defined(LCD_DISPLAY_GC9A01)
// =============================================================================
// _drawStatusLine() — GC9A01 only
//
// Battery / current / watts / PAS on one centred line, kept short so it stays
// inside the r=120 circle at that height.
//
// NOT CURRENTLY CALLED. The bike page is now graph + hero slope + three bare
// values, and this line was the text that used to sit under the curve. It is
// kept rather than deleted because those four readings are genuinely useful and
// restoring them is a one-line call -- but there is no room for it in the
// current layout, so if you call it again, find it a region first.
// =============================================================================
void LcdDisplay::_drawStatusLine(TFT_eSprite& spr, int16_t w, int16_t y) {
    char buf[40];
    const char* modeChar = (_last.operationMode == 1) ? "S" : "P";
    snprintf(buf, sizeof(buf), "%d%% %.1fA %uW %s%d",
             _last.batteryPct, _last.currentAmps,
             (unsigned)_last.powerWatts, modeChar, _last.assistLevel);

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(PAL_TEXT_WHITE, PAL_BG);

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
// Draws ONE curve -- the active assist level's -- filled under the line, plus a
// marker at the rider's live (cadence, target current) point.
//
// It previously drew all ten levels, nine of them greyed out behind the active
// one. On a 64 px chart inside a 240 px circle that was more detail than anyone
// can read at a glance, and it made the one curve that matters harder to pick
// out rather than easier.
// =============================================================================
void LcdDisplay::_drawCurve(TFT_eSprite& spr, int16_t w, int16_t h,
                             uint8_t liveCadenceRpm, uint8_t liveTargetPct,
                             bool hasLive) {
    // Axes (light grey).
    spr.drawFastHLine(0, h - 1, w, PAL_BORDER);
    spr.drawFastVLine(0, 0, h, PAL_BORDER);

    // The single curve: whichever assist level is actually selected.
    const int lvl = (int)_last.assistLevel;

    int prevY = -1;
    for (int px = 0; px < w; ++px) {
        uint8_t cadRpm = (uint8_t)((px * LCD_CURVE_MAX_CADENCE_RPM) / (w - 1));
        float curPct   = _computePasCurrent(cadRpm, (uint8_t)lvl);

        // Map to screen Y: 100% → y=0, 0% → y=h-1
        int py = (int)(h - 1 - (curPct * (h - 1) / 100.0f));
        py = constrain(py, 0, h - 1);

        // Fill under the curve so the shape reads at a glance, then lay the line
        // on top of it.
        spr.drawFastVLine(px, py, h - py, PAL_CURVE_BASE);

        if (prevY >= 0 && prevY != py) {
            // Vertical segment joining this sample to the previous one.
            int ylo = min(py, prevY);
            int yhi = max(py, prevY);
            spr.drawFastVLine(px, ylo, yhi - ylo + 1, PAL_CURVE_LINE);
        } else {
            spr.drawPixel(px, py, PAL_CURVE_LINE);
        }
        prevY = py;
    }

    // The rider's position on the curve: cadence across, actual target current
    // up, with a marker where the two meet. That marker is the point of the
    // graph -- it shows both where you are and how much range the level still
    // has ahead of you.
    if (hasLive && liveCadenceRpm <= LCD_CURVE_MAX_CADENCE_RPM) {
        int cursorX = (int)((liveCadenceRpm * (w - 1)) / LCD_CURVE_MAX_CADENCE_RPM);
        cursorX = constrain(cursorX, 0, w - 1);

        int targetY = (int)(h - 1 - (liveTargetPct * (h - 1) / 100.0f));
        targetY = constrain(targetY, 0, h - 1);

        // Thin guides so both axes can be read off the marker, then a solid core
        // ringed in white -- the fill underneath is nearly the same colour as the
        // cursor, so the core needs the outline to stay visible on the curve.
        spr.drawFastVLine(cursorX, 0, h, PAL_CURSOR_CLR);
        for (int px = 0; px < w; px += 6) {
            spr.drawPixel(px, targetY, PAL_CURSOR_CLR);
        }
        spr.fillRect(cursorX - 2, targetY - 2, 5, 5, PAL_CURSOR_CLR);
        spr.drawRect(cursorX - 3, targetY - 3, 7, 7, PAL_TEXT_WHITE);
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
                           uint8_t valueColor) {
    // Tile background + border
    spr.fillRect(x, y, w, h, PAL_TILE_BG);
    spr.drawRect(x, y, w, h, PAL_BORDER);

    // Dim label (font 1 = 6px high, proportional)
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(PAL_TEXT_DIM, PAL_TILE_BG);
    spr.setCursor(x + 2, y + 1);
    spr.print(label);

    // Value (font 2 = ~14px, fits in 18px row with 2px margin top and bottom)
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(valueColor, PAL_TILE_BG);

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
