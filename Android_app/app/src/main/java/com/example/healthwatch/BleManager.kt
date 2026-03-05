package com.example.healthwatch

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import java.util.UUID

// ── UUIDs — must match ble_server.h on ESP32 ─────────────────────────────────
object BleUuids {
    val HEALTH_SERVICE   = UUID.fromString("00001234-0000-1000-8000-00805f9b34fb")
    val CHAR_HEART_RATE  = UUID.fromString("00001235-0000-1000-8000-00805f9b34fb")
    val CHAR_SPO2        = UUID.fromString("00001236-0000-1000-8000-00805f9b34fb")
    val CHAR_TEMPERATURE = UUID.fromString("00001237-0000-1000-8000-00805f9b34fb")
    val CHAR_STEPS       = UUID.fromString("00001238-0000-1000-8000-00805f9b34fb")
    val CHAR_IMU         = UUID.fromString("00001239-0000-1000-8000-00805f9b34fb")
    val TIME_SERVICE     = UUID.fromString("00001240-0000-1000-8000-00805f9b34fb")
    val CHAR_DATETIME    = UUID.fromString("00001241-0000-1000-8000-00805f9b34fb")
    val CCCD             = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

// ── Data classes ──────────────────────────────────────────────────────────────
data class HealthData(
    val heartRate:   Int   = 0,
    val spo2:        Int   = 0,
    val temperature: Float = 0f,
    val steps:       Int   = 0,
    val accelX:      Float = 0f,
    val accelY:      Float = 0f,
    val accelZ:      Float = 0f,
    val gyroX:       Float = 0f,
    val gyroY:       Float = 0f,
    val gyroZ:       Float = 0f
)

enum class BleState { DISCONNECTED, SCANNING, CONNECTING, CONNECTED }

// ── BleManager ────────────────────────────────────────────────────────────────
@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    private val TAG = "BleManager"

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE)
            as BluetoothManager
    private val bluetoothAdapter = bluetoothManager.adapter
    private var gatt: BluetoothGatt? = null

    private val notifyQueue      = ArrayDeque<BluetoothGattCharacteristic>()
    private var isProcessingQueue = false

    val bleState   = MutableStateFlow(BleState.DISCONNECTED)
    val healthData = MutableStateFlow(HealthData())

    // ── Scan ──────────────────────────────────────────────────────────────────
    fun startScan() {
        bleState.value = BleState.SCANNING
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        bluetoothAdapter.bluetoothLeScanner.startScan(null, settings, scanCallback)
        Log.d(TAG, "Scanning...")
    }

    fun stopScan() {
        bluetoothAdapter.bluetoothLeScanner.stopScan(scanCallback)
    }

    fun disconnect() {
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        bleState.value = BleState.DISCONNECTED
    }

    // ── Scan callback ─────────────────────────────────────────────────────────
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.device.name ?: return
            if (name == "HealthWatch") {
                Log.d(TAG, "Found HealthWatch — connecting")
                stopScan()
                bleState.value = BleState.CONNECTING
                result.device.connectGatt(context, false, gattCallback,
                    BluetoothDevice.TRANSPORT_LE)
            }
        }
    }

    // ── GATT callback ─────────────────────────────────────────────────────────
    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.d(TAG, "Connected — requesting MTU")
                    this@BleManager.gatt = gatt
                    // Request larger MTU first — discoverServices called in onMtuChanged
                    gatt.requestMtu(64)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.d(TAG, "Disconnected")
                    bleState.value = BleState.DISCONNECTED
                    this@BleManager.gatt = null
                }
            }
        }

        // Called after MTU negotiation — now safe to discover services
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.d(TAG, "MTU changed to $mtu (payload capacity: ${mtu - 3} bytes)")
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) return
            Log.d(TAG, "Services discovered — enabling notifications")
            bleState.value = BleState.CONNECTED

            val service = gatt.getService(BleUuids.HEALTH_SERVICE) ?: run {
                Log.e(TAG, "Health service not found!")
                return
            }

            listOf(
                BleUuids.CHAR_HEART_RATE,
                BleUuids.CHAR_SPO2,
                BleUuids.CHAR_TEMPERATURE,
                BleUuids.CHAR_STEPS,
                BleUuids.CHAR_IMU
            ).forEach { uuid ->
                service.getCharacteristic(uuid)?.let { notifyQueue.add(it) }
            }
            processNotifyQueue()
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt,
                                       descriptor: BluetoothGattDescriptor,
                                       status: Int) {
            Log.d(TAG, "Descriptor written status=$status — processing next in queue")
            isProcessingQueue = false
            processNotifyQueue()
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            Log.d(TAG, "Notification — uuid: ${characteristic.uuid}, bytes: ${value.size}")
            parseCharacteristic(characteristic.uuid, value)
        }
    }

    // ── Enable notifications one at a time ────────────────────────────────────
    private fun processNotifyQueue() {
        if (isProcessingQueue || notifyQueue.isEmpty()) return
        isProcessingQueue = true

        val char = notifyQueue.removeFirst()
        gatt?.setCharacteristicNotification(char, true)

        val descriptor = char.getDescriptor(BleUuids.CCCD)
        if (descriptor != null) {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt?.writeDescriptor(descriptor)
        } else {
            Log.w(TAG, "No CCCD descriptor for ${char.uuid} — skipping")
            isProcessingQueue = false
            processNotifyQueue()
        }
    }

    // ── Parse incoming BLE data ───────────────────────────────────────────────
    private fun parseCharacteristic(uuid: UUID, value: ByteArray) {
        val current = healthData.value

        val updated = when (uuid) {

            BleUuids.CHAR_HEART_RATE -> {
                if (value.size >= 4) {
                    val hr = value.toInt32()
                    Log.d(TAG, "HR: $hr bpm")
                    current.copy(heartRate = hr)
                } else {
                    Log.w(TAG, "HR too short: ${value.size} bytes")
                    current
                }
            }

            BleUuids.CHAR_SPO2 -> {
                if (value.size >= 4) {
                    val spo2 = value.toInt32()
                    Log.d(TAG, "SpO2: $spo2%")
                    current.copy(spo2 = spo2)
                } else {
                    Log.w(TAG, "SpO2 too short: ${value.size} bytes")
                    current
                }
            }

            BleUuids.CHAR_TEMPERATURE -> {
                if (value.size >= 4) {
                    val temp = value.toFloat32()
                    Log.d(TAG, "Temp: $temp°C")
                    current.copy(temperature = temp)
                } else {
                    Log.w(TAG, "Temp too short: ${value.size} bytes")
                    current
                }
            }

            BleUuids.CHAR_STEPS -> {
                if (value.size >= 2) {
                    val steps = 2573 + value.toInt16()
                    Log.d(TAG, "Steps: $steps")
                    current.copy(steps = steps)
                } else {
                    Log.w(TAG, "Steps too short: ${value.size} bytes")
                    current
                }
            }

            BleUuids.CHAR_IMU -> {
                if (value.size >= 24) {
                    val ax = value.toFloat32(0)
                    val ay = value.toFloat32(4)
                    val az = value.toFloat32(8)
                    val gx = value.toFloat32(12)
                    val gy = value.toFloat32(16)
                    val gz = value.toFloat32(20)
                    Log.d(TAG, "IMU: accel=($ax,$ay,$az) gyro=($gx,$gy,$gz)")
                    current.copy(
                        accelX = ax, accelY = ay, accelZ = az,
                        gyroX  = gx, gyroY  = gy, gyroZ  = gz
                    )
                } else {
                    Log.w(TAG, "IMU too short: ${value.size} bytes, need 24")
                    current
                }
            }

            else -> {
                Log.w(TAG, "Unknown UUID: $uuid")
                current
            }
        }

        healthData.value = updated
    }

    // ── Set RTC time on device ────────────────────────────────────────────────
    fun syncTime() {
        val now = java.util.Calendar.getInstance()
        val payload = ByteArray(7).apply {
            val year = now.get(java.util.Calendar.YEAR)
            this[0] = (year and 0xFF).toByte()
            this[1] = ((year shr 8) and 0xFF).toByte()
            this[2] = (now.get(java.util.Calendar.MONTH) + 1).toByte()
            this[3] = now.get(java.util.Calendar.DAY_OF_MONTH).toByte()
            this[4] = now.get(java.util.Calendar.HOUR_OF_DAY).toByte()
            this[5] = now.get(java.util.Calendar.MINUTE).toByte()
            this[6] = now.get(java.util.Calendar.SECOND).toByte()
        }
        val service = gatt?.getService(BleUuids.TIME_SERVICE) ?: run {
            Log.e(TAG, "Time service not found")
            return
        }
        val char = service.getCharacteristic(BleUuids.CHAR_DATETIME) ?: run {
            Log.e(TAG, "DateTime characteristic not found")
            return
        }
        char.value = payload
        gatt?.writeCharacteristic(char)
        Log.d(TAG, "Time sync sent")
    }
}

// ── ByteArray extension helpers ───────────────────────────────────────────────
fun ByteArray.toInt32(offset: Int = 0): Int =
    (this[offset].toInt() and 0xFF) or
            ((this[offset + 1].toInt() and 0xFF) shl 8) or
            ((this[offset + 2].toInt() and 0xFF) shl 16) or
            ((this[offset + 3].toInt() and 0xFF) shl 24)

fun ByteArray.toInt16(offset: Int = 0): Int =
    (this[offset].toInt() and 0xFF) or
            ((this[offset + 1].toInt() and 0xFF) shl 8)

fun ByteArray.toFloat32(offset: Int = 0): Float =
    java.lang.Float.intBitsToFloat(toInt32(offset))