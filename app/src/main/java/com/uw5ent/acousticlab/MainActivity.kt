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
    
    // ВОТ ОНИ, НАШИ ПОЛЗУНКИ:
    external fun setScale(scale: Float)
    external fun setThreshold(th: Float)
    external fun setFftSize(size: Int)
    external fun setOverlap(overlap: Int)
    
    external fun startRecording()
    external fun stopRecording(path: String)

    private val fftBuffer = FloatArray(64)
    private val handler = Handler(Looper.getMainLooper())
    private var spectrogramView: SpectrogramView? = null
    private var recordPath: String = ""
    private var isRecording = false

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
            setPadding(40, 40, 40, 40)
        }

        val textView = TextView(this).apply { 
            textSize = 16f
            text = getDspStatus() 
        }

        // --- ГРОМКОСТЬ ---
        val noiseVolText = TextView(this).apply { text = "Громкость Шума: 50%" }
        val noiseVolSlider = SeekBar(this).apply {
            max = 100; progress = 50 
            setOnSeekBarChangeListener(createListener { 
                setNoiseVolume(it / 100.0f)
                noiseVolText.text = "Громкость Шума: $it%" 
            })
        }

        val micVolText = TextView(this).apply { text = "Громкость Микрофона: 100%" }
        val micVolSlider = SeekBar(this).apply {
            max = 100; progress = 100 
            setOnSeekBarChangeListener(createListener { 
                setMicVolume(it / 100.0f)
                micVolText.text = "Громкость Микрофона: $it%" 
            })
        }

        val filterCheckBox = CheckBox(this).apply {
            text = "DSP-фильтры (Bandpass)"
            isChecked = false 
            setOnCheckedChangeListener { _, isChecked -> setFilterEnabled(isChecked) }
        }

        // --- NOISE REDUCTION ---
        val btnLearn = Button(this).apply {
            text = "ЗАХВАТИТЬ ШУМ ФОНА"
            setOnClickListener { 
                startLearning()
                Toast.makeText(context, "Слушаю фон 2 сек...", Toast.LENGTH_SHORT).show() 
            }
        }

        val noiseReductionCheckBox = CheckBox(this).apply {
            text = "Включить Noise Reduction (GoldWave)"
            isChecked = false
            setOnCheckedChangeListener { _, isChecked -> setNoiseReduction(isChecked) }
        }

        // --- ПОЛЗУНКИ ИЗ GOLDWAVE ---
        val scaleText = TextView(this).apply { text = "Scale (Агрессивность): 100%" }
        val scaleSlider = SeekBar(this).apply {
            max = 300; progress = 100
            setOnSeekBarChangeListener(createListener { 
                setScale(it / 100.0f)
                scaleText.text = "Scale: $it%" 
            })
        }

        val thText = TextView(this).apply { text = "Threshold (Порог): 10%" }
        val thSlider = SeekBar(this).apply {
            max = 100; progress = 10
            setOnSeekBarChangeListener(createListener { 
                setThreshold(it / 100.0f)
                thText.text = "Threshold: $it%" 
            })
        }

        val fftText = TextView(this).apply { text = "FFT size: 9 (512)" }
        val fftSlider = SeekBar(this).apply {
            max = 3; progress = 0 
            setOnSeekBarChangeListener(createListener { p ->
                val size = 512 shl p 
                setFftSize(size)
                fftText.text = "FFT size: ${9 + p} ($size)"
            })
        }

        // --- ОБНОВЛЕННЫЙ OVERLAP (4x, 8x, 16x) ---
        val overlapSpinner = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_item, arrayOf("Overlap: 4x", "Overlap: 8x", "Overlap: 16x")).apply {
                setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            }
            onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                    val overlapValue = when(position) {
                        0 -> 4
                        1 -> 8
                        2 -> 16
                        else -> 4
                    }
                    setOverlap(overlapValue)
                }
                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }
        }

        // --- ЗАПИСЬ ---
        val btnRecordToggle = Button(this).apply {
            text = "Начать запись"
            setOnClickListener {
                if (!isRecording) {
                    startRecording(); text = "Остановить и Сохранить"; isRecording = true
                } else {
                    stopRecording(recordPath); text = "Начать запись"; isRecording = false
                    Toast.makeText(context, "Сохранено!", Toast.LENGTH_SHORT).show()
                }
            }
        }

        spectrogramView = SpectrogramView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 400)
        }

        val btnStart = Button(this).apply { 
            text = "ВКЛЮЧИТЬ СИСТЕМУ"
            setOnClickListener { text = if (startAudio()) "РАБОТАЕТ!" else "Ошибка" } 
        }
        
        val btnStop = Button(this).apply { 
            text = "ОСТАНОВИТЬ"
            setOnClickListener { stopAudio(); text = "Остановлено" } 
        }

        layout.addView(textView)
        layout.addView(noiseVolText); layout.addView(noiseVolSlider)
        layout.addView(micVolText); layout.addView(micVolSlider)
        layout.addView(filterCheckBox)
        layout.addView(btnLearn)
        layout.addView(noiseReductionCheckBox)
        layout.addView(scaleText); layout.addView(scaleSlider)
        layout.addView(thText); layout.addView(thSlider)
        layout.addView(fftText); layout.addView(fftSlider)
        layout.addView(overlapSpinner)
        layout.addView(btnRecordToggle)
        layout.addView(spectrogramView)
        layout.addView(btnStart)
        layout.addView(btnStop)
        
        scrollView.addView(layout)
        setContentView(scrollView)
    }

    private fun createListener(onChange: (Int) -> Unit) = object : SeekBar.OnSeekBarChangeListener {
        override fun onProgressChanged(sb: SeekBar?, p: Int, f: Boolean) = onChange(p)
        override fun onStartTrackingTouch(sb: SeekBar?) {}
        override fun onStopTrackingTouch(sb: SeekBar?) {}
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
