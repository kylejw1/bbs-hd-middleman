#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

// =============================================================================
// LCD_DISPLAY_H — Optional TFT display subsystem for BBS-HD Middleman
//
// Supports two hardware targets selected at compile time:
//
//   LCD_DISPLAY_ST7789   — 2.25" TFT, 76×284 (landscape: 284×76), ST7789 driver
//   LCD_DISPLAY_GC9A01   — 1.28" round TFT, 240×240, GC9A01 driver
//
// Both targets use the TFT_eSPI library configured via build flags in
// platformio.ini (USER_SETUP_LOADED=1). Neither flag defined → no-op compile.
//
// Layout (ST7789, landscape 284×76):
//   Row 0 (top 18 px): cadence | motor RPM | target I% | target spd%
//   Row 1 (next 18 px): batt% | current A | watts | PAS level
//   Chart (remaining ~40 px): the active level's PAS current curve, filled,
//                             with a marker at the live cadence/current point
//
// Layout (GC9A01, 240×240 circle — only the inscribed r=120 circle is visible):
//   Chart (y 48..104):  the active level's PAS current curve, filled, with a
//                       marker at the live cadence/current point. NO TEXT.
//   Row 0 (y 108..130): cadence (left, x=54) | target I% (right, x=186)
//   Row 1 (y 132..220): the hero slope in degrees at font 6 (48 px), centred,
//                       with target speed % directly beneath it
//
//   Values only — no tile frames and no labels. The frame was the grid and the
//   label was a title, and on a 240 px circle both cost more room than the
//   numbers they annotated. A four-across tile row only fits at y=120, so this
//   target never used one.
//   Content is rotated 90 degrees clockwise by LCD_ROTATION. The circle itself
//   is rotation-invariant, so no geometry below changes with it.
//
// The chart draws exactly ONE curve: the selected assist level. It used to draw
// all ten with the inactive nine greyed out, which on a 64 px chart was more
// detail than can be read at a glance.
//
// Rendering strategy: dirty-region sprites. The display is divided into fixed
// rectangular regions; each region has a TFT_eSprite that is pushed only when
// its source data changes. This avoids full-screen repaints and keeps SPI
// traffic proportional to what actually changed.
//
// Thread safety: LcdDisplay::process() is called from loop() on the Arduino
// task. It only reads from TelemetryTracker via the public getter API, which is
// mutex-protected. It never writes to TelemetryTracker.
//
// Timing: process() is gated by LCD_REFRESH_MS (default 200 ms). Within that
// budget it checks each region's dirty flag and pushes only what changed.
// =============================================================================

#include "Config.h"

#if defined(LCD_DISPLAY_ST7789) || defined(LCD_DISPLAY_GC9A01)

#include <Arduino.h>
#include <TFT_eSPI.h>


// ---------------------------------------------------------------------------
// Forward declaration — the global singleton
// ---------------------------------------------------------------------------
class LcdDisplay;
extern LcdDisplay Display;

// ---------------------------------------------------------------------------
// Colour palette
//
// Sprites run at 4 bits per pixel against an explicit RGB565 palette instead of
// TFT_eSPI's default 16bpp. That quarters sprite RAM -- 70 KB -> 17 KB on the
// round target -- and it is what leaves the Wi-Fi stack enough heap to start on
// a classic ESP32, where Bluedroid takes ~90 KB and the LCD otherwise leaves too
// little for the SoftAP (xQueueCreateMutex returned NULL inside WiFi.mode(),
// aborting in lock_init_generic).
//
// 4bpp is chosen over 8bpp deliberately: TFT_eSPI's 8bpp sprites are 332 RGB with
// no palette, which quantises the dark teal curve fill (0x0311) to something very
// close to black. The palette keeps every colour exact, and the design needs
// fewer than 16 of them.
//
// Index a sprite's draw calls with the PAL_* values. Use LCD_PALETTE[PAL_x] only
// for the few calls that target the panel directly rather than a sprite.
// ---------------------------------------------------------------------------
enum LcdPal : uint8_t {
    PAL_BG = 0,        // panel background
    PAL_BORDER,        // very dark grey: frame, and inactive curve
    PAL_TEXT_DIM,      // medium grey tile label
    PAL_TEXT_WHITE,
    PAL_ACCENT_CYAN,
    PAL_ACCENT_GREEN,
    PAL_ACCENT_YELL,
    PAL_ACCENT_RED,
    PAL_CURVE_BASE,    // dark teal fill under the active curve
    PAL_CURVE_LINE,    // bright cyan curve edge
    PAL_CURSOR_CLR,    // magenta cadence cursor
    PAL_TILE_BG,       // tile background (very dark navy)
    PAL_COUNT
};

