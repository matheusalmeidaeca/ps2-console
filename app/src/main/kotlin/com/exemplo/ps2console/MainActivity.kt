package com.exemplo.ps2console

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var status: TextView
    private var isoUri: Uri? = null

    private val pickBios = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@registerForActivityResult
        val dest = File(filesDir, "bios.bin")
        contentResolver.openInputStream(uri)?.use { i -> dest.outputStream().use { o -> i.copyTo(o) } }
        status.text = if (NativeBridge.validateBios(dest.path)) "BIOS ok." else "Arquivo não parece uma BIOS de PS2."
    }

    private val pickIso = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri ?: return@registerForActivityResult
        contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        val fd = contentResolver.openFileDescriptor(uri, "r")?.use { NativeBridge.validateIso(it.fd) } ?: false
        isoUri = if (fd) uri else null
        status.text = if (fd) "ISO ok." else "Arquivo não é uma ISO válida."
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(48, 96, 48, 48) }
        status = TextView(this).apply { text = NativeBridge.init(this@MainActivity); textSize = 16f }
        fun btn(t: String, a: () -> Unit) = Button(this).apply { text = t; setOnClickListener { a() } }
        root.addView(status)
        root.addView(btn("Escolher BIOS") { pickBios.launch(arrayOf("*/*")) })
        root.addView(btn("Escolher jogo (ISO)") { pickIso.launch(arrayOf("*/*")) })
        root.addView(btn("Iniciar") {
            val uri = isoUri
            if (uri == null || !File(filesDir, "bios.bin").exists()) status.text = "Escolha BIOS e ISO primeiro."
            else startActivity(Intent(this, EmulatorActivity::class.java).setData(uri))
        })
        setContentView(root)
    }
}
