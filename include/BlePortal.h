#ifndef BLE_PORTAL_H
#define BLE_PORTAL_H

#include <Arduino.h>
#include "Config.h"

#if BLE_TRANSPORT_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "BleProtocol.h"

class BLEServer;
class BLECharacteristic;

// BLE GATT transport for the dashboard API.
//
// The GATT callbacks run on the Bluedroid task, so they never touch the bridge or
// the API: they only append bytes to a receive ring. BlePortal::process(), called
// from loop(), reassembles a request, hands it to WebPortal::dispatchApi() and
// notifies the reply back. That keeps every 1200-baud transaction on the main
// task, which is what the display keep-alive arbitration depends on.
class BlePortal {
public:
    void begin();
    void process();

    bool isConnected() const { return _connected; }

    // Called from the Bluedroid task by the GATT callbacks. These do nothing but
    // copy bytes and flip a flag; all other state is owned by loop().
    void onRxData(const uint8_t* data, size_t len);
    void onConnect() { _connected = true; }
    void onDisconnect() { _connected = false; }

private:
    static constexpr size_t kRingCapacity = 2048;
    static constexpr size_t kAssembleCapacity = BLE_MAX_FRAME_BODY + 64;
    static constexpr size_t kQueueDepth = 4;
    static constexpr size_t kMaxNotifyChunk = 500;

    struct PendingRequest {
        uint16_t seq = 0;
        uint8_t method = BLE_METHOD_GET;
        String path;
        String body;
    };

    BLEServer* _server = nullptr;
    BLECharacteristic* _request = nullptr;
    BLECharacteristic* _response = nullptr;

    // Single producer (Bluedroid task) / single consumer (loop) byte stream.
    uint8_t _ring[kRingCapacity];
    size_t _ringHead = 0;
    size_t _ringTail = 0;
    SemaphoreHandle_t _ringMutex = nullptr;

    // Reassembly buffer, touched only from loop().
    uint8_t _assemble[kAssembleCapacity];
    size_t _assembleLen = 0;

    PendingRequest _queue[kQueueDepth];
    size_t _queueCount = 0;

    uint8_t _notifyBuffer[BLE_RESPONSE_HEADER_LEN + kMaxNotifyChunk];

    volatile bool _connected = false;
    bool _linkUp = false;          // loop()-side view of _connected, for edge logs
    bool _advertising = false;
    uint32_t _disconnectedMs = 0;
    uint16_t _notifyChunk = 20;    // ATT MTU minus the 3 byte notification header

    // Unsolicited pushes. Telemetry runs on a timer; the event log is pushed only
    // when TelemetryTracker::getEventSeq() moves, so a quiet bike is silent.
    uint16_t _pushSeq = BLE_PUSH_SEQ_BASE;
    uint32_t _lastTelemetryPushMs = 0;
    uint32_t _lastPushedEventSeq = 0;
    bool _havePushedEvents = false;

    // PIN gate. _authenticated is per connection and cleared whenever a new link
    // comes up; the failure counter is not, so reconnecting does not reset it.
    bool _authenticated = false;
    uint8_t _authFailures = 0;
    uint32_t _authLockoutUntilMs = 0;

    void drainRing();
    void assembleRequests();
    void dispatchOne();
    void enqueue(uint16_t seq, uint8_t method, const char* path, size_t pathLen,
                 const uint8_t* body, size_t bodyLen);
    void dropByte();
    void handleAuthRequest(const PendingRequest& req);
    void pushUpdates();
    void notifyFrame(uint16_t seq, uint16_t status, const String& body);
    void updateMtu();
};

extern BlePortal Ble;

#endif // BLE_TRANSPORT_ENABLED

#endif // BLE_PORTAL_H