// Must hold 16 entries: TFT_eSPI's 4bpp colour map is fixed at 16.
constexpr uint16_t LCD_PALETTE[16] = {
    TFT_BLACK,   // PAL_BG
    0x2104,      // PAL_BORDER
    0x8410,      // PAL_TEXT_DIM
    TFT_WHITE,   // PAL_TEXT_WHITE
    0x07FF,      // PAL_ACCENT_CYAN
    0x07E0,      // PAL_ACCENT_GREEN
    0xFFE0,      // PAL_ACCENT_YELL
    0xF800,      // PAL_ACCENT_RED
    0x0311,      // PAL_CURVE_BASE
    0x07EF,      // PAL_CURVE_LINE
    0xF81F,      // PAL_CURSOR_CLR
    0x0841,      // PAL_TILE_BG
    TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK   // 12..15 unused
};

constexpr int8_t LCD_SPRITE_BPP = 4;

// ---------------------------------------------------------------------------
// LcdDisplay class
// ---------------------------------------------------------------------------
class LcdDisplay {
public:
    LcdDisplay();

    // Call once in setup(), after Telemetry.begin() but before Portal.begin().
    // Initialises TFT_eSPI, rotates, clears to black, draws static chrome.
    void begin();

    // Call every loop() iteration. Internally rate-limited to LCD_REFRESH_MS.
    // Checks dirty flags; pushes only changed sprite regions.
    void process();

private:
    TFT_eSPI _tft;

#if defined(LCD_DISPLAY_ST7789)
    // -----------------------------------------------------------------------
    // ST7789 — 284×76 landscape
    // -----------------------------------------------------------------------
    // Physical rotation: the 76×284 panel is mounted landscape (76 is height,
    // 284 is width). TFT_eSPI rotation=1 maps x→width=284, y→height=76.
    static constexpr int16_t W = 284;
    static constexpr int16_t H = 76;

    // 4 tiles across, two rows, chart filling the rest.
    static constexpr int16_t TILES_PER_ROW = 4;
    static constexpr int16_t ROW0_H  = 18;
    static constexpr int16_t ROW1_H  = 18;
    static constexpr int16_t ROW0_Y  = 0;
    static constexpr int16_t ROW1_Y  = ROW0_H;
    static constexpr int16_t CHART_X = 0;
    static constexpr int16_t CHART_W = W;
    static constexpr int16_t CHART_Y = ROW0_H + ROW1_H;
    static constexpr int16_t CHART_H = H - CHART_Y;  // ≈ 40 px
    static constexpr int16_t CURVE_H = CHART_H;      // chart is all curve

    static constexpr int16_t TILE_W  = W / TILES_PER_ROW;  // 71
    static constexpr int16_t TILE_X0 = 0;

#elif defined(LCD_DISPLAY_GC9A01)
    // -----------------------------------------------------------------------
    // GC9A01 — 240×240 round
    // -----------------------------------------------------------------------
    // The panel is square but the bezel is a circle of radius 120 centred at
    // (120,120), so anything outside that circle is physically invisible.
    //
    // That rules out the ST7789 layout here: a row `rowW` wide is only fully
    // visible when |y - 120| <= sqrt(120^2 - (rowW/2)^2), which for a 240 px
    // row is true only at y = 120. Four-across rows at the top of the circle
    // would be almost entirely hidden behind the bezel, so this target inverts
    // the arrangement: the graph goes in the top band where the circle is still
    // usable, and the numbers stack below it around one hero value.
    //
    //   chart  192 px wide at x=24, y=48..104  → needs y >= 48, see below
    //   row 0  240 px sprite at y=108, content centred at x=54 and x=186
    //   row 1  240 px sprite at y=132..220, content centred
    static constexpr int16_t W = 240;
    static constexpr int16_t H = 240;

    // --- Chart: curve and cursor only, no text at all ---
    // A 192 px wide region is only fully inside the circle where
    // |y-120| <= sqrt(120^2 - 96^2) = 72, so the chart cannot start above y=48.
    static constexpr int16_t CHART_X = 24;
    static constexpr int16_t CHART_W = 192;
    static constexpr int16_t CHART_Y = 48;
    static constexpr int16_t CHART_H = 56;
    static constexpr int16_t CURVE_H = CHART_H;   // the chart is all curve now

