package com.exemplo.ps2console

import android.os.Bundle
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Button
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class EmulatorActivity : AppCompatActivity(), SurfaceHolder.Callback {
    // ids dos botões: 0-3 direcional, 4-7 X O □ △
    private val labels = listOf("↑", "↓", "←", "→", "X", "O", "□", "△")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = FrameLayout(this)
        val sv = SurfaceView(this).also { it.holder.addCallback(this) }
        root.addView(sv)
        labels.forEachIndexed { id, t ->
            val b = Button(this).apply {
                text = t; alpha = 0.5f
                setOnTouchListener { _, e ->
                    when (e.action) {
                        MotionEvent.ACTION_DOWN -> NativeBridge.setButton(id, true)
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> NativeBridge.setButton(id, false)
                    }
                    true
                }
            }
            val lp = FrameLayout.LayoutParams(160, 160)
            val left = id < 4
            // (distância horizontal, distância de baixo) dentro do grupo
            val pos = mapOf(0 to (160 to 320), 1 to (160 to 0), 2 to (0 to 160), 3 to (320 to 160),
                7 to (160 to 320), 4 to (160 to 0), 6 to (320 to 160), 5 to (0 to 160))[id]!!
            lp.gravity = android.view.Gravity.BOTTOM or if (left) android.view.Gravity.START else android.view.Gravity.END
            if (left) lp.leftMargin = 24 + pos.first else lp.rightMargin = 24 + pos.first
            lp.bottomMargin = 24 + pos.second
            root.addView(b, lp)
        }
        listOf(8 to "START", 9 to "SELECT").forEachIndexed { i, (id, t) ->
            val b = Button(this).apply {
                text = t; alpha = 0.5f
                setOnTouchListener { _, ev ->
                    when (ev.action) {
                        MotionEvent.ACTION_DOWN -> NativeBridge.setButton(id, true)
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> NativeBridge.setButton(id, false)
                    }
                    true
                }
            }
            val lp = FrameLayout.LayoutParams(300, 120)
            lp.gravity = android.view.Gravity.TOP or android.view.Gravity.CENTER_HORIZONTAL
            lp.topMargin = 24 + i * 140
            root.addView(b, lp)
        }
        setContentView(root)
    }

    override fun surfaceCreated(h: SurfaceHolder) {
        val iso = contentResolver.openFileDescriptor(intent.data!!, "r")!!.detachFd()
        val sys = File(filesDir, "system").path
        // carregar o jogo pode demorar: fora da thread principal
        Thread {
            val msg = NativeBridge.start(sys, iso, h.surface)
            runOnUiThread { Toast.makeText(this, "$msg (log: botão Ver log)", Toast.LENGTH_LONG).show() }
        }.start()
    }
    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {}
    override fun surfaceDestroyed(h: SurfaceHolder) { NativeBridge.stop() }
}
