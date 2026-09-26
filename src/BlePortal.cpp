#include "BlePortal.h"

#if BLE_TRANSPORT_ENABLED

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string.h>

#include "WebPortal.h"
#include "Telemetry.h"
#include "DebugLog.h"

BlePortal Ble;

namespace {

// Bluedroid's callbacks are plain virtual classes, so they reach the singleton
// through this pointer instead of a captured lambda.
BlePortal* g_ble = nullptr;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        (void)server;
        if (g_ble != nullptr) {
            g_ble->onConnect();
        }
    }

    void onDisconnect(BLEServer* server) override {
        (void)server;
        if (g_ble != nullptr) {
            g_ble->onDisconnect();
        }
    }
};

class RequestCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        if (g_ble == nullptr || characteristic == nullptr) {
            return;
        }
        // The core has already copied the payload into the characteristic value
        // by the time onWrite fires, so getData()/getLength() are valid here.
        g_ble->onRxData(characteristic->getData(), characteristic->getLength());
    }
};

} // namespace

void BlePortal::begin() {
    g_ble = this;

    // Bluedroid needs one large *contiguous* allocation, so the largest free
    // block matters more than total free heap. These go to the USB console
    // rather than through Debug.trace, because tracing is off by default
    // (DEBUG_TRACE_ENABLED_DEFAULT 0) and this is the only boot-time evidence
    // that the transport came up -- or why it did not.
    const uint32_t heapBefore = ESP.getFreeHeap();
    const uint32_t blockBefore = ESP.getMaxAllocHeap();
    Serial.printf("[BLE] init: free heap %u, largest block %u\n", heapBefore, blockBefore);

    _ringMutex = xSemaphoreCreateMutex();
    if (_ringMutex == nullptr) {
        Serial.println("[BLE] mutex allocation failed - transport disabled");
        return;
    }

    BLEDevice::init(BLE_DEVICE_NAME);
    BLEDevice::setMTU(BLE_PREFERRED_MTU);

    _server = BLEDevice::createServer();
    if (_server == nullptr) {
        // Almost always memory: Wi-Fi has taken the best of the heap by now.
        Serial.printf("[BLE] server allocation failed (free heap %u, largest block %u)"
                      " - transport disabled\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return;
    }
    _server->setCallbacks(new ServerCallbacks());

    BLEService* service = _server->createService(BLE_SERVICE_UUID);
    if (service == nullptr) {
        Serial.printf("[BLE] service allocation failed (free heap %u) - transport disabled\n",
                      ESP.getFreeHeap());
        return;
    }

    // Fixed descriptor; anything dynamic (IPs, config version, heap) comes from
    // /api/info over the request/response pair. Keeping it immutable means the
    // read callback never has to touch shared state from the Bluedroid task.
    BLECharacteristic* info = service->createCharacteristic(
        BLE_CHAR_INFO_UUID, BLECharacteristic::PROPERTY_READ);
    const String descriptor = String("{\"protocol\":") + BLE_PROTOCOL_VERSION +
                              ",\"name\":\"" BLE_DEVICE_NAME "\"" +
                              ",\"fw\":\"" FW_VERSION "\"}";
    info->setValue((uint8_t*)descriptor.c_str(), descriptor.length());

    _request = service->createCharacteristic(
        BLE_CHAR_REQUEST_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    _request->setCallbacks(new RequestCallbacks());

    _response = service->createCharacteristic(
        BLE_CHAR_RESPONSE_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    _response->addDescriptor(new BLE2902());  // lets the client enable notifications

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    // Connection interval hints; phones pick a value in this range.
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    _advertising = true;

    Serial.printf("[BLE] advertising as %s (free heap %u, largest block %u)\n",
                  BLE_DEVICE_NAME, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void BlePortal::process() {
    if (_ringMutex == nullptr || _server == nullptr) {
        return;
    }

    // Connection state is owned by the Bluedroid task, but everything that
    // follows from it is handled here on the main task so the callbacks stay
    // trivial (and cannot race with the bridge or the API).
    const bool connected = _connected;
    if (connected != _linkUp) {
        _linkUp = connected;
        if (connected) {
            Debug.trace("BLE: client connected");
            // Every new link must authenticate again.
            _authenticated = false;
            // Push a full snapshot immediately rather than waiting a tick, and
            // re-send the event log even if nothing has happened since the last
            // client (it has never seen it).
            _lastTelemetryPushMs = millis() - BLE_TELEMETRY_PUSH_MS;
            _havePushedEvents = false;
        } else {
            Debug.trace("BLE: client disconnected");
            _advertising = false;
            _disconnectedMs = millis();
            // Any half-received request is meaningless once the link drops.
            _assembleLen = 0;
            _queueCount = 0;
        }
    }

    if (connected) {
        updateMtu();
    } else if (!_advertising && (millis() - _disconnectedMs) > 500) {
        // Advertise again once the stack has settled after the disconnect.
        BLEDevice::startAdvertising();
        _advertising = true;
    }

    drainRing();
    assembleRequests();
    dispatchOne();
    pushUpdates();
}

void BlePortal::onRxData(const uint8_t* data, size_t len) {
    if (_ringMutex == nullptr || data == nullptr || len == 0) {
        return;
    }

    // Runs on the Bluedroid task: never block it for long.
    if (xSemaphoreTake(_ringMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        const size_t next = (_ringHead + 1) % kRingCapacity;
        if (next == _ringTail) {
            break;  // overflow: drop the excess rather than stall the BT stack
        }
        _ring[_ringHead] = data[i];
        _ringHead = next;
    }

    xSemaphoreGive(_ringMutex);
}

void BlePortal::drainRing() {
    if (xSemaphoreTake(_ringMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    while (_ringTail != _ringHead && _assembleLen < kAssembleCapacity) {
        _assemble[_assembleLen++] = _ring[_ringTail];
        _ringTail = (_ringTail + 1) % kRingCapacity;
    }

    xSemaphoreGive(_ringMutex);
}

void BlePortal::assembleRequests() {
    while (_assembleLen >= BLE_REQUEST_HEADER_LEN) {
        const uint16_t seq = (uint16_t)(_assemble[0] | (_assemble[1] << 8));
        const uint8_t method = _assemble[2];
        const uint16_t pathLen = (uint16_t)(_assemble[4] | (_assemble[5] << 8));
        const uint16_t bodyLen = (uint16_t)(_assemble[6] | (_assemble[7] << 8));
        const size_t total = BLE_REQUEST_HEADER_LEN + (size_t)pathLen + (size_t)bodyLen;

        const bool plausible = (pathLen > 0) &&
                               (bodyLen <= BLE_MAX_FRAME_BODY) &&
                               (total <= kAssembleCapacity);
        if (!plausible) {
            // The stream lost byte alignment; resynchronise one byte at a time.
            dropByte();
            continue;
        }

        if (_assembleLen < total) {
            break;  // the rest of the frame has not arrived yet
        }

        if (_queueCount < kQueueDepth) {
            enqueue(seq, method,
                    (const char*)(_assemble + BLE_REQUEST_HEADER_LEN), pathLen,
                    _assemble + BLE_REQUEST_HEADER_LEN + pathLen, bodyLen);
        }
        // else: the client is outpacing loop(); drop rather than grow unbounded.

        const size_t remaining = _assembleLen - total;
        memmove(_assemble, _assemble + total, remaining);
        _assembleLen = remaining;
    }
}

void BlePortal::enqueue(uint16_t seq, uint8_t method, const char* path, size_t pathLen,
                        const uint8_t* body, size_t bodyLen) {
    PendingRequest& slot = _queue[_queueCount++];
    slot.seq = seq;
    slot.method = method;
    slot.path = String(path, pathLen);
    slot.body = (bodyLen > 0) ? String((const char*)body, bodyLen) : String();
}

void BlePortal::dropByte() {
    if (_assembleLen == 0) {
        return;
    }
    const size_t remaining = _assembleLen - 1;
    memmove(_assemble, _assemble + 1, remaining);
    _assembleLen = remaining;
}

void BlePortal::dispatchOne() {
    if (_queueCount == 0) {
        return;
    }

    // Pop the head. Requests that arrived while we were busy stay queued for the
    // next iteration -- one API call per process() keeps the loop responsive.
    const PendingRequest req = _queue[0];
    for (size_t i = 1; i < _queueCount; ++i) {
        _queue[i - 1] = _queue[i];
    }
    _queueCount--;

    if (!_connected) {
        return;  // client vanished; there is nobody to answer
    }

    // The dashboard appends a query string to some paths; the route table is
    // path-only, so split it off and parse what we understand.
    String path = req.path;
    uint32_t afterSeq = 0;
    const int query = path.indexOf('?');
    if (query >= 0) {
        afterSeq = parseAfterParam(path.c_str() + query + 1);
        path = path.substring(0, query);
    }

    // PIN gate. When a PIN is configured, an unauthenticated client may do
    // exactly one thing: present it.
    if (strcmp(path.c_str(), BLE_AUTH_PATH) == 0) {
        handleAuthRequest(req);
        return;
    }
    if (!_authenticated && Portal.getBlePin().length() > 0) {
        notifyFrame(req.seq, 401,
                    "{\"success\":false,\"error\":\"Bluetooth PIN required\"}");
        return;
    }

    // `path` stays alive for the whole call, so c_str() is valid for the
    // synchronous dispatch below.
    ApiRequest api;
    api.path = path.c_str();
    api.post = (req.method == BLE_METHOD_POST);
    api.body = api.post ? &req.body : nullptr;
    api.afterSeq = afterSeq;

    ApiResponse res;
    if (!Portal.dispatchApi(api, res)) {
        res.status = 404;
        res.body = "{\"error\":\"Not found\"}";
        res.html = false;
    }

    Debug.tracef("BLE: %s %s -> %d (%u bytes)", api.post ? "POST" : "GET",
                 path.c_str(), res.status, (unsigned)res.body.length());

    notifyFrame(req.seq, (uint16_t)res.status, res.body);
}

void BlePortal::handleAuthRequest(const PendingRequest& req) {
    const String pin = Portal.getBlePin();

    if (pin.length() == 0) {
        // No PIN configured: the API is open, so this is just a handshake.
        _authenticated = true;
        notifyFrame(req.seq, 200, "{\"success\":true,\"pinRequired\":false}");
        return;
    }

    // Wraparound-safe comparison against the lockout deadline.
    if (_authLockoutUntilMs != 0 && (int32_t)(millis() - _authLockoutUntilMs) < 0) {
        notifyFrame(req.seq, 429,
                    "{\"success\":false,\"error\":\"Too many PIN attempts; wait a moment\"}");
        return;
    }

    String supplied;
    if (req.body.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, req.body)) {
            supplied = String((const char*)(doc["pin"] | ""));
        }
    }

    if (supplied.length() > 0 && supplied == pin) {
        _authenticated = true;
        _authFailures = 0;
        _authLockoutUntilMs = 0;
        Debug.trace("BLE: client authenticated");
        notifyFrame(req.seq, 200, "{\"success\":true,\"pinRequired\":true}");
        return;
    }

    if (_authFailures < 0xff) {
        _authFailures++;
    }
    if (_authFailures >= BLE_AUTH_MAX_ATTEMPTS) {
        _authFailures = 0;
        _authLockoutUntilMs = millis() + BLE_AUTH_LOCKOUT_MS;
        Debug.trace("BLE: too many PIN attempts, backing off");
    } else {
        Debug.trace("BLE: PIN rejected");
    }

    notifyFrame(req.seq, 401,
                "{\"success\":false,\"error\":\"Bluetooth PIN required\"}");
}

void BlePortal::pushUpdates() {
    // Pushes are courtesy traffic: they must never compete with a reply or run
    // while the link is down, and they must never stream data to a client that
    // has not presented the PIN.
    if (!_connected || _response == nullptr) {
        return;
    }
    if (!_authenticated && Portal.getBlePin().length() > 0) {
        return;
    }

    const uint32_t now = millis();
    if (now - _lastTelemetryPushMs >= BLE_TELEMETRY_PUSH_MS) {
        _lastTelemetryPushMs = now;
        _pushSeq = (_pushSeq >= 0xffff) ? BLE_PUSH_SEQ_BASE : (uint16_t)(_pushSeq + 1);

        JsonDocument doc;
        Telemetry.buildTelemetryJson(doc);
        String json;
        serializeJson(doc, json);
        notifyFrame(_pushSeq, BLE_PUSH_TELEMETRY, json);
    }

    const uint32_t eventSeq = Telemetry.getEventSeq();
    if (!_havePushedEvents || eventSeq != _lastPushedEventSeq) {
        _lastPushedEventSeq = eventSeq;
        _havePushedEvents = true;
        _pushSeq = (_pushSeq >= 0xffff) ? BLE_PUSH_SEQ_BASE : (uint16_t)(_pushSeq + 1);

        JsonDocument doc;
        Telemetry.buildEventsJson(doc);
        String json;
        serializeJson(doc, json);
        notifyFrame(_pushSeq, BLE_PUSH_EVENTS, json);
    }
}

void BlePortal::notifyFrame(uint16_t seq, uint16_t status, const String& body) {
    if (_response == nullptr || !_connected) {
        return;
    }

    // The response header carries a 16 bit length, so a body beyond that cannot
    // be described. Nothing the API produces comes close.
    size_t total = body.length();
    if (total > 0xFFFF) {
        total = 0xFFFF;
    }

    const uint8_t header[BLE_RESPONSE_HEADER_LEN] = {
        (uint8_t)(seq & 0xff), (uint8_t)(seq >> 8),
        (uint8_t)(status & 0xff), (uint8_t)(status >> 8),
        (uint8_t)(total & 0xff), (uint8_t)(total >> 8),
    };

    size_t room = (_notifyChunk > BLE_RESPONSE_HEADER_LEN)
                ? (size_t)(_notifyChunk - BLE_RESPONSE_HEADER_LEN)
                : 1;
    if (room > kMaxNotifyChunk) {
        room = kMaxNotifyChunk;
    }

    size_t offset = 0;
    do {
        size_t n = total - offset;
        if (n > room) {
            n = room;
        }

        memcpy(_notifyBuffer, header, BLE_RESPONSE_HEADER_LEN);
        if (n > 0) {
            memcpy(_notifyBuffer + BLE_RESPONSE_HEADER_LEN, body.c_str() + offset, n);
        }

        _response->setValue(_notifyBuffer, BLE_RESPONSE_HEADER_LEN + n);
        _response->notify();

        offset += n;
    } while (offset < total);
}

void BlePortal::updateMtu() {
    const uint16_t mtu = _server->getPeerMTU(_server->getConnId());
    if (mtu < 23) {
        return;  // exchange has not completed yet; keep using the safe default
    }

    size_t chunk = (size_t)mtu - 3;  // ATT notification header
    if (chunk > kMaxNotifyChunk) {
        chunk = kMaxNotifyChunk;
    }
    _notifyChunk = (uint16_t)chunk;
}

#endif // BLE_TRANSPORT_ENABLED
