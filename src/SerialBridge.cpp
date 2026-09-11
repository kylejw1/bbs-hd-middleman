#include "SerialBridge.h"
#include "DebugLog.h"

SerialBridge Bridge;

SerialBridge::SerialBridge()
    : _controllerSerial(CONTROLLER_UART_NUM)
    , _displaySerial(DISPLAY_UART_NUM)
    , _state(BridgeState::PASS_THROUGH)
    , _bridgeMutex(nullptr)
    , _displayBufLen(0)
    , _lastDisplayByteMs(0)
    , _controllerBufLen(0)
    , _lastControllerByteMs(0)
    , _lastDisplayOpcode(0)
    , _configVersion(BBS_FW_CONFIG_VERSION)
    , _queuedCount(0)
{
}

void SerialBridge::begin() {
    if (_bridgeMutex == nullptr) {
        _bridgeMutex = xSemaphoreCreateMutex();
    }

    // Configure Controller UART
    _controllerSerial.begin(BAFANG_BAUD_RATE, SERIAL_8N1, CONTROLLER_RX_PIN, CONTROLLER_TX_PIN);

    // Configure Display UART
    _displaySerial.begin(BAFANG_BAUD_RATE, SERIAL_8N1, DISPLAY_RX_PIN, DISPLAY_TX_PIN);

    _displayBufLen = 0;
    _controllerBufLen = 0;
    _queuedCount = 0;
    _state = BridgeState::PASS_THROUGH;

    Serial.printf("[Bridge] Initialized UART%d (Controller: RX=%d, TX=%d) & UART%d (Display: RX=%d, TX=%d) at %d baud\n",
                  CONTROLLER_UART_NUM, CONTROLLER_RX_PIN, CONTROLLER_TX_PIN,
                  DISPLAY_UART_NUM, DISPLAY_RX_PIN, DISPLAY_TX_PIN,
                  BAFANG_BAUD_RATE);
    Debug.tracef("Bridge initialized: UART%d (Ctrl RX=%d, TX=%d), UART%d (Disp RX=%d, TX=%d), %d baud",
                 CONTROLLER_UART_NUM, CONTROLLER_RX_PIN, CONTROLLER_TX_PIN,
                 DISPLAY_UART_NUM, DISPLAY_RX_PIN, DISPLAY_TX_PIN,
                 BAFANG_BAUD_RATE);
}

BridgeState SerialBridge::getState() const {
    return _state;
}

bool SerialBridge::isInterceptActive() const {
    return _state == BridgeState::CONFIG_INTERCEPT;
}

uint8_t SerialBridge::getConfigVersion() const {
    return _configVersion;
}

void SerialBridge::process() {
    if (_state == BridgeState::PASS_THROUGH) {
        processDisplayRxPassThrough();
        processControllerRxPassThrough();
    } else {
        // In CONFIG_INTERCEPT mode: middleman intercepts display queries and replies with mock data
        processDisplayRxIntercept();
    }
}

// -----------------------------------------------------------------------------
// PASS-THROUGH MODE
// -----------------------------------------------------------------------------

