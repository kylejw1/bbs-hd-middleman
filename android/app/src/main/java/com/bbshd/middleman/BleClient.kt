package com.bbshd.middleman

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/**
 * The BLE side of the middleman, as a native Android client.
 *
 * This speaks exactly the framing documented in the firmware's
 * `include/BleProtocol.h`, which `web/index.html` also implements. All three have
 * to agree on the wire format; if a change is made here it must be made there.
 *
 *   request   u16 seq | u8 method | u8 rsvd | u16 pathLen | u16 bodyLen | path | body
 *   response  u16 seq | u16 status | u16 totalLen | body slice
 *
 * Little endian throughout. Both directions are byte streams, so both sides
 * reassemble consecutive slices; the response header repeats on every slice so
 * each notification is self describing.
 *
 * Replies and unsolicited pushes share the response characteristic and are told
 * apart by the sequence range (client requests are 1..0x7FFF, device pushes are
 * 0x8000..0xFFFF).
 *
 * Threading: every GATT operation and every piece of mutable state is confined to
 * the main thread. Callbacks arrive on a binder thread and are re-posted
 * immediately, which serialises them behind whatever the UI is doing and avoids
 * having to lock the write queue or the inflight map.
 */
class BleClient(private val context: Context, private val listener: Listener) {

    enum class State { IDLE, SCANNING, CONNECTING, READY, FAILED }

    interface Listener {
        fun onState(state: State, detail: String)

        /** A live telemetry snapshot: the same JSON `/api/telemetry` returns. */
        fun onTelemetry(json: String) = Unit

        /** The event log, as the same JSON array `/api/events` returns. */
        fun onEvents(json: String) = Unit

        /** The device has a PIN configured and ours was not accepted. */
        fun onPinRequired() = Unit
    }

    companion object {
        private const val TAG = "BleClient"

        // Must match include/BleProtocol.h and web/index.html.
        val SERVICE: UUID = UUID.fromString("6b9a0001-7c3f-4a5e-9c8d-1e2f3a4b5c6d")
        val CHAR_INFO: UUID = UUID.fromString("6b9a0002-7c3f-4a5e-9c8d-1e2f3a4b5c6d")
        val CHAR_REQUEST: UUID = UUID.fromString("6b9a0003-7c3f-4a5e-9c8d-1e2f3a4b5c6d")
        val CHAR_RESPONSE: UUID = UUID.fromString("6b9a0004-7c3f-4a5e-9c8d-1e2f3a4b5c6d")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        private const val PUSH_SEQ_BASE = 0x8000
        private const val PUSH_TELEMETRY = 1
        private const val PUSH_EVENTS = 2

        private const val REQUEST_TIMEOUT_MS = 20_000L
        private const val SCAN_TIMEOUT_MS = 12_000L
        private const val CONNECT_TIMEOUT_MS = 12_000L
        private const val WRITE_ACK_TIMEOUT_MS = 5_000L
        private const val MAX_CHUNK = 244
    }

    private val main = Handler(Looper.getMainLooper())
    private val adapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private var gatt: BluetoothGatt? = null
    private var requestChar: BluetoothGattCharacteristic? = null
    private var responseChar: BluetoothGattCharacteristic? = null

    /** Set once the GATT link is fully set up; requests are refused before this. */
    private var linked = false

    /**
     * True only once the device accepted our PIN. `linked` says the plumbing is
     * ready; this says the API will actually answer, which is what the UI needs
     * before offering to read or write anything.
     */
    private var ready = false
    private var everReady = false
    private var manualDisconnect = true

    /**
     * Set when the device rejected our PIN. While this is true the client does
     * not reconnect: the only thing that can fix it is the user entering a PIN,
     * and retrying just burns the radio (and, on a device with a stale bond,
     * escalates into a pairing storm). Cleared by [submitPin] and [start].
     */
    private var authBlocked = false

    /** Consecutive link drops without reaching READY, used to back the retry off. */
    private var consecutiveDrops = 0

    private var seqCounter = 1
    private var chunkSize = 20
    private var retryDelayMs = 2_000L

    /** The PIN to present. An empty string is the correct value for a device with none set. */
    var pin: String = ""

