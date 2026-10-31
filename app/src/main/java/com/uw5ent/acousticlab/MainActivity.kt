package com.uw5ent.acousticlab

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    // Подгружаем нашу C++ библиотеку
    init {
        System.loadLibrary("acousticlab")
    }

    // Объявляем метод из C++
    external fun getDspStatus(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Создаем простой текст на экране для проверки
        val textView = TextView(this).apply {
            textSize = 24f
            text = getDspStatus()
            setPadding(60, 60, 60, 60)
        }
        setContentView(textView)
    }
}