    // --- Row 0: the two target percentages, above the heroes ---
    // Full-width sprite with inset content: a 240 px row is not fully visible
    // this high up, but these two values are centred well inside the circle.
    static constexpr int16_t ROW0_Y    = 108;
    static constexpr int16_t ROW0_H    = 22;
    static constexpr int16_t FLANK_L_X = 54;
    static constexpr int16_t FLANK_R_X = 186;

    // --- Row 1: cadence and PAS, the two figures the rider acts on ---
    static constexpr int16_t ROW1_Y = 132;
    static constexpr int16_t ROW1_H = 88;

    // Layout inside row 1: two hero figures side by side at font 6 (48 px)
    // against font 2's 16. Cadence left, PAS right, each centred in its half.
    static constexpr int16_t HERO_TOP = 12;
    static constexpr int16_t HERO_L_X = 62;
    static constexpr int16_t HERO_R_X = 178;
#endif

    // Sprites — one per region, allocated in begin(). Common to both targets so
    // the render code below is target-agnostic; only the geometry differs.
    TFT_eSprite _sprRow0;
    TFT_eSprite _sprRow1;
    TFT_eSprite _sprChart;

    // -----------------------------------------------------------------------
    // Snapshot of last-rendered values — used to skip unchanged regions
    // -----------------------------------------------------------------------
    struct Snapshot {
        uint8_t  cadenceRpm;      // integer RPM (x10 value ÷ 10)
        uint8_t  motorRpm;
        uint8_t  targetCurrentPct;
        uint8_t  targetSpeedPct;
        uint8_t  batteryPct;
        float    currentAmps;
        uint16_t powerWatts;
        uint8_t  assistLevel;
        uint8_t  operationMode;   // 0=Standard 1=Sport
        bool     hasTargetTelemetry;
        // Config snapshot for curve validity
        uint8_t  maxCurrentAmps;
        uint8_t  pasKeepCurrentPct;
        uint8_t  pasKeepCurrentCadenceRpm;
        // Per-level data for the active mode (10 levels)
        uint8_t  levelCurrentPct[10];
        uint8_t  levelCadencePct[10];
        uint8_t  levelFlags[10];
    };

    Snapshot _last;
    bool     _initialised;
    bool     _spritesOk;      // false if any sprite allocation failed
    uint32_t _lastRefreshMs;


    // Dirty flags — set true when data changes
    bool _dirtyRow0;
    bool _dirtyRow1;
    bool _dirtyChart;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    // Snapshot current telemetry and config; set dirty flags if changed
    void _snapshotAndDiff();

    // Render helpers
    void _renderRow0();
    void _renderRow1();
    void _renderChart();


    // Draw a single numeric "tile" into the sprite spr at (x, y, w, h).
    // label is the small dim label above the value.
    void _drawTile(TFT_eSprite& spr,
                   int16_t x, int16_t y, int16_t w, int16_t h,
                   const char* label, const char* value,
                   uint8_t valueColor = PAL_TEXT_WHITE);

    // Compute the PAS target-current % for a given cadence RPM and level,
    // using the current config snapshot. Returns a value in [0, 100].
    // Mirrors the computePasTargetCurrent() logic from the web UI JS.
    float _computePasCurrent(uint8_t cadenceRpm, uint8_t levelIdx) const;

    // Draw the PAS curve chart into _sprChart.
    // Draws curves for all 10 active-mode levels (greyed out except active).
    // Also draws the live cadence cursor if target telemetry is fresh.
    void _drawCurve(TFT_eSprite& spr, int16_t w, int16_t h,
                    uint8_t liveCadenceRpm, uint8_t liveTargetPct,
                    bool hasLive);

    // GC9A01 only: the centred "batt / A / W / PAS" line drawn in the lower part
    // of the chart sprite, since a 240 px circle has no room for two more tiles.
    void _drawStatusLine(TFT_eSprite& spr, int16_t w, int16_t y);

    // Format helpers
    static void _fmtInt(char* buf, size_t len, int val);
    static void _fmtFloat1(char* buf, size_t len, float val);
};

#else // LCD_DISPLAY_ST7789 || LCD_DISPLAY_GC9A01

// ---------------------------------------------------------------------------
// No display configured — stub so the rest of main.cpp compiles cleanly
// ---------------------------------------------------------------------------
class LcdDisplay {
public:
    inline void begin()   {}
    inline void process() {}
};
extern LcdDisplay Display;

#endif // LCD_DISPLAY_ST7789 || LCD_DISPLAY_GC9A01

#endif // LCD_DISPLAY_H