void SerialBridge::processDisplayRxPassThrough() {
    while (_displaySerial.available()) {
        int b = _displaySerial.read();
        if (b == -1) break;

        uint8_t byteVal = (uint8_t)b;
        _lastDisplayByteMs = millis();
        Telemetry.recordDisplayRx(1);
        Debug.traceByte(TraceDir::DisplayRx, byteVal);

        if (_displayBufLen < sizeof(_displayBuf)) {
            _displayBuf[_displayBufLen++] = byteVal;
        }

        // Direct pass-through to controller
        _controllerSerial.write(byteVal);
        Telemetry.recordControllerTx(1);
        Telemetry.recordForward();
        Debug.traceByte(TraceDir::ControllerTx, byteVal);

        // Check if we have received a complete known display packet
        if (_displayBufLen >= 2 && _displayBuf[0] == REQUEST_TYPE_BAFANG_READ) {
            _lastDisplayOpcode = _displayBuf[1];

            // Display read requests are 2 or 3 bytes
            size_t reqLen = 2;
            if (_displayBuf[1] == OPCODE_DISPLAY_READ_UNKNOWN1 ||
                _displayBuf[1] == OPCODE_DISPLAY_READ_RANGE ||
                _displayBuf[1] == OPCODE_DISPLAY_READ_CALORIES ||
                _displayBuf[1] == OPCODE_DISPLAY_READ_UNKNOWN3) {
                reqLen = 3;
            }

            if (_displayBufLen >= reqLen) {
                _displayBufLen = 0;
            }
        } else if (_displayBufLen >= 2 && _displayBuf[0] == REQUEST_TYPE_BAFANG_WRITE) {
            // Write commands
            size_t cmdLen = 4; // default
            if (_displayBuf[1] == OPCODE_DISPLAY_WRITE_LIGHTS) cmdLen = 3;
            else if (_displayBuf[1] == OPCODE_DISPLAY_WRITE_SPEED_LIM) cmdLen = 5;

            if (_displayBufLen >= cmdLen) {
                handleDisplayPacket(_displayBuf, cmdLen);
                _displayBufLen = 0;
            }
        }
    }

    // Reset buffer on framing pause (> 40ms)
    if (_displayBufLen > 0 && (millis() - _lastDisplayByteMs > 40)) {
        _displayBufLen = 0;
    }
}

void SerialBridge::handleDisplayPacket(const uint8_t* buf, size_t len) {
    if (len < 2 || buf[0] != REQUEST_TYPE_BAFANG_WRITE) return;

    switch (buf[1]) {
        case OPCODE_DISPLAY_WRITE_PAS: {
            // PAS change: 0x16 0x0b <level> <checksum>
            if (len >= 4 && verifyChecksum(buf, 4)) {
                uint8_t lvl = 0;
                switch (buf[2]) {
                    case 0x00: lvl = 0; break;
                    case 0x01: lvl = 1; break;
                    case 0x0b: lvl = 2; break;
                    case 0x0c: lvl = 3; break;
                    case 0x0d: lvl = 4; break;
                    case 0x02: lvl = 5; break;
                    case 0x15: lvl = 6; break;
                    case 0x16: lvl = 7; break;
                    case 0x17: lvl = 8; break;
                    case 0x03: lvl = 9; break;
                    case 0x06: lvl = 6; break; // Walk mode
                    default: lvl = buf[2]; break;
                }
                Telemetry.updateAssistLevel(lvl);
            }
            break;
        }
        case OPCODE_DISPLAY_WRITE_MODE: {
            // Mode change: 0x16 0x0c <mode> <checksum>
            if (len >= 4 && verifyChecksum(buf, 4)) {
                uint8_t mode = (buf[2] == 0x04) ? 1 : 0; // 0 = Standard, 1 = Sport
                Telemetry.updateOperationMode(mode);
            }
            break;
        }
        case OPCODE_DISPLAY_WRITE_LIGHTS: {
            // Lights change: 0x16 0x1a <state>
            if (len >= 3) {
                bool on = (buf[2] == 0xf1);
                Telemetry.updateLights(on);
            }
            break;
        }
    }
}

