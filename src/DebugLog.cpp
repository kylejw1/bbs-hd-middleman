#include "DebugLog.h"

DebugLog Debug;

DebugLog::DebugLog()
    : _mutex(nullptr)
    , _enabled(DEBUG_TRACE_ENABLED_DEFAULT != 0)
    , _byteHead(0)
    , _byteCount(0)
    , _textHead(0)
    , _textCount(0)
    , _seq(0)
    , _dropped(0)
{
}

void DebugLog::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
}

void DebugLog::setEnabled(bool enabled) {
    _enabled = enabled;
}

void DebugLog::clear() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }
    _byteHead = 0;
    _byteCount = 0;
    _textHead = 0;
    _textCount = 0;
    _seq = 0;
    _dropped = 0;
    xSemaphoreGive(_mutex);
}

void DebugLog::traceByte(TraceDir dir, uint8_t byte) {
    if (!_enabled) return;
    uint32_t seq = _seq++;
    _writeByteEntry(millis(), seq, (uint8_t)dir, byte);
}

void DebugLog::traceBytes(TraceDir dir, const uint8_t* data, size_t len) {
    if (!_enabled) return;
    for (size_t i = 0; i < len; ++i) {
        uint32_t seq = _seq++;
        _writeByteEntry(millis(), seq, (uint8_t)dir, data[i]);
    }
}

void DebugLog::tracef(const char* fmt, ...) {
    if (!_enabled) return;
    char buf[TRACE_TEXT_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    uint32_t seq = _seq++;
    _writeTextEntry(millis(), seq, buf);
}

void DebugLog::trace(const char* msg) {
    if (!_enabled) return;
    uint32_t seq = _seq++;
    _writeTextEntry(millis(), seq, msg);
}

void DebugLog::_writeByteEntry(uint32_t ts, uint32_t seq, uint8_t dir, uint8_t byte) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        _dropped++;
        return;
    }

    // If buffer is full, oldest entry is overwritten
    if (_byteCount >= SERIAL_TRACE_BYTE_ENTRIES) {
        _dropped++;
    }

    ByteTraceEntry& entry = _bytes[_byteHead];
    entry.timestampMs = ts;
    entry.sequence = seq;
    entry.dir = dir;
    entry.byte = byte;

    _byteHead = (_byteHead + 1) % SERIAL_TRACE_BYTE_ENTRIES;
    if (_byteCount < SERIAL_TRACE_BYTE_ENTRIES) {
        _byteCount++;
    }

    xSemaphoreGive(_mutex);
}

void DebugLog::_writeTextEntry(uint32_t ts, uint32_t seq, const char* text) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        _dropped++;
        return;
    }

    if (_textCount >= SERIAL_TRACE_TEXT_ENTRIES) {
        _dropped++;
    }

    TextTraceEntry& entry = _texts[_textHead];
    entry.timestampMs = ts;
    entry.sequence = seq;
    strncpy(entry.text, text, TRACE_TEXT_MAX - 1);
    entry.text[TRACE_TEXT_MAX - 1] = '\0';

    _textHead = (_textHead + 1) % SERIAL_TRACE_TEXT_ENTRIES;
    if (_textCount < SERIAL_TRACE_TEXT_ENTRIES) {
        _textCount++;
    }

    xSemaphoreGive(_mutex);
}

uint32_t DebugLog::getLatestSeq() const {
    return _seq > 0 ? _seq - 1 : 0;
}

uint32_t DebugLog::getDropped() const {
    return _dropped;
}

void DebugLog::buildTraceJson(JsonDocument& doc, uint32_t afterSeq) {
    doc["seq"] = getLatestSeq();
    doc["dropped"] = _dropped;

    JsonArray bytesArr = doc["bytes"].to<JsonArray>();
    JsonArray textsArr = doc["texts"].to<JsonArray>();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    // Collect byte entries with seq > afterSeq
    if (_byteCount > 0) {
        size_t start = (_byteHead + SERIAL_TRACE_BYTE_ENTRIES - _byteCount) % SERIAL_TRACE_BYTE_ENTRIES;
        for (size_t i = 0; i < _byteCount; ++i) {
            size_t idx = (start + i) % SERIAL_TRACE_BYTE_ENTRIES;
            const ByteTraceEntry& entry = _bytes[idx];
            if (entry.sequence > afterSeq) {
                JsonObject e = bytesArr.add<JsonObject>();
                e["t"] = entry.timestampMs;
                e["s"] = entry.sequence;
                switch (entry.dir) {
                    case (uint8_t)TraceDir::DisplayRx:    e["d"] = TRACE_DIR_DRX; break;
                    case (uint8_t)TraceDir::DisplayTx:    e["d"] = TRACE_DIR_DTX; break;
                    case (uint8_t)TraceDir::ControllerRx: e["d"] = TRACE_DIR_CRX; break;
                    case (uint8_t)TraceDir::ControllerTx: e["d"] = TRACE_DIR_CTX; break;
                    default:                               e["d"] = TRACE_DIR_SYS; break;
                }
                e["b"] = entry.byte;
            }
        }
    }

    // Collect text entries with seq > afterSeq
    if (_textCount > 0) {
        size_t start = (_textHead + SERIAL_TRACE_TEXT_ENTRIES - _textCount) % SERIAL_TRACE_TEXT_ENTRIES;
        for (size_t i = 0; i < _textCount; ++i) {
            size_t idx = (start + i) % SERIAL_TRACE_TEXT_ENTRIES;
            const TextTraceEntry& entry = _texts[idx];
            if (entry.sequence > afterSeq) {
                JsonObject e = textsArr.add<JsonObject>();
                e["t"] = entry.timestampMs;
                e["s"] = entry.sequence;
                e["d"] = TRACE_DIR_SYS;
                e["msg"] = entry.text;
            }
        }
    }

    xSemaphoreGive(_mutex);
}