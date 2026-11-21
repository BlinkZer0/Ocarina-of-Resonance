package com.blinkzer0.ocarina

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private var audio = 0L
    private lateinit var status: TextView
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
                if (audio != 0L) {
                    handler.removeCallbacks(finishTest)
                    testStartFrames = audibleFrames(audio)
                    sendNote(audio, true, 69, 100)
                    handler.postDelayed(finishTest, 500)
                }
            }
        })
        setContentView(column)
    }

    override fun onStart() {
        super.onStart()
        try {
            audio = openAudio()
            status.text = audioDescription(audio)
        } catch (error: IllegalStateException) {
            status.text = "Audio unavailable (${error.message})"
            Log.e("OcarinaAudio", "Cannot open output", error)
        }
    }

    override fun onStop() {
        handler.removeCallbacks(finishTest)
        if (audio != 0L) {
            closeAudio(audio)
            audio = 0L
        }
        super.onStop()
    }

    private external fun openAudio(): Long
    private external fun closeAudio(handle: Long)
    private external fun sendNote(handle: Long, on: Boolean, key: Int, source: Int)
    private external fun audioDescription(handle: Long): String
    private external fun audibleFrames(handle: Long): Long

    companion object {
        init { System.loadLibrary("ocarina_android") }
    }
}