void SerialBridge::processControllerRxPassThrough() {
    while (_controllerSerial.available()) {
        int b = _controllerSerial.read();
        if (b == -1) break;

        uint8_t byteVal = (uint8_t)b;
        _lastControllerByteMs = millis();
        Telemetry.recordControllerRx(1);
        Debug.traceByte(TraceDir::ControllerRx, byteVal);

        if (_controllerBufLen < sizeof(_controllerBuf)) {
            _controllerBuf[_controllerBufLen++] = byteVal;
        }

        // Check if this is an Event Log frame from bbs-fw
        if (_controllerBufLen >= 1 && (_controllerBuf[0] == EVENT_LOG_ENTRY || _controllerBuf[0] == EVENT_LOG_DATA_ENTRY)) {
            size_t evtSize = (_controllerBuf[0] == EVENT_LOG_ENTRY) ? 3 : 5;

            if (_controllerBufLen >= evtSize) {
                if (verifyChecksum(_controllerBuf, evtSize)) {
                    uint8_t evtId = _controllerBuf[1];
                    int16_t evtData = 0;
                    bool hasData = false;

                    if (_controllerBuf[0] == EVENT_LOG_DATA_ENTRY) {
                        evtData = (int16_t)((_controllerBuf[2] << 8) | _controllerBuf[3]);
                        hasData = true;
                    }

                    Telemetry.addEvent(evtId, evtData, hasData);
                    Telemetry.recordIntercept();
                    Debug.tracef("Event log intercepted: id=%d data=%d", evtId, evtData);

                    // Do NOT forward event frames to the display (prevents display confusion)
                    _controllerBufLen = 0;
                    continue;
                } else {
                    // Checksum mismatch, discard
                    _controllerBufLen = 0;
                    continue;
                }
            }
            // Still waiting for remaining event bytes, do not forward yet
            continue;
        }

        // Normal response handling: forward to Display
        _displaySerial.write(byteVal);
        Telemetry.recordDisplayTx(1);
        Telemetry.recordForward();
        Debug.traceByte(TraceDir::DisplayTx, byteVal);

        // Check if we can parse the response for Telemetry
        handleControllerPacket(_controllerBuf, _controllerBufLen);
    }

    // Reset buffer on framing pause (> 30ms)
    if (_controllerBufLen > 0 && (millis() - _lastControllerByteMs > 30)) {
        _controllerBufLen = 0;
    }
}

