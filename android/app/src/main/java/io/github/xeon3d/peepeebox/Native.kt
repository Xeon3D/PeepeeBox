package io.github.xeon3d.peepeebox

import android.view.Surface

/** The emulator: libPeepeeBox.so (native/android_main.cpp). One machine per process. */
object Native {
    init {
        System.loadLibrary("PeepeeBox")
    }

    /** The funworld I/O card's lines (PeepeeBox's src/include/86box/funworld_io.h). */
    const val LINE_COIN1 = 0 // 0.10 EUR
    const val LINE_COIN2 = 1 // 0.20 EUR
    const val LINE_COIN3 = 2 // 0.50 EUR
    const val LINE_COIN4 = 3 // 1.00 EUR
    const val LINE_COIN5 = 4 // 2.00 EUR
    const val LINE_COIN6 = 5 // token
    const val LINE_SETUP = 6
    const val LINE_NOTE1 = 8 // 5 EUR
    const val LINE_NOTE2 = 9 // 10 EUR
    const val LINE_NOTE3 = 10 // 20 EUR
    const val LINE_NOTE4 = 11 // 50 EUR

    /** Set once the machine has been built (nativeInit + nativeStart). */
    var started = false

    /** 0 = ready; 1 = the configuration could not be loaded; 2 = no ROMs. */
    @JvmStatic external fun nativeInit(dir: String): Int
    @JvmStatic external fun nativeStart()
    @JvmStatic external fun nativeStop()
    @JvmStatic external fun nativeSetSurface(surface: Surface?)
    @JvmStatic external fun nativePause(paused: Boolean)
    @JvmStatic external fun nativeIsPaused(): Boolean
    /** x, y: 0..1 across the picture. */
    @JvmStatic external fun nativeTouch(x: Float, y: Float, down: Boolean)
    @JvmStatic external fun nativePulse(line: Int)
    @JvmStatic external fun nativeHardReset()
    @JvmStatic external fun nativeSpeed(): Int
    @JvmStatic external fun nativeFrameSize(): IntArray
    @JvmStatic external fun nativeIsRunning(): Boolean
    /** { title (release - NSB), detail (the dongle banner) } */
    @JvmStatic external fun nativeCabinetInfo(): Array<String>

    /* The Machine Manager.  nativeIdentify needs no machine. */
    /** { runnable "1"/"0", release, territory, NSB, banner, note } */
    @JvmStatic external fun nativeIdentify(path: String): Array<String>
    /** The cabinet's disk (after nativeInit); rebuilds a running machine. */
    @JvmStatic external fun nativeSelect(image: String)

    /* fun.net over the network card (RTL8139). */
    /** { fitted "1"/"0", "lswitch"/"rswitch", remote host, secret, card name } */
    @JvmStatic external fun nativeNetworkGet(): Array<String>
    /** Fit or remove the card, on the local switch or a remote one; restarts the cabinet. */
    @JvmStatic external fun nativeNetworkSet(on: Boolean, remote: Boolean, host: String, secret: String)

    /* The modem on COM4. */
    /** { fitted part ("" = none), host ("" = no line), port, sounds "1"/"0", then { part, name }... } */
    @JvmStatic external fun nativeModemGet(): Array<String>
    /** Fit a part (or "" for none) and set where a dial connects; restarts the cabinet if that changed. */
    @JvmStatic external fun nativeModemSet(part: String, host: String, port: Int, sounds: Boolean)
}