    /** The address of the device to reconnect to; persisted by the activity. */
    var lastMac: String? = null

    // --- request write pump ---------------------------------------------------
    // Android allows one outstanding GATT operation, so frames are cut into MTU
    // sized slices and fed out one at a time, each waiting for its ack.
    private val outbox = ArrayDeque<ByteArray>()
    private var currentFrame: ByteArray? = null
    private var currentOffset = 0
    private var writeBusy = false
    private var writeToken = 0

    private val inflight = HashMap<Int, Reply>()

    private class Reply(val onDone: (Int, String) -> Unit) {
        val body = ByteArrayOutputStream(512)
        var total = -1
        var timeout: Runnable? = null
    }

    // --- push reassembly ------------------------------------------------------
    // Deliberately a single slot, mirroring the browser client. Each push gets a
    // fresh seq, so a message whose chunks were partly lost is simply abandoned
    // when the next one starts instead of corrupting a shared buffer.
    private var pushSeq = -1
    private var pushStatus = 0
    private val pushBuf = ByteArrayOutputStream(512)

    // --- lifecycle ------------------------------------------------------------

    fun start() {
        val a = adapter
        if (a == null) {
            emit(State.FAILED, "This device has no Bluetooth")
            return
        }
        if (!a.isEnabled) {
            emit(State.FAILED, "Bluetooth is switched off")
            return
        }
        manualDisconnect = false
        authBlocked = false
        consecutiveDrops = 0
        retryDelayMs = 2_000L

        // A remembered device is far quicker to reach than a fresh scan, and it
        // avoids showing the rider a picker on every reconnect.
        val mac = lastMac
        if (mac != null) {
            val device = try {
                a.getRemoteDevice(mac)
            } catch (e: IllegalArgumentException) {
                null
            }
            if (device != null) {
                connect(device)
                return
            }
        }
        scan()
    }

    fun stop() {
        manualDisconnect = true
        everReady = false
        cancelScan()
        main.removeCallbacksAndMessages(null)
        closeGatt()
        emit(State.IDLE, "Disconnected")
    }

    fun submitPin(value: String) {
        pin = value
        authBlocked = false
        consecutiveDrops = 0
        authenticate()
    }

    fun isReady(): Boolean = ready

    fun hasRememberedDevice(): Boolean = lastMac != null

    fun forgetDevice() {
        lastMac = null
    }

    // --- scanning -------------------------------------------------------------

    private var scanning = false

    private val scanTimeout = Runnable {
        if (scanning) {
            cancelScan()
            emit(State.FAILED, "No middleman found — is it powered and in range?")
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            // Binder thread: everything below mutates main-thread state, so route
            // it through the main looper before touching any of it.
            main.post { handleScanResult(result) }
        }

        override fun onScanFailed(errorCode: Int) {
            main.post {
                scanning = false
                emit(State.FAILED, "Scan failed (code $errorCode)")
            }
        }
    }

    private fun handleScanResult(result: ScanResult) {
        if (!scanning) return
        val device = result.device ?: return

        val name = try {
            result.scanRecord?.deviceName ?: device.name
        } catch (e: SecurityException) {
            null
        }
        val advertisesService =
            result.scanRecord?.serviceUuids?.any { it?.uuid == SERVICE } == true
        val looksRight = advertisesService || name?.contains("BBSHD", ignoreCase = true) == true

        if (!looksRight) return

        Log.i(TAG, "found ${device.address} name=$name")
        lastMac = device.address
        cancelScan()
        connect(device)
    }

    private fun scan() {
        val scanner = adapter?.bluetoothLeScanner
        if (scanner == null) {
            emit(State.FAILED, "Bluetooth LE is not available")
            return
        }
        cancelScan()
        emit(State.SCANNING, "Searching for the middleman…")
        scanning = true
        try {
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()
            // Unfiltered on purpose: the scan filter would drop a device that is
            // in the middle of booting and has not put the service UUID in its
            // advertisement yet, which is exactly when a rider is waiting.
            scanner.startScan(null, settings, scanCallback)
        } catch (e: SecurityException) {
            scanning = false
            emit(State.FAILED, "Bluetooth permission was denied")
            return
        }
        main.postDelayed(scanTimeout, SCAN_TIMEOUT_MS)
    }