void SerialBridge::handleControllerPacket(const uint8_t* buf, size_t len) {
    if (len < 1) return;

    switch (_lastDisplayOpcode) {
        case OPCODE_DISPLAY_READ_STATUS:
            if (len >= 1) {
                Telemetry.updateStatusCode(buf[0]);
                _controllerBufLen = 0;
            }
            break;

        case OPCODE_DISPLAY_READ_CURRENT:
            if (len >= 2) {
                float amps = (float)buf[0] / 2.0f;
                Telemetry.updateCurrent(amps);
                _controllerBufLen = 0;
            }
            break;

        case OPCODE_DISPLAY_READ_BATTERY:
            if (len >= 2) {
                Telemetry.updateBatteryPercent(buf[0]);
                _controllerBufLen = 0;
            }
            break;

        case OPCODE_DISPLAY_READ_SPEED:
            if (len >= 3) {
                uint16_t rpm = (buf[0] << 8) | buf[1];
                Telemetry.updateSpeedRpm(rpm);
                _controllerBufLen = 0;
            }
            break;

        case OPCODE_DISPLAY_READ_CALORIES:
            if (len >= 3) {
                uint16_t volt_x10 = (buf[0] << 8) | buf[1];
                Telemetry.updateBatteryVoltage(volt_x10 / 10.0f);
                _controllerBufLen = 0;
            }
            break;

        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// INTERCEPT / CONFIG MODE (Seamless Arbitration)
// -----------------------------------------------------------------------------

void SerialBridge::processDisplayRxIntercept() {
    while (_displaySerial.available()) {
        int b = _displaySerial.read();
        if (b == -1) break;

        uint8_t byteVal = (uint8_t)b;
        _lastDisplayByteMs = millis();
        Telemetry.recordDisplayRx(1);
        Debug.traceByte(TraceDir::DisplayRx, byteVal);

        if (_displayBufLen < sizeof(_displayBuf)) {
            _displayBuf[_displayBufLen++] = byteVal;
        }

        // Check if a read request from display is complete
        if (_displayBufLen >= 2 && _displayBuf[0] == REQUEST_TYPE_BAFANG_READ) {
            uint8_t op = _displayBuf[1];
            size_t reqLen = 2;
            if (op == OPCODE_DISPLAY_READ_UNKNOWN1 ||
                op == OPCODE_DISPLAY_READ_RANGE ||
                op == OPCODE_DISPLAY_READ_CALORIES ||
                op == OPCODE_DISPLAY_READ_UNKNOWN3) {
                reqLen = 3;
            }

            if (_displayBufLen >= reqLen) {
                // Synthesize immediate mock response to keep display alive with NO Error 30!
                synthesizeDisplayResponse(op);
                Telemetry.recordIntercept();
                _displayBufLen = 0;
            }
        } else if (_displayBufLen >= 2 && _displayBuf[0] == REQUEST_TYPE_BAFANG_WRITE) {
            // Write commands from display (e.g. user toggled lights or changed PAS on handlebar)
            size_t cmdLen = 4;
            if (_displayBuf[1] == OPCODE_DISPLAY_WRITE_LIGHTS) cmdLen = 3;
            else if (_displayBuf[1] == OPCODE_DISPLAY_WRITE_SPEED_LIM) cmdLen = 5;

            if (_displayBufLen >= cmdLen) {
                // Buffer this command so we can send it to the controller once config mode finishes!
                if (_queuedCount < MAX_QUEUED_DISPLAY_CMDS) {
                    _queuedWrites[_queuedCount].len = cmdLen;
                    memcpy(_queuedWrites[_queuedCount].data, _displayBuf, cmdLen);
                    _queuedCount++;
                }
                handleDisplayPacket(_displayBuf, cmdLen);
                _displayBufLen = 0;
            }
        }
    }

    if (_displayBufLen > 0 && (millis() - _lastDisplayByteMs > 40)) {
        _displayBufLen = 0;
    }
}

void SerialBridge::synthesizeDisplayResponse(uint8_t opcode) {
    uint8_t resp[8];
    size_t respLen = 0;

    switch (opcode) {
        case OPCODE_DISPLAY_READ_STATUS:
            resp[0] = Telemetry.getCachedStatusCode();
            respLen = 1;
            break;

        case OPCODE_DISPLAY_READ_CURRENT: {
            uint8_t amp_x2 = (uint8_t)(Telemetry.getCachedCurrentAmps() * 2.0f);
            resp[0] = amp_x2;
            resp[1] = amp_x2;
            respLen = 2;
            break;
        }

        case OPCODE_DISPLAY_READ_BATTERY: {
            uint8_t pct = Telemetry.getCachedBatteryPercent();
            resp[0] = pct;
            resp[1] = pct;
            respLen = 2;
            break;
        }

        case OPCODE_DISPLAY_READ_SPEED: {
            uint16_t rpm = Telemetry.getCachedSpeedRpm();
            uint8_t chk = 0;
            resp[0] = (uint8_t)(rpm >> 8);
            resp[1] = (uint8_t)(rpm & 0xFF);
            chk += resp[0];
            chk += resp[1];
            resp[2] = (uint8_t)(chk + 0x20);
            respLen = 3;
            break;
        }

        case OPCODE_DISPLAY_READ_UNKNOWN1:
            resp[0] = 0x00;
            resp[1] = 0x00;
            resp[2] = 0x00;
            respLen = 3;
            break;

        case OPCODE_DISPLAY_READ_RANGE: {
            uint16_t val = (uint16_t)(Telemetry.getCachedCurrentAmps() * 10.0f);
            resp[0] = (uint8_t)(val >> 8);
            resp[1] = (uint8_t)(val & 0xFF);
            resp[2] = (uint8_t)(resp[0] + resp[1]);
            respLen = 3;
            break;
        }

        case OPCODE_DISPLAY_READ_CALORIES: {
            uint16_t volt_x10 = (uint16_t)(Telemetry.getCachedVoltage() * 10.0f);
            resp[0] = (uint8_t)(volt_x10 >> 8);
            resp[1] = (uint8_t)(volt_x10 & 0xFF);
            resp[2] = (uint8_t)(resp[0] + resp[1]);
            respLen = 3;
            break;
        }

        case OPCODE_DISPLAY_READ_UNKNOWN3:
            memset(resp, 0, 5);
            respLen = 5;
            break;

        case OPCODE_DISPLAY_READ_MOVING:
            resp[0] = (Telemetry.getCachedSpeedRpm() > 0) ? 0x31 : 0x30;
            resp[1] = resp[0];
            respLen = 2;
            break;

        default:
            resp[0] = 0x00;
            respLen = 1;
            break;
    }

    if (respLen > 0) {
        _displaySerial.write(resp, respLen);
        Telemetry.recordDisplayTx(respLen);
        Debug.traceBytes(TraceDir::DisplayTx, resp, respLen);
    }
}

void SerialBridge::flushQueuedDisplayWrites() {
    for (size_t i = 0; i < _queuedCount; ++i) {
        _controllerSerial.write(_queuedWrites[i].data, _queuedWrites[i].len);
        Telemetry.recordControllerTx(_queuedWrites[i].len);
        delay(15);
    }
    _queuedCount = 0;
}

// -----------------------------------------------------------------------------
// CONTROLLER CONFIG TRANSACTIONS
// -----------------------------------------------------------------------------

// bbs-fw emits event-log frames asynchronously and they can land in the middle
// of a config transaction. They must be swallowed (and parsed into telemetry)
// rather than counted as the transaction response, otherwise a write gets
// "checksum mismatch" and fails even though the controller accepted it.
// Frame shapes: 0xEE <id> <chk> (3 bytes) or 0xED <id> <hi> <lo> <chk> (5 bytes).
// `firstByte` has already been consumed by the caller.
bool SerialBridge::consumeControllerEventFrame(uint8_t firstByte, uint32_t deadlineMs) {
    const size_t evtSize = (firstByte == EVENT_LOG_ENTRY) ? 3 : 5;
    uint8_t frame[5];
    frame[0] = firstByte;
    size_t n = 1;

    while (n < evtSize && (int32_t)(deadlineMs - millis()) > 0) {
        // Keep servicing Display so it never times out during long transfers!
        processDisplayRxIntercept();

        while (_controllerSerial.available() && n < evtSize) {
            frame[n] = (uint8_t)_controllerSerial.read();
            Telemetry.recordControllerRx(1);
            Debug.traceByte(TraceDir::ControllerRx, frame[n]);
            ++n;
        }

        if (n < evtSize) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if (n < evtSize) {
        Debug.tracef("Event frame truncated (%d of %d bytes)", (int)n, (int)evtSize);
        return false;
    }

    if (!verifyChecksum(frame, evtSize)) {
        Debug.trace("Event frame checksum mismatch, discarded");
        return true;  // garbled frame consumed; keep waiting for the response
    }

    int16_t evtData = 0;
    bool hasData = false;
    if (firstByte == EVENT_LOG_DATA_ENTRY) {
        evtData = (int16_t)((frame[2] << 8) | frame[3]);
        hasData = true;
    }

    Telemetry.addEvent(frame[1], evtData, hasData);
    Telemetry.recordIntercept();
    Debug.tracef("Event log intercepted during config: id=%d data=%d", frame[1], evtData);
    return true;
}

bool SerialBridge::receiveController(uint8_t* buf, size_t len, uint32_t timeoutMs) {
    size_t received = 0;
    uint32_t deadlineMs = millis() + timeoutMs;

    while (received < len && (int32_t)(deadlineMs - millis()) > 0) {
        // Keep servicing Display so it never times out during long transfers!
        processDisplayRxIntercept();

        while (_controllerSerial.available()) {
            uint8_t byteVal = (uint8_t)_controllerSerial.read();
            Telemetry.recordControllerRx(1);
            Debug.traceByte(TraceDir::ControllerRx, byteVal);

            // At a frame boundary an async event-log frame may be interleaved
            // with the response we are waiting for.
            if (received == 0 && (byteVal == EVENT_LOG_ENTRY || byteVal == EVENT_LOG_DATA_ENTRY)) {
                if (!consumeControllerEventFrame(byteVal, deadlineMs)) {
                    return false;
                }
                continue;
            }

            buf[received++] = byteVal;
            if (received >= len) break;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (received < len) {
        Debug.tracef("Controller timeout: got %d of %d bytes", received, len);
        return false;
    }
    return true;
}

bool SerialBridge::sendAndReceiveController(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t expectedLen, uint32_t timeoutMs) {
    // Flush stale data from controller RX
    while (_controllerSerial.available()) {
        _controllerSerial.read();
    }

    // Send request
    _controllerSerial.write(txBuf, txLen);
    Telemetry.recordControllerTx(txLen);
    Debug.traceBytes(TraceDir::ControllerTx, txBuf, txLen);

    if (!receiveController(rxBuf, expectedLen, timeoutMs)) {
        Serial.printf("[Bridge] Timeout waiting for controller response (%d bytes expected)\n", expectedLen);
        return false;
    }

    bool checksumOk = verifyChecksum(rxBuf, expectedLen);
    if (!checksumOk) {
        Debug.tracef("Controller response checksum mismatch (len=%d)", expectedLen);
    }
    return checksumOk;
}

bool SerialBridge::readFirmwareInfo(uint8_t& major, uint8_t& minor, uint8_t& patch, uint8_t& cfgVer, ControllerType& ctrlType, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    uint8_t req[3] = { REQUEST_TYPE_READ, OPCODE_READ_FW_VERSION, 0 };
    req[2] = computeChecksum(req, 2);

    uint8_t resp[8];
    bool ok = sendAndReceiveController(req, 3, resp, 8, timeoutMs);

    if (!ok) {
        // Try fallback for V1 firmware format (7 bytes)
        ok = sendAndReceiveController(req, 3, resp, 7, 1000);
        if (ok) {
            major = resp[2];
            minor = resp[3];
            patch = resp[4];
            cfgVer = resp[5];
            ctrlType = ControllerType::BBSHD;
        }
    } else {
        major = resp[2];
        minor = resp[3];
        patch = resp[4];
        cfgVer = resp[5];
        ctrlType = (ControllerType)resp[6];
    }

    if (ok) {
        _configVersion = cfgVer;
        Telemetry.updateFirmwareInfo(major, minor, patch, cfgVer, ctrlType);
        Debug.tracef("FW info: %s v%d.%d.%d (cfg v%d)", getControllerTypeName(ctrlType), major, minor, patch, cfgVer);
    } else {
        Debug.trace("readFirmwareInfo FAILED: no response");
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::readConfig(BbsFwConfigV5& config, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Debug.trace("readConfig: mutex acquire failed");
        return false;
    }

    Debug.trace("Entering CONFIG_INTERCEPT for readConfig");
    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    uint8_t req[3] = { REQUEST_TYPE_READ, OPCODE_READ_CONFIG, 0 };
    req[2] = computeChecksum(req, 2);

    // Flush stale data from controller RX, then send request
    while (_controllerSerial.available()) {
        _controllerSerial.read();
    }
    _controllerSerial.write(req, 3);
    Telemetry.recordControllerTx(3);
    Debug.traceBytes(TraceDir::ControllerTx, req, 3);

    // Phase 1: read the 4-byte header (req, opcode, version, length) so we know
    // how many payload bytes to expect before we commit to a fixed-length receive.
    uint8_t frame[4 + BBS_FW_CONFIG_V5_SIZE + 1]; // max possible V5 frame = 159 bytes
    bool ok = receiveController(frame, 4, timeoutMs);

    if (!ok) {
        Debug.trace("readConfig FAILED: no header response");
    } else {
        uint8_t ver = frame[2];
        uint8_t len = frame[3];
        _configVersion = ver;
        Debug.tracef("readConfig header: ver=%d len=%d", ver, len);

        bool knownLen = (ver == BBS_FW_CONFIG_VERSION && len == BBS_FW_CONFIG_V5_SIZE) ||
                        (ver == BBS_FW_CONFIG_VERSION_4 && len == BBS_FW_CONFIG_V4_SIZE);
        size_t total = 4 + (size_t)len + 1; // header + payload + checksum

        // Phase 2: read the config payload + trailing checksum byte into the same
        // contiguous buffer so verifyChecksum works over the full original frame.
        if (knownLen && total <= sizeof(frame)) {
            ok = receiveController(frame + 4, (size_t)len + 1, timeoutMs);
            if (ok) {
                ok = verifyChecksum(frame, total);
                if (ok) {
                    if (ver == BBS_FW_CONFIG_VERSION_4) {
                        BbsFwConfigV4 v4;
                        memcpy(&v4, frame + 4, BBS_FW_CONFIG_V4_SIZE);
                        convertConfigV4toV5(v4, config);
                    } else {
                        memcpy(&config, frame + 4, BBS_FW_CONFIG_V5_SIZE);
                    }
                    Telemetry.addEvent(2, 0, false); // EVT_MSG_CONFIG_READ_DONE
                    Debug.tracef("readConfig OK: ver=%d len=%d", ver, len);
                } else {
                    Debug.trace("readConfig FAILED: checksum mismatch");
                }
            } else {
                Debug.tracef("readConfig FAILED: payload timeout (ver=%d len=%d)", ver, len);
            }
        } else {
            Serial.printf("[Bridge] Config version unsupported: ver=%d, len=%d\n", ver, len);
            Debug.tracef("readConfig FAILED: unsupported version/length (ver=%d, len=%d)", ver, len);
            ok = false;
        }
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::writeConfig(const BbsFwConfigV5& config, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Debug.trace("writeConfig: mutex acquire failed");
        return false;
    }

    Debug.trace("Entering CONFIG_INTERCEPT for writeConfig");
    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    // Frame: 0x02, 0xf1, version, len, ...config bytes..., checksum
    // Frame the write in the controller's own config version: V4 (152 bytes) if the
    // controller predates the pretension fields, else V5 (154 bytes).
    uint8_t ver;
    size_t cfgSize;
    uint8_t txBuf[4 + BBS_FW_CONFIG_V5_SIZE + 1]; // max V5 frame

    if (_configVersion == BBS_FW_CONFIG_VERSION_4) {
        BbsFwConfigV4 v4;
        convertConfigV5toV4(config, v4);
        ver = BBS_FW_CONFIG_VERSION_4;
        cfgSize = BBS_FW_CONFIG_V4_SIZE;
        memcpy(txBuf + 4, &v4, BBS_FW_CONFIG_V4_SIZE);
    } else {
        ver = BBS_FW_CONFIG_VERSION;
        cfgSize = BBS_FW_CONFIG_V5_SIZE;
        memcpy(txBuf + 4, &config, BBS_FW_CONFIG_V5_SIZE);
    }

    const size_t txLen = 4 + cfgSize + 1;
    txBuf[0] = REQUEST_TYPE_WRITE;
    txBuf[1] = OPCODE_WRITE_CONFIG;
    txBuf[2] = ver;
    txBuf[3] = (uint8_t)cfgSize;
    txBuf[txLen - 1] = computeChecksum(txBuf, txLen - 1);

    // Controller response: 0x02, 0xf1, result, checksum (4 bytes)
    uint8_t resp[4];
    bool ok = sendAndReceiveController(txBuf, txLen, resp, 4, timeoutMs);

    if (ok) {
        bool success = (resp[2] != 0);
        if (success) {
            Telemetry.addEvent(4, 0, false); // EVT_MSG_CONFIG_WRITE_DONE
            Debug.tracef("writeConfig OK (ver=%d)", ver);
        } else {
            Debug.trace("writeConfig FAILED: controller returned status=0");
        }
        ok = success;
    } else {
        Debug.trace("writeConfig FAILED: no valid response");
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::resetConfig(uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    uint8_t req[3] = { REQUEST_TYPE_WRITE, OPCODE_WRITE_RESET_CONFIG, 0 };
    req[2] = computeChecksum(req, 2);

    uint8_t resp[4];
    bool ok = sendAndReceiveController(req, 3, resp, 4, timeoutMs);

    if (ok) {
        bool success = (resp[2] != 0);
        if (success) {
            Telemetry.addEvent(3, 0, false); // EVT_MSG_CONFIG_RESET
        }
        ok = success;
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::calibrateVoltage(float measuredVolts, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    uint16_t volts_x100 = (uint16_t)(measuredVolts * 100.0f);
    uint8_t req[5] = {
        REQUEST_TYPE_WRITE,
        OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION,
        (uint8_t)(volts_x100 >> 8),
        (uint8_t)(volts_x100 & 0xFF),
        0
    };
    req[4] = computeChecksum(req, 4);

    uint8_t resp[5];
    bool ok = sendAndReceiveController(req, 5, resp, 5, timeoutMs);

    if (ok) {
        Telemetry.addEvent(146, (int16_t)volts_x100, true);
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::enableEventLog(bool enable, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    uint8_t req[4] = { REQUEST_TYPE_WRITE, OPCODE_WRITE_EVTLOG_ENABLE, (uint8_t)(enable ? 1 : 0), 0 };
    req[3] = computeChecksum(req, 3);

    uint8_t resp[4];
    bool ok = sendAndReceiveController(req, 4, resp, 4, timeoutMs);

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

void SerialBridge::injectPasLevel(uint8_t level) {
    uint8_t bafangLvl = 0x00;
    switch (level) {
        case 0: bafangLvl = 0x00; break;
        case 1: bafangLvl = 0x01; break;
        case 2: bafangLvl = 0x0b; break;
        case 3: bafangLvl = 0x0c; break;
        case 4: bafangLvl = 0x0d; break;
        case 5: bafangLvl = 0x02; break;
        case 6: bafangLvl = 0x15; break;
        case 7: bafangLvl = 0x16; break;
        case 8: bafangLvl = 0x17; break;
        case 9: bafangLvl = 0x03; break;
        default: bafangLvl = 0x01; break;
    }

    uint8_t pkt[4] = { REQUEST_TYPE_BAFANG_WRITE, OPCODE_DISPLAY_WRITE_PAS, bafangLvl, 0 };
    pkt[3] = computeChecksum(pkt, 3);

    _controllerSerial.write(pkt, 4);
    Telemetry.updateAssistLevel(level);
}

void SerialBridge::injectLights(bool on) {
    uint8_t state = on ? 0xf1 : 0xf0;
    uint8_t pkt[3] = { REQUEST_TYPE_BAFANG_WRITE, OPCODE_DISPLAY_WRITE_LIGHTS, state };

    _controllerSerial.write(pkt, 3);
    Telemetry.updateLights(on);
}

void SerialBridge::injectOperationMode(uint8_t mode) {
    uint8_t state = (mode == 1) ? 0x04 : 0x02; // 0x02 = Standard, 0x04 = Sport
    uint8_t pkt[4] = { REQUEST_TYPE_BAFANG_WRITE, OPCODE_DISPLAY_WRITE_MODE, state, 0 };
    pkt[3] = computeChecksum(pkt, 3);

    _controllerSerial.write(pkt, 4);
    Telemetry.updateOperationMode(mode);
}
