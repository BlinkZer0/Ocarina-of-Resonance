package com.blinkzer0.ocarina

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView

class MainActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private var audio = 0L
    private lateinit var status: TextView
    private lateinit var models: Spinner
    private lateinit var voices: Spinner
    private var benchmarking = false
    private var benchmarkXruns = 0
    private val benchmarkNotes = (48..63)
    private val finishBenchmark = Runnable {
        if (audio != 0L) {
            val report = endProfile(audio, benchmarkXruns)
            for (note in benchmarkNotes) sendNote(audio, false, note, 200)
            benchmarking = false
            applyPerformance()
            status.text = "${audioDescription(audio)}\n$report"
            Log.i("OcarinaAudio", "${audioDescription(audio)} | $report")
        }
    }
    private val finishTest = Runnable {
        if (audio != 0L) {
            sendNote(audio, false, 69, 100)
            val frames = audibleFrames(audio) - testStartFrames
            status.text = "${audioDescription(audio)}\nTest tone rendered $frames frames"
            Log.i("OcarinaAudio", "${audioDescription(audio)} | A4 test rendered $frames non-silent frames")
        }
    }
    private var testStartFrames = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val padding = (24 * resources.displayMetrics.density).toInt()
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(padding, padding, padding, padding)
            setBackgroundColor(Color.rgb(12, 22, 41))
        }
        column.addView(TextView(this).apply {
            text = "Ocarina of Resonance"
            textSize = 27f
            gravity = Gravity.CENTER
            setTextColor(Color.rgb(245, 207, 134))
        })
        status = TextView(this).apply {
            textSize = 16f
            gravity = Gravity.CENTER
            setPadding(0, padding / 2, 0, padding / 2)
        }
        column.addView(status)
        val controls = LinearLayout(this).apply { gravity = Gravity.CENTER }
        models = addSelector(controls, "Sound model", listOf("Ceramic Authentic", "Retro Fantasy", "Crystal Resonator", "Electronic Hybrid"), 0)
        voices = addSelector(controls, "Polyphony", listOf("Mono legato") + (1..16).map { "$it voices" }, 16)
        column.addView(controls, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        val keys = LinearLayout(this).apply { gravity = Gravity.CENTER }
        for ((name, note) in listOf("C4" to 60, "E4" to 64, "G4" to 67, "A4" to 69)) {
            keys.addView(Button(this).apply {
                text = name
                minWidth = 0
                setOnTouchListener { view, event ->
                    when (event.actionMasked) {
                        MotionEvent.ACTION_DOWN -> if (audio != 0L) sendNote(audio, true, note, note)
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                            if (audio != 0L) sendNote(audio, false, note, note)
                            if (event.actionMasked == MotionEvent.ACTION_UP) view.performClick()
                        }
                    }
                    true
                }
            }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        }
        column.addView(keys, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        column.addView(Button(this).apply {
            text = "Play A4 test tone"
            setOnClickListener {
                if (audio != 0L && !benchmarking) {
                    handler.removeCallbacks(finishTest)
                    testStartFrames = audibleFrames(audio)
                    sendNote(audio, true, 69, 100)
                    handler.postDelayed(finishTest, 500)
                }
            }
        })
        if ((applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            column.addView(Button(this).apply {
                text = "Profile 16 voices"
                setOnClickListener {
                    if (audio != 0L && !benchmarking) {
                        benchmarking = true
                        setPerformance(audio, models.selectedItemPosition, 16)
                        benchmarkXruns = beginProfile(audio)
                        for (note in benchmarkNotes) sendNote(audio, true, note, 200)
                        status.text = "Profiling 16 voices…"
                        handler.postDelayed(finishBenchmark, 4000)
                    }
                }
            })
        }
        setContentView(column)
    }

    private fun addSelector(parent: LinearLayout, label: String, choices: List<String>, initial: Int): Spinner {
        return Spinner(this).apply {
            contentDescription = label
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, choices)
            setSelection(initial)
            onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) = applyPerformance()
                override fun onNothingSelected(parent: AdapterView<*>?) = Unit
            }
            parent.addView(this, LinearLayout.LayoutParams(0, (48 * resources.displayMetrics.density).toInt(), 1f))
        }
    }

    private fun applyPerformance() {
        if (audio != 0L && !benchmarking)
            setPerformance(audio, models.selectedItemPosition, voices.selectedItemPosition)
    }

    override fun onStart() {
        super.onStart()
        try {
            audio = openAudio()
            applyPerformance()
            status.text = audioDescription(audio)
        } catch (error: IllegalStateException) {
            status.text = "Audio unavailable (${error.message})"
            Log.e("OcarinaAudio", "Cannot open output", error)
        }
    }

    override fun onStop() {
        handler.removeCallbacks(finishTest)
        handler.removeCallbacks(finishBenchmark)
        benchmarking = false
        if (audio != 0L) {
            closeAudio(audio)
            audio = 0L
        }
        super.onStop()
    }

    private external fun openAudio(): Long
    private external fun closeAudio(handle: Long)
    private external fun sendNote(handle: Long, on: Boolean, key: Int, source: Int)
    private external fun setPerformance(handle: Long, model: Int, voices: Int)
    private external fun beginProfile(handle: Long): Int
    private external fun endProfile(handle: Long, before: Int): String
    private external fun audioDescription(handle: Long): String
    private external fun audibleFrames(handle: Long): Long

    companion object {
        init { System.loadLibrary("ocarina_android") }
    }
}
