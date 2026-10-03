package io.github.xeon3d.peepeebox

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Drawable
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.ArrayAdapter
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toolbar
import android.window.OnBackInvokedDispatcher
import java.io.File

/**
 * The cabinet: the emulator's picture, 4:3 and centred, with an app bar (what runs, its
 * speed) and an action bar (Credit, Setup, Manager, Fullscreen, More).  One machine per
 * process: Exit ends the process, as a cabinet is switched off.
 */
class CabinetActivity : Activity() {
    private lateinit var toolbar: Toolbar
    private lateinit var actions: View
    private lateinit var screen: SurfaceView
    private val ui = Handler(Looper.getMainLooper())
    private var fullscreen = false
    private var userPaused = false
    private var multicast: WifiManager.MulticastLock? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_cabinet)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) // attract mode is no "activity" to Android

        toolbar = findViewById(R.id.toolbar)
        actions = findViewById(R.id.actions)
        screen = findViewById(R.id.screen)

        // Android 15+ draws apps edge to edge: keep the bars clear of the status bar,
        // the navigation bar and the camera cut-out (none of them while fullscreen).
        val actionsTop = actions.paddingTop
        val actionsBottom = actions.paddingBottom
        findViewById<View>(R.id.root).setOnApplyWindowInsetsListener { _, insets ->
            val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
            toolbar.setPadding(bars.left, bars.top, bars.right, 0)
            actions.setPadding(bars.left, actionsTop, bars.right, actionsBottom + bars.bottom)
            insets
        }

        val dir = getExternalFilesDir(null) ?: filesDir
        if (!Native.started) {
            // Recreated by Android without a pick (the process had been ended in the
            // background): back to the Machine Manager.
            if (intent.getStringExtra(EXTRA_IMAGE) == null) {
                startActivity(Intent(this, ManagerActivity::class.java))
                finish()
                return
            }
            Assets.unpack(this, dir)
            when (Native.nativeInit(dir.path)) {
                0 -> {
                    select(intent)
                    Native.nativeStart()
                    Native.started = true
                }
                2 -> return fatal("The ROM set is missing from ${dir.path}/roms.")
                else -> return fatal("The cabinet could not be started (see ${dir.path}/86box.log).")
            }
        }

        // The Wi-Fi drops multicast (the local switch, for fun.net over the network
        // card) unless an app holds this.
        multicast = (applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager)
            .createMulticastLock("PeepeeBox").apply {
                setReferenceCounted(false)
                acquire()
            }

        screen.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) = Native.nativeSetSurface(holder.surface)
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) =
                Native.nativeSetSurface(holder.surface)
            override fun surfaceDestroyed(holder: SurfaceHolder) = Native.nativeSetSurface(null)
        })
        screen.setOnTouchListener { v, e -> touch(v, e) }

        toolbar.setNavigationOnClickListener { openManager() }
        // A tap is one 1.00 EUR coin; a long press offers every coin and note the
        // cabinet takes (ten lines on the I/O card, one per kind of money).
        findViewById<View>(R.id.btnCredit).setOnClickListener { Native.nativePulse(Native.LINE_COIN4) }
        findViewById<View>(R.id.btnCredit).setOnLongClickListener {
            money()
            true
        }
        findViewById<View>(R.id.btnSetup).setOnClickListener { Native.nativePulse(Native.LINE_SETUP) }
        findViewById<View>(R.id.btnManager).setOnClickListener { openManager() }
        findViewById<View>(R.id.btnFullscreen).setOnClickListener { setFullscreen(true) }
        findViewById<View>(R.id.btnMore).setOnClickListener { more(it) }
        // In fullscreen, a tap on the black beside the picture brings the bars back.
        findViewById<View>(R.id.stage).setOnClickListener { if (fullscreen) setFullscreen(false) }
        sizeIcons()

        onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT) {
            if (fullscreen) setFullscreen(false) else confirmExit()
        }

        updateBar()
    }

    /** A new pick from the Machine Manager while this cabinet runs: swap the disk. */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (Native.started && (intent.getStringExtra(EXTRA_IMAGE) != null)) {
            select(intent)
            updateBarNow()
        }
    }

    private fun select(i: Intent) {
        Native.nativeSelect(i.getStringExtra(EXTRA_IMAGE) ?: return)
    }

    private fun openManager() = startActivity(Intent(this, ManagerActivity::class.java))

    override fun onResume() {
        super.onResume()
        if (Native.started && !userPaused) Native.nativePause(false)
    }

    /* Android may end an app in the background without notice: pause and save. */
    override fun onPause() {
        if (Native.started) Native.nativePause(true)
        super.onPause()
    }

    override fun onDestroy() {
        ui.removeCallbacksAndMessages(null)
        multicast?.release()
        super.onDestroy()
    }

    /** The touchscreen: where the finger is on the picture, and whether it is down. */
    private fun touch(v: View, e: MotionEvent): Boolean {
        val down = when (e.actionMasked) {
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> false
            else -> true
        }
        Native.nativeTouch(e.x / v.width, e.y / v.height, down)
        return true
    }

    /**
     * Every kind of money the cabinet takes, as the desktop's toolbar has them: six coins
     * from the coin validator and four notes from the bill validator, each on its own
     * line.  They go by channel; what each is worth is the image's operator setup (these
     * are the usual euro values).
     */
    private fun money() {
        val lines = listOf(
            Triple(Native.LINE_COIN1, "Coin 1 (0.10 EUR)", R.drawable.ic_coin_1),
            Triple(Native.LINE_COIN2, "Coin 2 (0.20 EUR)", R.drawable.ic_coin_2),
            Triple(Native.LINE_COIN3, "Coin 3 (0.50 EUR)", R.drawable.ic_coin_3),
            Triple(Native.LINE_COIN4, "Coin 4 (1.00 EUR)", R.drawable.ic_coin_4),
            Triple(Native.LINE_COIN5, "Coin 5 (2.00 EUR)", R.drawable.ic_coin_5),
            Triple(Native.LINE_COIN6, "Coin 6 (token)", R.drawable.ic_coin_6),
            Triple(Native.LINE_NOTE1, "Note 1 (5 EUR)", R.drawable.ic_note_1),
            Triple(Native.LINE_NOTE2, "Note 2 (10 EUR)", R.drawable.ic_note_2),
            Triple(Native.LINE_NOTE3, "Note 3 (20 EUR)", R.drawable.ic_note_3),
            Triple(Native.LINE_NOTE4, "Note 4 (50 EUR)", R.drawable.ic_note_4),
        )
        val px = (32 * resources.displayMetrics.density).toInt()
        val adapter = object : ArrayAdapter<String>(this, android.R.layout.select_dialog_item, lines.map { it.second }) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                val tv = super.getView(position, convertView, parent) as TextView
                val d = getDrawable(lines[position].third)!!.mutate()
                d.setBounds(0, 0, px, px)
                tv.setCompoundDrawablesRelative(d, null, null, null)
                tv.compoundDrawablePadding = px / 2
                return tv
            }
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.money)
            .setAdapter(adapter) { _, which -> Native.nativePulse(lines[which].first) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun more(anchor: View) {
        val menu = PopupMenu(this, anchor)
        menu.menu.add(0, 1, 0, R.string.money)
        menu.menu.add(0, 5, 1, R.string.network)
        menu.menu.add(0, 6, 1, R.string.modem)
        menu.menu.add(0, 2, 2, if (userPaused) R.string.resume else R.string.pause)
        menu.menu.add(0, 3, 3, R.string.reset)
        menu.menu.add(0, 7, 4, R.string.about)
        menu.menu.add(0, 4, 5, R.string.exit)
        menu.setOnMenuItemClickListener {
            when (it.itemId) {
                1 -> money()
                2 -> {
                    userPaused = !userPaused
                    Native.nativePause(userPaused)
                    updateBar()
                }
                3 -> Native.nativeHardReset()
                4 -> confirmExit()
                5 -> Network.show(this) { updateBarNow() }
                6 -> Modem.show(this) { updateBarNow() }
                7 -> About.show(this)
            }
            true
        }
        menu.show()
    }

    private fun setFullscreen(on: Boolean) {
        fullscreen = on
        toolbar.visibility = if (on) View.GONE else View.VISIBLE
        actions.visibility = if (on) View.GONE else View.VISIBLE
        window.insetsController?.let {
            if (on) {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            } else {
                it.show(WindowInsets.Type.systemBars())
            }
        }
    }

    private fun confirmExit() {
        AlertDialog.Builder(this)
            .setTitle(R.string.exit)
            .setMessage("Switch the cabinet off?")
            .setPositiveButton(R.string.exit) { _, _ -> exit() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** Off: save, stop, and end the process (the emulator is built once per process). */
    private fun exit() {
        if (Native.started) Native.nativeStop()
        finishAndRemoveTask()
        Process.killProcess(Process.myPid())
    }

    private fun fatal(message: String) {
        AlertDialog.Builder(this)
            .setTitle(R.string.app_name)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok) { _, _ -> finishAndRemoveTask() }
            .setCancelable(false)
            .show()
    }

    /** Title: what runs.  Subtitle: the dongle banner, resolution, speed (or paused). Once a second. */
    private fun updateBar() {
        updateBarNow()
        ui.postDelayed({ updateBar() }, 1000)
    }

    private fun updateBarNow() {
        if (Native.started && !Native.nativeIsRunning()) return exit() // the guest powered off
        val info = Native.nativeCabinetInfo()
        val wh = Native.nativeFrameSize()
        toolbar.title = info[0].ifEmpty { getString(R.string.app_name) }
        val parts = mutableListOf(info[1])
        if (wh[0] > 0) parts += "${wh[0]} × ${wh[1]}"
        parts += if (userPaused) getString(R.string.paused) else "${Native.nativeSpeed()}%"
        toolbar.subtitle = parts.filter { it.isNotEmpty() }.joinToString("  ·  ")
    }

    /** The bar's icons at one size (they come as 96 px PNGs and 24 dp vectors). */
    private fun sizeIcons() {
        val px = (28 * resources.displayMetrics.density).toInt()
        for (id in intArrayOf(R.id.btnCredit, R.id.btnSetup, R.id.btnManager, R.id.btnFullscreen, R.id.btnMore)) {
            val tv = findViewById<TextView>(id)
            val d: Drawable = tv.compoundDrawablesRelative[1] ?: continue
            d.setBounds(0, 0, px, px)
            tv.setCompoundDrawablesRelative(null, d, null, null)
        }
    }

    companion object {
        /* The Machine Manager's pick (ManagerActivity.runImage). */
        const val EXTRA_IMAGE = "image"
    }
}
