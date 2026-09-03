#include "SerialBridge.h"

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
}

BridgeState SerialBridge::getState() const {
    return _state;
}

bool SerialBridge::isInterceptActive() const {
    return _state == BridgeState::CONFIG_INTERCEPT;
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

        if (_displayBufLen < sizeof(_displayBuf)) {
            _displayBuf[_displayBufLen++] = byteVal;
        }

        // Direct pass-through to controller
        _controllerSerial.write(byteVal);
        Telemetry.recordControllerTx(1);
        Telemetry.recordForward();

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

bool SerialBridge::sendAndReceiveController(const uint8_t* txBuf, size_t txLen, uint8_t* rxBuf, size_t expectedLen, uint32_t timeoutMs) {
    // Flush stale data from controller RX
    while (_controllerSerial.available()) {
        _controllerSerial.read();
    }

    // Send request
    _controllerSerial.write(txBuf, txLen);
    Telemetry.recordControllerTx(txLen);

    size_t received = 0;
    uint32_t startMs = millis();

    while (received < expectedLen && (millis() - startMs) < timeoutMs) {
        // Keep servicing Display so it never times out during long transfers!
        processDisplayRxIntercept();

        while (_controllerSerial.available()) {
            rxBuf[received++] = (uint8_t)_controllerSerial.read();
            Telemetry.recordControllerRx(1);
            if (received >= expectedLen) break;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (received < expectedLen) {
        Serial.printf("[Bridge] Timeout waiting for controller! Got %d of %d bytes\n", received, expectedLen);
        return false;
    }

    return verifyChecksum(rxBuf, expectedLen);
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
        Telemetry.updateFirmwareInfo(major, minor, patch, cfgVer, ctrlType);
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::readConfig(BbsFwConfigV5& config, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    uint8_t req[3] = { REQUEST_TYPE_READ, OPCODE_READ_CONFIG, 0 };
    req[2] = computeChecksum(req, 2);

    // Expected: 4 bytes header (req, opcode, version, len) + 154 bytes + 1 byte checksum = 159 bytes
    const size_t expectedLen = 4 + BBS_FW_CONFIG_V5_SIZE + 1;
    uint8_t resp[expectedLen];

    bool ok = sendAndReceiveController(req, 3, resp, expectedLen, timeoutMs);

    if (ok) {
        uint8_t ver = resp[2];
        uint8_t len = resp[3];
        if (ver == BBS_FW_CONFIG_VERSION && len == BBS_FW_CONFIG_V5_SIZE) {
            memcpy(&config, resp + 4, sizeof(BbsFwConfigV5));
            Telemetry.addEvent(2, 0, false); // EVT_MSG_CONFIG_READ_DONE
        } else {
            Serial.printf("[Bridge] Config version mismatch: ver=%d, len=%d\n", ver, len);
            ok = false;
        }
    }

    flushQueuedDisplayWrites();
    _state = BridgeState::PASS_THROUGH;
    xSemaphoreGive(_bridgeMutex);
    return ok;
}

bool SerialBridge::writeConfig(const BbsFwConfigV5& config, uint32_t timeoutMs) {
    if (xSemaphoreTake(_bridgeMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    _state = BridgeState::CONFIG_INTERCEPT;
    delay(BUS_QUIET_TIME_MS);

    // Frame: 0x02, 0xf1, version, len, ...config bytes..., checksum
    const size_t txLen = 4 + BBS_FW_CONFIG_V5_SIZE + 1;
    uint8_t txBuf[txLen];

    txBuf[0] = REQUEST_TYPE_WRITE;
    txBuf[1] = OPCODE_WRITE_CONFIG;
    txBuf[2] = BBS_FW_CONFIG_VERSION;
    txBuf[3] = BBS_FW_CONFIG_V5_SIZE;
    memcpy(txBuf + 4, &config, sizeof(BbsFwConfigV5));
    txBuf[txLen - 1] = computeChecksum(txBuf, txLen - 1);

    // Controller response: 0x02, 0xf1, result, checksum (4 bytes)
    uint8_t resp[4];
    bool ok = sendAndReceiveController(txBuf, txLen, resp, 4, timeoutMs);

    if (ok) {
        bool success = (resp[2] != 0);
        if (success) {
            Telemetry.addEvent(4, 0, false); // EVT_MSG_CONFIG_WRITE_DONE
        }
        ok = success;
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
