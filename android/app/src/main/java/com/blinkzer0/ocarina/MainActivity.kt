package com.blinkzer0.ocarina

import android.app.Activity
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.util.AtomicFile
import android.view.WindowInsets
import android.webkit.JavascriptInterface
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream

class MainActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var web: WebView
    private var audio = 0L
    private var ready = false
    private var audioStatus = "Opening audio"
    private var diagnostic = ""
    private val values = mutableMapOf<Int, Float>()
    private val controlCount by lazy { parameterCount() }
    private var pendingExport: String? = null
    private var pendingFileEvent: Pair<String, Any>? = null
    private var benchmarking = false
    private var benchmarkXruns = 0
    private var testStartFrames = 0L
    private val benchmarkNotes = 48..63
    private val debug get() = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0

    private val meterPoll = object : Runnable {
        override fun run() {
            publish()
            if (audio != 0L) handler.postDelayed(this, 50)
        }
    }
    private val finishTest = Runnable {
        if (audio != 0L) {
            sendNote(audio, false, 69, TEST_SOURCE)
            val frames = audibleFrames(audio) - testStartFrames
            diagnostic = "A4 test rendered $frames non-silent frames"
            Log.i("OcarinaAudio", "$audioStatus | $diagnostic")
            publish()
        }
    }
    private val finishBenchmark = Runnable {
        if (audio != 0L) {
            diagnostic = endProfile(audio, benchmarkXruns)
            for (note in benchmarkNotes) sendNote(audio, false, note, BENCHMARK_SOURCE)
            benchmarking = false
            val savedVoices = if (values[VOICE_MODE] == 0f) 0 else (values[VOICE_LIMIT] ?: 16f).toInt()
            setPerformance(audio, (values[MODEL] ?: 0f).toInt(), savedVoices)
            Log.i("OcarinaAudio", "$audioStatus | $diagnostic")
            publish()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        pendingExport = savedInstanceState?.getString("pendingExport")
        savedInstanceState?.getFloatArray("instrument")?.forEachIndexed { index, value ->
            if (value.isFinite()) values[index] = value
        }
        WebView.setWebContentsDebuggingEnabled(debug)
        web = WebView(this).apply {
            setBackgroundColor(Color.rgb(10, 16, 27))
            settings.javaScriptEnabled = true
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            addJavascriptInterface(InstrumentBridge(), "AndroidNative")
            webViewClient = object : WebViewClient() {
                override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest) = true

                override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse {
                    val url = request.url
                    val path = url.path
                    val mime = assetsByPath[path]
                    if (url.scheme != "https" || url.host != ASSET_HOST || path == null || mime == null)
                        return WebResourceResponse("text/plain", "UTF-8", null)
                    return WebResourceResponse(mime, if (mime == "image/webp") null else "UTF-8", assets.open(path.removePrefix("/")))
                }
            }
        }
        setContentView(FrameLayout(this).apply {
            setBackgroundColor(Color.rgb(10, 16, 27))
            addView(web)
            setOnApplyWindowInsetsListener { view, insets ->
                if (Build.VERSION.SDK_INT >= 30) {
                    val safe = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                    view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
                }
                insets
            }
        })
        web.loadUrl("https://$ASSET_HOST/index.html")
    }

    override fun onStart() {
        super.onStart()
        try {
            audio = openAudio()
            for ((index, value) in values) setParameter(audio, index, value)
            audioStatus = "Audio ready · " + audioDescription(audio)
            handler.post(meterPoll)
        } catch (error: IllegalStateException) {
            audioStatus = "Audio unavailable (" + error.message + ")"
            Log.e("OcarinaAudio", audioStatus, error)
            publish()
        }
    }

    override fun onStop() {
        handler.removeCallbacks(meterPoll)
        handler.removeCallbacks(finishTest)
        handler.removeCallbacks(finishBenchmark)
        benchmarking = false
        if (audio != 0L) {
            closeAudio(audio)
            audio = 0L
        }
        super.onStop()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putFloatArray("instrument", FloatArray(controlCount) { values[it] ?: Float.NaN })
        outState.putString("pendingExport", pendingExport)
        super.onSaveInstanceState(outState)
    }

    override fun onDestroy() {
        ready = false
        handler.removeCallbacksAndMessages(null)
        web.removeJavascriptInterface("AndroidNative")
        web.destroy()
        super.onDestroy()
    }

    private inner class InstrumentBridge {
        @JavascriptInterface
        fun message(json: String) {
            if (json.length > MAX_PRESET_BYTES + 4096) return
            val event = runCatching { JSONObject(json) }.getOrNull() ?: return
            handler.post { if (!isDestroyed) receive(event) }
        }
    }

    private fun receive(event: JSONObject) {
        when (event.optString("type")) {
            "ready" -> {
                ready = true
                publish(includeControls = true)
                sendLibrary()
                pendingFileEvent?.let { sendFileEvent(it.first, it.second) }
                pendingFileEvent = null
            }
            "listPresets" -> sendLibrary()
            "importPreset" -> chooseFile(export = false)
            "savePreset", "exportPreset" -> {
                val id = event.optString("id")
                val json = event.optString("json")
                if (!PRESET_ID.matches(id) || json.isEmpty() || json.toByteArray(Charsets.UTF_8).size > MAX_PRESET_BYTES) {
                    sendFileEvent("fileStatus", "Invalid preset data.")
                    return
                }
                if (event.optString("type") == "exportPreset") {
                    pendingExport = json
                    chooseFile(export = true, name = event.optString("name"))
                } else {
                    savePreset(id, json)
                }
            }
            "patch" -> {
                val packet = event.optJSONArray("values") ?: return
                if (packet.length() != controlCount) return
                val snapshot = FloatArray(controlCount) { packet.optDouble(it, Double.NaN).toFloat() }
                if (snapshot.any { !it.isFinite() }) return
                applyPatch(snapshot)
            }
            "parameter" -> {
                val index = event.optInt("index", -1)
                val value = event.optDouble("value", Double.NaN).toFloat()
                if (index !in 0 until controlCount || !value.isFinite()) return
                values[index] = value
                if (audio != 0L && (!benchmarking || index !in MODEL..VOICE_LIMIT))
                    setParameter(audio, index, value)
            }
            "note" -> if (audio != 0L && !benchmarking) {
                sendNote(audio, event.optBoolean("on"), event.optInt("key", -1), KEYBOARD_SOURCE)
            }
            "release" -> if (audio != 0L) releaseKeyboard(audio, KEYBOARD_SOURCE)
            "test" -> if (debug && audio != 0L && !benchmarking) {
                handler.removeCallbacks(finishTest)
                testStartFrames = audibleFrames(audio)
                sendNote(audio, true, 69, TEST_SOURCE)
                handler.postDelayed(finishTest, 500)
            }
            "profile" -> if (debug && audio != 0L && !benchmarking) {
                benchmarking = true
                handler.removeCallbacks(finishTest)
                releaseKeyboard(audio, KEYBOARD_SOURCE)
                sendNote(audio, false, 69, TEST_SOURCE)
                setPerformance(audio, (values[MODEL] ?: 0f).toInt(), 16)
                benchmarkXruns = beginProfile(audio)
                for (note in benchmarkNotes) sendNote(audio, true, note, BENCHMARK_SOURCE)
                diagnostic = "Profiling 16 voices…"
                handler.postDelayed(finishBenchmark, 4000)
            }
        }
    }

    private fun applyPatch(snapshot: FloatArray) {
        if (benchmarking) {
            handler.removeCallbacks(finishBenchmark)
            if (audio != 0L) {
                endProfile(audio, benchmarkXruns)
                releaseKeyboard(audio, BENCHMARK_SOURCE)
            }
            benchmarking = false
        }
        if (audio != 0L && !setPatch(audio, snapshot)) {
            publish(includeControls = true)
            sendFileEvent("fileStatus", "Audio controls are busy. Select the preset again.")
            return
        }
        snapshot.forEachIndexed { index, value -> values[index] = value }
    }

    private fun readPreset(stream: InputStream): String {
        val bytes = ByteArray(MAX_PRESET_BYTES + 1)
        var count = 0
        while (count < bytes.size) {
            val next = stream.read(bytes, count, bytes.size - count)
            if (next < 0) break
            count += next
        }
        require(count in 1..MAX_PRESET_BYTES) { "Preset files must be 128 KB or smaller." }
        return String(bytes, 0, count, Charsets.UTF_8)
    }

    private val presetDirectory: File get() = File(filesDir, "presets")

    private fun savePreset(id: String, json: String) {
        try {
            presetDirectory.mkdirs()
            val file = AtomicFile(File(presetDirectory, "patch-$id.orpreset"))
            val stream = file.startWrite()
            try {
                stream.write(json.toByteArray(Charsets.UTF_8))
                file.finishWrite(stream)
            } catch (error: Exception) {
                file.failWrite(stream)
                throw error
            }
            sendLibrary()
        } catch (error: Exception) {
            Log.w("OcarinaPreset", "Could not save preset", error)
            sendFileEvent("fileStatus", "Could not save in the Ocarina preset folder.")
        }
    }

    private fun sendLibrary() {
        val library = JSONArray()
        for (entry in presetDirectory.listFiles()?.sortedBy { it.name }.orEmpty()) {
            if (entry.isFile && entry.extension == "orpreset" && entry.length() in 1..MAX_PRESET_BYTES.toLong()) {
                runCatching { AtomicFile(entry).openRead().use(::readPreset) }.getOrNull()?.let { library.put(it) }
            }
        }
        sendFileEvent("library", library)
    }

    private fun sendFileEvent(key: String, value: Any) {
        if (ready) web.evaluateJavascript("window.ocarinaReceive?.(" + JSONObject().put(key, value) + ")", null)
        else if (key != "library") pendingFileEvent = key to value
    }

    @Suppress("DEPRECATION")
    private fun chooseFile(export: Boolean, name: String = "") {
        val intent = Intent(if (export) Intent.ACTION_CREATE_DOCUMENT else Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = if (export) "application/octet-stream" else "*/*"
            if (export) {
                val title = name.replace(Regex("[^\\p{L}\\p{N} ._-]"), "-").trim().take(80).ifEmpty { "Ocarina of Resonance" }
                putExtra(Intent.EXTRA_TITLE, "$title.orpreset")
            }
        }
        try { startActivityForResult(intent, if (export) EXPORT_PRESET else IMPORT_PRESET) }
        catch (error: android.content.ActivityNotFoundException) { sendFileEvent("fileStatus", "System file picker is unavailable.") }
    }

    @Deprecated("Android system document picker callback")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val json = pendingExport
        if (requestCode == EXPORT_PRESET) pendingExport = null
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        try {
            when (requestCode) {
                IMPORT_PRESET -> {
                    val preset = contentResolver.openInputStream(uri)?.use(::readPreset) ?: error("Could not open preset file.")
                    sendFileEvent("presetFile", preset)
                }
                EXPORT_PRESET -> if (json != null) {
                    val stream = contentResolver.openOutputStream(uri, "wt") ?: error("Could not open preset file.")
                    stream.use { it.write(json.toByteArray(Charsets.UTF_8)) }
                    sendFileEvent("fileStatus", "Preset exported.")
                }
            }
        } catch (error: Exception) {
            sendFileEvent("fileStatus", error.message ?: "Preset file operation failed.")
        }
    }

    private fun publish(includeControls: Boolean = false) {
        if (!ready) return
        val stats = if (audio != 0L) telemetry(audio) else floatArrayOf(0f, 0f, -1f)
        val state = JSONObject()
            .put("peak", stats[0].toDouble()).put("voices", stats[1].toInt()).put("note", stats[2].toInt())
            .put("status", audioStatus).put("debug", debug).put("diagnostic", diagnostic)
        if (includeControls && values.isNotEmpty()) {
            val controls = JSONArray()
            for ((index, value) in values) controls.put(index, value.toDouble())
            state.put("params", controls)
        }
        web.evaluateJavascript("window.ocarinaReceive?.($state)", null)
    }

    private external fun openAudio(): Long
    private external fun closeAudio(handle: Long)
    private external fun sendNote(handle: Long, on: Boolean, key: Int, source: Int)
    private external fun releaseKeyboard(handle: Long, source: Int)
    private external fun setParameter(handle: Long, index: Int, value: Float)
    private external fun parameterCount(): Int
    private external fun setPatch(handle: Long, values: FloatArray): Boolean
    private external fun telemetry(handle: Long): FloatArray
    private external fun setPerformance(handle: Long, model: Int, voices: Int)
    private external fun beginProfile(handle: Long): Int
    private external fun endProfile(handle: Long, before: Int): String
    private external fun audioDescription(handle: Long): String
    private external fun audibleFrames(handle: Long): Long

    companion object {
        private const val ASSET_HOST = "appassets.androidplatform.net"
        private const val MAX_PRESET_BYTES = 128 * 1024
        private const val IMPORT_PRESET = 21
        private const val EXPORT_PRESET = 22
        private val PRESET_ID = Regex("[a-z0-9][a-z0-9-]{0,95}")
        private const val MODEL = 13
        private const val VOICE_MODE = 14
        private const val VOICE_LIMIT = 15
        private const val KEYBOARD_SOURCE = 2
        private const val TEST_SOURCE = 100
        private const val BENCHMARK_SOURCE = 200
        private val assetsByPath = mapOf(
            "/index.html" to "text/html",
            "/assets/main.js" to "text/javascript",
            "/assets/presets.js" to "text/javascript",
            "/assets/style.css" to "text/css",
            "/assets/parameters.json" to "application/json",
            "/assets/ocarina.webp" to "image/webp",
        )
        init { System.loadLibrary("ocarina_android") }
    }
}
