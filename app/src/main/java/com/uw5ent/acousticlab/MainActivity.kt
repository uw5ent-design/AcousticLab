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
import android.widget.Button
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {

    init {
        System.loadLibrary("acousticlab")
    }

    external fun getDspStatus(): String
    external fun startAudio(): Boolean
    external fun stopAudio()
    external fun getFftData(outArray: FloatArray)
    external fun setMixRatio(ratio: Float) // <-- Наш новый метод

    private val fftBuffer = FloatArray(64)
    private val handler = Handler(Looper.getMainLooper())
    private var spectrogramView: SpectrogramView? = null

    private val updateRunnable = object : Runnable {
        override fun run() {
            getFftData(fftBuffer)
            spectrogramView?.updateFft(fftBuffer)
            handler.postDelayed(this, 33) 
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 1)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(30, 30, 30, 30)
        }

        val textView = TextView(this).apply {
            textSize = 16f
            text = getDspStatus()
        }

        // Настраиваем ползунок
        val mixerSlider = SeekBar(this).apply {
            max = 100
            progress = 50 // По умолчанию 50 на 50
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    // ОТПРАВЛЯЕМ ЗНАЧЕНИЕ В C++ (делим на 100.0, чтобы получить 0.0 - 1.0)
                    setMixRatio(progress / 100.0f)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }

        spectrogramView = SpectrogramView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                400
            )
        }

        val btnStart = Button(this).apply {
            text = "Включить Микшер"
            setOnClickListener {
                val success = startAudio()
                text = if (success) "Микшер работает!" else "Ошибка"
            }
        }

        val btnStop = Button(this).apply {
            text = "Остановить"
            setOnClickListener {
                stopAudio()
                text = "Остановлено"
            }
        }

        layout.addView(textView)
        layout.addView(TextView(this).apply { text = "Баланс (Шум <---> Микрофон):" })
        layout.addView(mixerSlider)
        layout.addView(spectrogramView)
        layout.addView(btnStart)
        layout.addView(btnStop)
        
        setContentView(layout)
    }

    override fun onResume() {
        super.onResume()
        handler.post(updateRunnable)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(updateRunnable)
    }
}

class SpectrogramView(context: Context) : android.view.View(context) {
    private var fftData = FloatArray(64)
    private val paint = Paint().apply {
        color = Color.GREEN // Сделал зеленым для разнообразия (как в матрице)
        style = Paint.Style.FILL
    }

    fun updateFft(newData: FloatArray) {
        this.fftData = newData
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(Color.BLACK)
        val barWidth = width.toFloat() / fftData.size
        for (i in fftData.indices) {
            val barHeight = (fftData[i] / 100f) * height
            canvas.drawRect(
                i * barWidth,
                height - barHeight,
                (i + 1) * barWidth - 2f,
                height.toFloat(),
                paint
            )
        }
    }
}
