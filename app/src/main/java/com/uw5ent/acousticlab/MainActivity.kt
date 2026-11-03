package com.uw5ent.acousticlab

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.random.Random

class MainActivity : AppCompatActivity() {

    init { System.loadLibrary("acousticlab") }

    external fun getDspStatus(): String
    external fun startAudio(): Boolean
    external fun stopAudio()
    external fun getFftData(outArray: FloatArray)
    external fun setMixRatio(ratio: Float)
    external fun setFilterEnabled(enabled: Boolean)
    external fun startLearning() // НОВЫЙ
    external fun setNoiseReduction(enabled: Boolean) // НОВЫЙ

    private val fftBuffer = FloatArray(64)
    private val handler = Handler(Looper.getMainLooper())
    private var spectrogramView: SpectrogramView? = null
    private var logFile: File? = null
    private val ANOMALY_THRESHOLD = 85.0f

    private val updateRunnable = object : Runnable {
        override fun run() {
            getFftData(fftBuffer)
            spectrogramView?.updateFft(fftBuffer)
            checkForAnomalies()
            handler.postDelayed(this, 33) 
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        logFile = File(getExternalFilesDir(null), "dsp_anomalies_log.json")

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 1)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(30, 30, 30, 30)
        }

        val textView = TextView(this).apply { textSize = 16f; text = getDspStatus() }

        val mixerSlider = SeekBar(this).apply {
            max = 100; progress = 50 
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, f: Boolean) { setMixRatio(p / 100.0f) }
                override fun onStartTrackingTouch(sb: SeekBar?) {}
                override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }

        val filterCheckBox = CheckBox(this).apply {
            text = "DSP-фильтры (300-3400 Гц)"
            isChecked = true 
            setOnCheckedChangeListener { _, isChecked -> setFilterEnabled(isChecked) }
        }

        // КНОПКА ЗАХВАТА ШУМА
        val btnLearn = Button(this).apply {
            text = "1. Захватить шум фона (молчите 2 сек)"
            setOnClickListener {
                startLearning()
                Toast.makeText(context, "Слушаю фон...", Toast.LENGTH_SHORT).show()
            }
        }

        // ГАЛОЧКА NOISE REDUCTION
        val noiseReductionCheckBox = CheckBox(this).apply {
            text = "2. Включить Noise Reduction (GoldWave)"
            isChecked = false
            setOnCheckedChangeListener { _, isChecked -> setNoiseReduction(isChecked) }
        }

        spectrogramView = SpectrogramView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 400)
        }

        val btnStart = Button(this).apply {
            text = "Включить Микшер"
            setOnClickListener { text = if (startAudio()) "Микшер работает!" else "Ошибка" }
        }

        val btnStop = Button(this).apply {
            text = "Остановить"
            setOnClickListener { stopAudio(); text = "Остановлено" }
        }

        layout.addView(textView)
        layout.addView(TextView(this).apply { text = "Баланс (Шум <---> Микрофон):" })
        layout.addView(mixerSlider)
        layout.addView(filterCheckBox)
        layout.addView(btnLearn)
        layout.addView(noiseReductionCheckBox)
        layout.addView(spectrogramView)
        layout.addView(btnStart)
        layout.addView(btnStop)
        
        setContentView(layout)
    }

    private var lastLogTime = 0L
    private fun checkForAnomalies() {
        var maxIdx = 0; var maxMag = 0.0f
        for (i in fftBuffer.indices) {
            if (fftBuffer[i] > maxMag) { maxMag = fftBuffer[i]; maxIdx = i }
        }
        val currentTime = System.currentTimeMillis()
        if (maxMag > ANOMALY_THRESHOLD && currentTime - lastLogTime > 1000) {
            lastLogTime = currentTime
            val freqHz = maxIdx * (24000.0f / 64.0f)
            val rnd = Random.nextInt(1000, 9999)
            val ts = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault()).format(Date(currentTime))
            val json = """{"ts": "$ts", "random": $rnd, "peak_freq_hz": ${String.format("%.1f", freqHz)}, "magnitude": ${String.format("%.1f", maxMag)}}"""
            try {
                logFile?.appendText(json + "\n")
                runOnUiThread { Toast.makeText(this, "Аномалия! ${freqHz.toInt()} Гц", Toast.LENGTH_SHORT).show() }
            } catch (e: Exception) { e.printStackTrace() }
        }
    }

    override fun onResume() { super.onResume(); handler.post(updateRunnable) }
    override fun onPause() { super.onPause(); handler.removeCallbacks(updateRunnable) }
}

class SpectrogramView(context: Context) : android.view.View(context) {
    private var fftData = FloatArray(64)
    private val paint = Paint().apply { color = Color.GREEN; style = Paint.Style.FILL }
    fun updateFft(newData: FloatArray) { this.fftData = newData; invalidate() }
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(Color.BLACK)
        val barWidth = width.toFloat() / fftData.size
        for (i in fftData.indices) {
            val barHeight = (fftData[i] / 100f) * height
            canvas.drawRect(i * barWidth, height - barHeight, (i + 1) * barWidth - 2f, height.toFloat(), paint)
        }
    }
}
