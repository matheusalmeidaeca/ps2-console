package com.exemplo.ps2console

import android.content.Context
import android.view.Surface

object NativeBridge {
    init { System.loadLibrary("ps2bridge") }

    fun init(ctx: Context): String = nativeInit(ctx.applicationInfo.nativeLibraryDir)
    fun validateBios(path: String) = nativeValidateBios(path)
    fun validateIso(fd: Int) = nativeValidateIso(fd)
    fun start(systemDir: String, isoFd: Int, surface: Surface) = nativeStart(systemDir, isoFd, surface)
    fun setButton(id: Int, down: Boolean) = nativeSetButton(id, down)
    fun stop() = nativeStop()
    fun setOption(k: String, v: String) = nativeSetOption(k, v)

    private external fun nativeInit(libDir: String): String
    private external fun nativeValidateBios(path: String): Boolean
    private external fun nativeValidateIso(fd: Int): Boolean
    private external fun nativeStart(systemDir: String, isoFd: Int, surface: Surface): String
    private external fun nativeSetButton(id: Int, down: Boolean)
    private external fun nativeStop()
    private external fun nativeSetOption(k: String, v: String)
}