    private fun cancelScan() {
        if (!scanning) return
        scanning = false
        main.removeCallbacks(scanTimeout)
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            Log.w(TAG, "stopScan denied", e)
        }
    }

    // --- connecting -----------------------------------------------------------

    private val connectTimeout = Runnable {
        if (!linked) {
            Log.w(TAG, "connect timed out")
            closeGatt()
            if (everReady) scheduleRetry() else scan()
        }
    }

    private fun connect(device: BluetoothDevice) {
        closeGatt()
        emit(State.CONNECTING, "Connecting…")
        gatt = try {
            device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } catch (e: SecurityException) {
            emit(State.FAILED, "Bluetooth permission was denied")
            return
        }
        if (gatt == null) {
            emit(State.FAILED, "Could not open a GATT link")
            return
        }
        main.postDelayed(connectTimeout, CONNECT_TIMEOUT_MS)
    }

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            main.post { handleConnectionState(g, status, newState) }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            main.post {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    chunkSize = (mtu - 3).coerceIn(20, MAX_CHUNK)
                }
                Log.i(TAG, "mtu=$mtu chunk=$chunkSize")
                discover(g)
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post { handleServices(g, status) }
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            main.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    emit(State.FAILED, "Could not subscribe to telemetry ($status)")
                    return@post
                }
                onLinkReady()
            }
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            main.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Log.w(TAG, "write failed status=$status")
                }
                writeBusy = false
                writeToken++ // invalidates the pending write-ack watchdog
                val frame = currentFrame
                if (frame != null && currentOffset >= frame.size) {
                    currentFrame = null
                    currentOffset = 0
                }
                pumpWrites()
            }
        }

        // API 33 delivers the value directly; overriding both means the framework
        // never routes through the deprecated accessor, and older devices still
        // work through the 4-argument form.
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            val copy = value.copyOf()
            main.post { handleNotification(copy) }
        }

        @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            val copy = characteristic.value?.copyOf() ?: return
            main.post { handleNotification(copy) }
        }
    }

    private fun handleConnectionState(g: BluetoothGatt, status: Int, newState: Int) {
        Log.i(TAG, "state change status=$status newState=$newState")
        if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
            if (!g.requestMtu(517)) {
                // No MTU exchange: fall back to the default and carry on.
                discover(g)
            } else {
                // Not every stack calls onMtuChanged even when requestMtu()
                // accepted, and without discovery the link is useless. 1.5 s is
                // long enough for a real exchange and short enough that a
                // missing callback costs one second, not a dead connection.
                // The identity check matters: this timer can outlive the
                // connection, and discovering on a closed gatt reports a
                // failure for a link that is already being replaced.
                main.postDelayed({ if (gatt === g && !linked) discover(g) }, 1_500)
            }
            return
        }
        if (newState == BluetoothProfile.STATE_DISCONNECTED) {
            closeGatt()
            if (manualDisconnect) {
                emit(State.IDLE, "Disconnected")
                return
            }

            // A rejected PIN cannot be retried into working: only the user can
            // supply a different one. Park here rather than cycling.
            if (authBlocked) {
                emit(State.IDLE, "PIN required — tap Connect and enter the PIN")
                return
            }

            consecutiveDrops++
            if (everReady) {
                scheduleRetry()
            } else if (consecutiveDrops < 3) {
                // The remembered address did not answer, so fall back to a scan
                // rather than retrying a device that may have been reflashed or
                // replaced. Bounded: a device that is simply off must not turn
                // into an endless scan loop.
                lastMac = null
                scan()
            } else {
                scheduleRetry()
            }
        }
    }

    private var discoveryRequested = false

    private fun discover(g: BluetoothGatt) {
        if (discoveryRequested) return
        discoveryRequested = true
        if (!g.discoverServices()) {
            discoveryRequested = false
            emit(State.FAILED, "Could not enumerate services")
        }
    }

    private fun handleServices(g: BluetoothGatt, status: Int) {
        if (status != BluetoothGatt.GATT_SUCCESS) {
            emit(State.FAILED, "Service discovery failed ($status)")
            return
        }

        val service = g.getService(SERVICE)
        if (service == null) {
            emit(State.FAILED, "Middleman service not found — wrong firmware?")
            g.disconnect()
            return
        }

        val request = service.getCharacteristic(CHAR_REQUEST)
        val response = service.getCharacteristic(CHAR_RESPONSE)
        if (request == null || response == null) {
            emit(State.FAILED, "Middleman characteristics not found")
            g.disconnect()
            return
        }
        requestChar = request
        responseChar = response

        if (!g.setCharacteristicNotification(response, true)) {
            emit(State.FAILED, "Could not enable notifications")
            return
        }

        val cccd = response.getDescriptor(CCCD)
        if (cccd == null) {
            // Nothing to subscribe to, but the characteristic may already be
            // notifying; try to use the link anyway.
            onLinkReady()
            return
        }
        // The descriptor has to carry the subscription value before it is
        // written. writeDescriptor() returns false outright when the value is
        // null, which is what "could not write the notification descriptor"
        // meant: the enable-notifications bit was never set on the descriptor.
        @Suppress("DEPRECATION")
        cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE

        @Suppress("DEPRECATION")
        val subscribed = g.writeDescriptor(cccd)
        if (!subscribed) {
            emit(State.FAILED, "Could not write the notification descriptor")
            return
        }
    }

    private fun onLinkReady() {
        linked = true
        main.removeCallbacks(connectTimeout)
        // The device accepts exactly one request before it is authenticated, so
        // this has to succeed before the link is considered usable.
        authenticate()
    }

    private fun scheduleRetry() {
        emit(State.CONNECTING, "Reconnecting in ${retryDelayMs / 1000} s…")
        main.postDelayed({
            if (!manualDisconnect) start()
        }, retryDelayMs)
        retryDelayMs = (retryDelayMs * 2).coerceAtMost(15_000L)
    }

    private fun closeGatt() {
        linked = false
        ready = false
        discoveryRequested = false
        main.removeCallbacks(connectTimeout)
        writeBusy = false
        currentFrame = null
        currentOffset = 0
        outbox.clear()
        pushSeq = -1
        pushBuf.reset()
        inflight.values.forEach { reply ->
            reply.timeout?.let { main.removeCallbacks(it) }
        }
        inflight.clear()
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: SecurityException) {
            Log.w(TAG, "close denied", e)
        }
        gatt = null
        requestChar = null
        responseChar = null
    }

    // --- requests -------------------------------------------------------------

    private fun authenticate() {
        val body = if (pin.isEmpty()) "{}" else JSONObject().put("pin", pin).toString()
        request(1 /* POST */, "/api/auth", body) { status, _ ->
            when {
                status == 200 -> {
                    ready = true
                    everReady = true
                    authBlocked = false
                    consecutiveDrops = 0
                    emit(State.READY, "Connected")
                }

                status == 401 -> {
                    // Never retry a rejected PIN: the firmware counts attempts
                    // and locks out after five.
                    pin = ""
                    ready = false
                    authBlocked = true
                    emit(State.IDLE, "PIN required")
                    listener.onPinRequired()
                }

                status == 429 -> emit(State.FAILED, "Too many PIN attempts — wait 30 s")

                status < 0 -> emit(State.FAILED, "The middleman did not answer")

                else -> emit(State.FAILED, "Bluetooth authentication failed ($status)")
            }
        }
    }

    /**
     * Issues an API request. The callback runs on the main thread with `-1` in
     * place of a status when the link is down or the device never answered, so
     * callers never have to special-case a dead transport.
     *
     * These are the only entry points the UI uses; `request` stays private so
     * nothing can bypass the link check or the inflight bookkeeping.
     */
    fun get(path: String, onDone: ((Int, String) -> Unit)? = null) {
        request(0, path, "") { status, body -> onDone?.invoke(status, body) }
    }

    fun post(path: String, body: String = "{}", onDone: ((Int, String) -> Unit)? = null) {
        request(1, path, body) { status, response -> onDone?.invoke(status, response) }
    }

    private fun request(method: Int, path: String, body: String, onDone: (Int, String) -> Unit) {
        val g = gatt
        if (g == null || !linked || requestChar == null) {
            onDone(-1, "")
            return
        }

        val seq = seqCounter
        seqCounter = if (seqCounter >= 0x7FFF) 1 else seqCounter + 1

        val pathBytes = path.toByteArray(Charsets.UTF_8)
        val bodyBytes = body.toByteArray(Charsets.UTF_8)
        val frame = ByteBuffer.allocate(8 + pathBytes.size + bodyBytes.size)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putShort(seq.toShort())
            .put(method.toByte())
            .put(0) // reserved
            .putShort(pathBytes.size.toShort())
            .putShort(bodyBytes.size.toShort())
            .put(pathBytes)
            .put(bodyBytes)
            .array()

        val reply = Reply(onDone)
        inflight[seq] = reply
        val timeout = Runnable {
            if (inflight.remove(seq) != null) {
                Log.w(TAG, "request $seq timed out")
                onDone(-1, "")
            }
        }
        reply.timeout = timeout
        main.postDelayed(timeout, REQUEST_TIMEOUT_MS)

        outbox.addLast(frame)
        pumpWrites()
    }

    private fun pumpWrites() {
        if (writeBusy) return
        val g = gatt ?: return
        val characteristic = requestChar ?: return

        val frame: ByteArray
        val pending = currentFrame
        if (pending == null) {
            val next = outbox.removeFirstOrNull() ?: return
            currentFrame = next
            currentOffset = 0
            frame = next
        } else {
            frame = pending
        }

        val end = minOf(currentOffset + chunkSize, frame.size)
        val slice = frame.copyOfRange(currentOffset, end)
        currentOffset = end

        writeBusy = true
        val token = ++writeToken
        main.postDelayed({
            if (writeBusy && token == writeToken) {
                Log.w(TAG, "no write ack; resetting the write queue")
                writeBusy = false
                currentFrame = null
                currentOffset = 0
                pumpWrites()
            }
        }, WRITE_ACK_TIMEOUT_MS)

        @Suppress("DEPRECATION")
        characteristic.value = slice
        @Suppress("DEPRECATION")
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT

        val accepted = try {
            @Suppress("DEPRECATION")
            g.writeCharacteristic(characteristic)
        } catch (e: SecurityException) {
            false
        }
        if (!accepted) {
            Log.w(TAG, "writeCharacteristic refused")
            writeBusy = false
            currentFrame = null
            currentOffset = 0
        }
    }

    // --- notifications --------------------------------------------------------

    private fun handleNotification(value: ByteArray) {
        if (value.size < 6) return

        val header = ByteBuffer.wrap(value).order(ByteOrder.LITTLE_ENDIAN)
        val seq = header.short.toInt() and 0xFFFF
        val status = header.short.toInt() and 0xFFFF
        val total = header.short.toInt() and 0xFFFF
        val payload = value.copyOfRange(6, value.size)

        if (seq >= PUSH_SEQ_BASE) {
            handlePush(seq, status, total, payload)
            return
        }

        val reply = inflight[seq] ?: return
        reply.body.write(payload)
        reply.total = total
        if (reply.body.size() < total) return

        inflight.remove(seq)
        reply.timeout?.let { main.removeCallbacks(it) }
        reply.onDone(status, String(reply.body.toByteArray(), Charsets.UTF_8))
    }

    private fun handlePush(seq: Int, status: Int, total: Int, payload: ByteArray) {
        if (pushSeq != seq) {
            pushSeq = seq
            pushStatus = status
            pushBuf.reset()
        }
        pushBuf.write(payload)
        if (pushBuf.size() < total) return

        val json = String(pushBuf.toByteArray(), Charsets.UTF_8)
        val kind = pushStatus
        pushSeq = -1
        pushBuf.reset()

        when (kind) {
            PUSH_TELEMETRY -> listener.onTelemetry(json)
            PUSH_EVENTS -> listener.onEvents(json)
            else -> Log.w(TAG, "unknown push kind $kind")
        }
    }

    private fun emit(state: State, detail: String) {
        listener.onState(state, detail)
    }
}
