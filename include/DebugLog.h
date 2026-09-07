#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

// =============================================================================
// Serial Trace Ring Buffer for Real-Time Debug Console
// =============================================================================
// Captures raw UART byte traffic and system messages so the web UI can display
// a live serial-monitor-style debug console view.
// =============================================================================

#define SERIAL_TRACE_BYTE_ENTRIES   512     // ring buffer size for raw byte events
#define SERIAL_TRACE_TEXT_ENTRIES   48      // ring buffer size for text messages
#define TRACE_TEXT_MAX              96      // max chars per text message

// Direction tags for byte trace events (compact single-char representation)
// Used for JSON serialization to keep payloads minimal
#define TRACE_DIR_DRX  "drx"   // Display RX: byte received FROM display
#define TRACE_DIR_DTX  "dtx"   // Display TX: byte sent TO display
#define TRACE_DIR_CRX  "crx"   // Controller RX: byte received FROM controller
#define TRACE_DIR_CTX  "ctx"   // Controller TX: byte sent TO controller
#define TRACE_DIR_SYS  "sys"   // System text message

enum class TraceDir : uint8_t {
    DisplayRx    = 0,
    DisplayTx    = 1,
    ControllerRx = 2,
    ControllerTx = 3,
    System       = 4
};

struct __attribute__((packed)) ByteTraceEntry {
    uint32_t timestampMs;
    uint32_t sequence;
    uint8_t  dir;       // TraceDir value
    uint8_t  byte;
};

struct TextTraceEntry {
    uint32_t timestampMs;
    uint32_t sequence;
    char     text[TRACE_TEXT_MAX];
};

class DebugLog {
public:
    DebugLog();

    void begin();

    // Byte-level tracing — call these from SerialBridge on every UART read/write
    void traceByte(TraceDir dir, uint8_t byte);
    void traceBytes(TraceDir dir, const uint8_t* data, size_t len);

    // System / text messages — printf-style
    void tracef(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void trace(const char* msg);

    // Build JSON payload for the /api/serial-trace endpoint.
    // afterSeq: return only events with sequence > afterSeq (0 = all).
    void buildTraceJson(JsonDocument& doc, uint32_t afterSeq = 0);

    // Get latest sequence number (for polling clients to resume)
    uint32_t getLatestSeq() const;

    // Total events dropped (ring buffer overwrites since begin)
    uint32_t getDropped() const;

private:
    mutable SemaphoreHandle_t _mutex;

    // Byte trace ring buffer
    ByteTraceEntry _bytes[SERIAL_TRACE_BYTE_ENTRIES];
    size_t _byteHead;       // write index
    size_t _byteCount;      // number of valid entries (0..SERIAL_TRACE_BYTE_ENTRIES)

    // Text trace ring buffer
    TextTraceEntry _texts[SERIAL_TRACE_TEXT_ENTRIES];
    size_t _textHead;
    size_t _textCount;

    // Monotonic sequence counter (shared across byte + text)
    uint32_t _seq;
    uint32_t _dropped;

    // Helpers
    void _writeByteEntry(uint32_t ts, uint32_t seq, uint8_t dir, uint8_t byte);
    void _writeTextEntry(uint32_t ts, uint32_t seq, const char* text);
};

extern DebugLog Debug;

#endif // DEBUG_LOG_H