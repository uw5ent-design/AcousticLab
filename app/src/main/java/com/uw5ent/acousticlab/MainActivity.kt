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

class MainActivity : AppCompatActivity() {

    init { System.loadLibrary("acousticlab") }

    external fun getDspStatus(): String
    external fun startAudio(): Boolean
    external fun stopAudio()
    external fun getFftData(outArray: FloatArray)
    external fun setNoiseVolume(vol: Float)
    external fun setMicVolume(vol: Float)
    external fun setFilterEnabled(enabled: Boolean)
    external fun startLearning()
    external fun setNoiseReduction(enabled: Boolean)
    external fun setScale(scale: Float)
    external fun setThreshold(th: Float)
    external fun startRecording()
    external fun stopRecording(path: String)

    private val fftBuffer = FloatArray(64)
    private val handler = Handler(Looper.getMainLooper())
    private var spectrogramView: SpectrogramView? = null
    private var recordPath: String = ""
    private var isRecording = false // Состояние записи

    private val updateRunnable = object : Runnable {
        override fun run() {
            getFftData(fftBuffer)
            spectrogramView?.updateFft(fftBuffer)
            handler.postDelayed(this, 33) 
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        recordPath = File(getExternalFilesDir(null), "record.raw").absolutePath

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 1)
        }

        val scrollView = ScrollView(this)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(30, 30, 30, 30)
        }

        val textView = TextView(this).apply { textSize = 16f; text = getDspStatus() }

        // --- ГРОМКОСТЬ ШУМА ---
        val noiseVolText = TextView(this).apply { text = "Громкость Шума: 50%" }
        val noiseVolSlider = SeekBar(this).apply {
            max = 100; progress = 50 
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, f: Boolean) { 
                    setNoiseVolume(p / 100.0f); noiseVolText.text = "Громкость Шума: $p%" 
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}; override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }

        // --- ВКЛ / ВЫКЛ МИКРОФОНА ---
        val micEnableCheckBox = CheckBox(this).apply {
            text = "Микрофон ВКЛ/ВЫКЛ"
            isChecked = true
            setOnCheckedChangeListener { _, isChecked -> 
                setMicVolume(if (isChecked) 1.0f else 0.0f) 
            }
        }

        // --- ГРОМКОСТЬ МИКРОФОНА ---
        val micVolText = TextView(this).apply { text = "Громкость Микрофона: 100%" }
        val micVolSlider = SeekBar(this).apply {
            max = 100; progress = 100 
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, f: Boolean) { 
                    setMicVolume(p / 100.0f); micVolText.text = "Громкость Микрофона: $p%" 
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}; override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }

        val filterCheckBox = CheckBox(this).apply {
            text = "DSP-фильтры (Bandpass)"; isChecked = true 
            setOnCheckedChangeListener { _, isChecked -> setFilterEnabled(isChecked) }
        }

        // --- NOISE REDUCTION ---
        val btnLearn = Button(this).apply {
            text = "1. Захватить шум фона"
            setOnClickListener { startLearning(); Toast.makeText(context, "Слушаю фон...", Toast.LENGTH_SHORT).show() }
        }

        val noiseReductionCheckBox = CheckBox(this).apply {
            text = "2. Включить Noise Reduction"; isChecked = false
            setOnCheckedChangeListener { _, isChecked -> setNoiseReduction(isChecked) }
        }

        val scaleText = TextView(this).apply { text = "Scale (Агрессивность): 100%" }
        val scaleSlider = SeekBar(this).apply {
            max = 300; progress = 100
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, f: Boolean) { 
                    setScale(p / 100.0f); scaleText.text = "Scale: $p%" 
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}; override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }

        val thText = TextView(this).apply { text = "Threshold (Порог): 10%" }
        val thSlider = SeekBar(this).apply {
            max = 100; progress = 10
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, f: Boolean) { 
                    setThreshold(p / 100.0f); thText.text = "Threshold: $p%" 
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}; override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }

        // --- ЗАПИСЬ (ОДНА КНОПКА) ---
        val btnRecordToggle = Button(this).apply {
            text = "Начать запись"
            setOnClickListener {
                if (!isRecording) {
                    startRecording()
                    text = "Остановить и Сохранить"
                    isRecording = true
                    Toast.makeText(context, "Запись пошла...", Toast.LENGTH_SHORT).show()
                } else {
                    stopRecording(recordPath)
                    text = "Начать запись"
                    isRecording = false
                    Toast.makeText(context, "Сохранено в $recordPath", Toast.LENGTH_LONG).show()
                }
            }
        }

        spectrogramView = SpectrogramView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 300)
        }

        val btnStart = Button(this).apply { text = "Включить систему"; setOnClickListener { text = if (startAudio()) "Работает!" else "Ошибка" } }
        val btnStop = Button(this).apply { text = "Остановить"; setOnClickListener { stopAudio(); text = "Стоп" } }

        layout.addView(textView)
        layout.addView(noiseVolText)
        layout.addView(noiseVolSlider)
        layout.addView(micEnableCheckBox) // Переключатель
        layout.addView(micVolText)
        layout.addView(micVolSlider)
        layout.addView(filterCheckBox)
        layout.addView(btnLearn)
        layout.addView(noiseReductionCheckBox)
        layout.addView(scaleText)
        layout.addView(scaleSlider)
        layout.addView(thText)
        layout.addView(thSlider)
        layout.addView(btnRecordToggle) // Одна кнопка
        layout.addView(spectrogramView)
        layout.addView(btnStart)
        layout.addView(btnStop)
        
        scrollView.addView(layout)
        setContentView(scrollView)
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
