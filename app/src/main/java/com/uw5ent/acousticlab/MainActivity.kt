package com.uw5ent.acousticlab

import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    init {
        System.loadLibrary("acousticlab")
    }

    external fun getDspStatus(): String
    external fun startAudio(): Boolean
    external fun stopAudio()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Создаем интерфейс программно
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(60, 60, 60, 60)
        }

        val textView = TextView(this).apply {
            textSize = 20f
            text = getDspStatus()
        }

        val btnStart = Button(this).apply {
            text = "Включить белый шум"
            setOnClickListener {
                val success = startAudio()
                text = if (success) "Шум включен!" else "Ошибка запуска"
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
        layout.addView(btnStart)
        layout.addView(btnStop)
        
        setContentView(layout)
    }
}
