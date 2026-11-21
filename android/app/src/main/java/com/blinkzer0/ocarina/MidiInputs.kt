package com.blinkzer0.ocarina

import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.ParcelUuid
import android.widget.ArrayAdapter

internal class MidiFramer(private val receive: (Int, Int, Int) -> Unit) {
    private var status = 0
    private var remaining = 0
    private var first = 0
    private var collected = 0
    private var sysex = false

    fun send(bytes: ByteArray, offset: Int, count: Int) {
        for (index in offset until offset + count) {
            val byte = bytes[index].toInt() and 255
            when {
                byte >= 0xf8 -> continue
                byte >= 0xf0 -> {
                    sysex = byte == 0xf0
                    status = 0
                    collected = 0
                    remaining = when (byte) { 0xf1, 0xf3 -> 1; 0xf2 -> 2; else -> 0 }
                }
                byte >= 0x80 -> {
                    sysex = false
                    status = byte
                    collected = 0
                    remaining = 0
                }
                sysex -> continue
                remaining > 0 -> remaining--
                status != 0 -> {
                    val size = if ((status and 0xf0) in 0xc0..0xdf) 1 else 2
                    if (size == 1) receive(status, byte, 0)
                    else if (collected == 0) {
                        first = byte
                        collected = 1
                    } else {
                        receive(status, first, byte)
                        collected = 0
                    }
                }
            }
        }
    }

    fun reset() {
        status = 0
        remaining = 0
        collected = 0
        sysex = false
    }
}

@Suppress("DEPRECATION", "MissingPermission")
internal class MidiInputs(
    private val activity: Activity,
    private val handler: Handler,
    private val event: (Int, Int, Int, Int) -> Unit,
    private val release: (Int) -> Unit,
    private val notice: (String) -> Unit
) {
    private data class Input(val source: Int, val port: MidiOutputPort)
    private data class Connection(val device: MidiDevice, val inputs: MutableList<Input> = mutableListOf())
    private val manager = activity.getSystemService(MidiManager::class.java)
    private val open = mutableMapOf<Int, Connection>()
    private val pending = mutableMapOf<Int, MidiDeviceInfo>()
    private var generation = 0
    private var source = 16
    private var started = false
    private var stopScan: (() -> Unit)? = null
    private val bleMidi = ParcelUuid.fromString("03b80e5a-ede8-4b33-a751-6ce34ec4c700")
    private val callback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) = connect(info)
        override fun onDeviceRemoved(info: MidiDeviceInfo) {
            pending.remove(info.id)
            close(info.id)
            report()
        }
    }

    fun start() {
        if (started || manager == null) return
        started = true
        manager.registerDeviceCallback(callback, handler)
        manager.devices.forEach(::connect)
        report()
    }

    fun stop() {
        stopScan?.invoke()
        if (!started) return
        started = false
        generation++
        manager?.unregisterDeviceCallback(callback)
        pending.clear()
        open.keys.toList().forEach(::close)
    }

    private fun connect(info: MidiDeviceInfo) {
        if (!started || info.outputPortCount == 0 || info.id in open || info.id in pending) return
        pending[info.id] = info
        val session = generation
        manager?.openDevice(info, { device ->
            val current = pending[info.id] === info && generation == session
            if (current) pending.remove(info.id)
            if (current && started && device != null) attach(device)
            else runCatching { device?.close() }
        }, handler)
    }

    private fun attach(device: MidiDevice) {
        val id = device.info.id
        if (id in open) {
            runCatching { device.close() }
            return
        }
        val connection = Connection(device)
        open[id] = connection
        for (index in 0 until device.info.outputPortCount) {
            val port = device.openOutputPort(index) ?: continue
            val input = Input(source++, port)
            if (source > 60000) source = 16
            connection.inputs.add(input)
            val parser = MidiFramer { status, first, second ->
                handler.post { if (open[id] === connection) event(input.source, status, first, second) }
            }
            port.connect(object : MidiReceiver() {
                override fun onSend(bytes: ByteArray, offset: Int, count: Int, timestamp: Long) =
                    synchronized(parser) { parser.send(bytes, offset, count) }
                override fun onFlush() {
                    synchronized(parser) { parser.reset() }
                    handler.post { if (open[id] === connection) release(input.source) }
                }
            })
        }
        if (connection.inputs.isEmpty()) close(id)
        report()
    }

    private fun close(id: Int) {
        val connection = open.remove(id) ?: return
        for (input in connection.inputs) {
            release(input.source)
            runCatching { input.port.close() }
        }
        runCatching { connection.device.close() }
    }

    fun report() {
        notice(when {
            manager == null -> "MIDI is unavailable on this device."
            open.isEmpty() -> "Connect a USB controller or find a Bluetooth MIDI device."
            else -> "${open.size} MIDI device${if (open.size == 1) "" else "s"} connected."
        })
    }

    fun discover() {
        if (!started || manager == null) return
        stopScan?.invoke()
        val scanner = runCatching {
            activity.getSystemService(BluetoothManager::class.java)?.adapter?.bluetoothLeScanner
        }.getOrNull()
        if (scanner == null) {
            notice("Turn on Bluetooth to find a MIDI controller.")
            return
        }
        scan(scanner)
    }

    private fun scan(scanner: BluetoothLeScanner) {
        val devices = linkedMapOf<String, BluetoothDevice>()
        val list = ArrayAdapter<String>(activity, android.R.layout.simple_list_item_1)
        val dialog = AlertDialog.Builder(activity).setTitle("Bluetooth MIDI").setAdapter(list) { _, index ->
            devices.values.elementAtOrNull(index)?.let(::connectBluetooth)
        }.setNegativeButton("Close", null).create()
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                handler.post {
                    val device = result.device
                    if (dialog.isShowing && devices.putIfAbsent(device.address, device) == null)
                        list.add(result.scanRecord?.deviceName ?: runCatching { device.name }.getOrNull() ?: device.address)
                }
            }
            override fun onScanFailed(errorCode: Int) {
                handler.post { notice("Bluetooth MIDI scan failed ($errorCode)."); dialog.dismiss() }
            }
        }
        val timeout = Runnable {
            runCatching { scanner.stopScan(callback) }
            dialog.setTitle(if (devices.isEmpty()) "No Bluetooth MIDI devices found" else "Choose a MIDI controller")
        }
        stopScan = { dialog.dismiss() }
        dialog.setOnDismissListener {
            handler.removeCallbacks(timeout)
            runCatching { scanner.stopScan(callback) }
            stopScan = null
        }
        dialog.show()
        try {
            scanner.startScan(listOf(ScanFilter.Builder().setServiceUuid(bleMidi).build()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback)
            handler.postDelayed(timeout, 8000)
        } catch (error: SecurityException) {
            notice("Bluetooth permission is required to find MIDI controllers.")
            dialog.dismiss()
        }
    }

    private fun connectBluetooth(bluetooth: BluetoothDevice) {
        val session = generation
        runCatching {
            manager?.openBluetoothDevice(bluetooth, { device ->
                if (device != null && started && generation == session) attach(device)
                else {
                    runCatching { device?.close() }
                    if (started && generation == session) notice("Could not connect to the Bluetooth MIDI controller.")
                }
            }, handler)
        }.onFailure { notice("Could not open the Bluetooth MIDI controller.") }
    }
}
