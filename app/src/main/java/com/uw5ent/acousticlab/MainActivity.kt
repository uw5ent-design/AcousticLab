package com.uw5ent.acousticlab

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // ЗАПРАШИВАЕМ РАЗРЕШЕНИЕ НА МИКРОФОН
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 1)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(60, 60, 60, 60)
        }

        val textView = TextView(this).apply {
            textSize = 20f
            text = getDspStatus()
        }

        // ДОБАВЛЯЕМ ПОЛЗУНОК (Имитация Микшера)
        val mixerSlider = SeekBar(this).apply {
            max = 100
            progress = 50 // По умолчанию 50% шума и 50% микрофона
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
        layout.addView(TextView(this).apply { text = "Баланс Шум / Микрофон:" })
        layout.addView(mixerSlider)
        layout.addView(btnStart)
        layout.addView(btnStop)
        
        setContentView(layout)
    }
}
