package io.github.xeon3d.peepeebox

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.AdapterView
import android.widget.BaseAdapter
import android.widget.ListView
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import android.widget.Toolbar
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The Machine Manager: the Photo Play images in the app's own folder (where the emulator
 * can open them), identified from their contents - release, territory and NSB, as the
 * desktop's Machine Manager lists them, never from the file name.  A tap runs one (the
 * dongle answers for whatever the image says it is); a long press deletes it.  Import
 * copies an image in from anywhere the system file picker reaches.
 */
class ManagerActivity : Activity() {
    /** One image and what it was identified as. */
    private class Entry(val file: File, id: Array<String>) {
        val runnable = id[0] == "1"
        val release = id[1]
        val territory = id[2]
        val nsb = id[3]
        val banner = id[4]
        val note = id[5]
        val title get() = if (territory.isEmpty()) release else "$release ($territory)"
    }

    private val work = Executors.newSingleThreadExecutor()
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var dir: File
    private lateinit var list: ListView
    private lateinit var empty: TextView
    private var entries = listOf<Entry>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_manager)
        dir = getExternalFilesDir(null) ?: filesDir

        val toolbar = findViewById<Toolbar>(R.id.toolbar)
        list = findViewById(R.id.list)
        empty = findViewById(R.id.empty)
        list.emptyView = empty

        // Edge to edge (Android 15+): keep clear of the system bars and the cut-out.
        findViewById<View>(R.id.root).setOnApplyWindowInsetsListener { _, insets ->
            val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
            toolbar.setPadding(bars.left, bars.top, bars.right, 0)
            list.setPadding(bars.left, 0, bars.right, bars.bottom)
            list.clipToPadding = false
            insets
        }

        toolbar.subtitle = dir.path
        toolbar.menu.add(0, 1, 0, R.string.import_image).apply {
            setIcon(android.R.drawable.ic_menu_add)
            setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS or MenuItem.SHOW_AS_ACTION_WITH_TEXT)
        }
        toolbar.menu.add(0, 2, 1, R.string.rescan).setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        toolbar.menu.add(0, 3, 2, R.string.about).setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        toolbar.setOnMenuItemClickListener {
            when (it.itemId) {
                1 -> pickImport()
                2 -> scan()
                3 -> About.show(this)
            }
            true
        }

        list.onItemClickListener = AdapterView.OnItemClickListener { _, _, pos, _ -> runImage(entries[pos]) }
        list.onItemLongClickListener = AdapterView.OnItemLongClickListener { _, _, pos, _ ->
            confirmDelete(entries[pos])
            true
        }

        scan()
    }

    override fun onDestroy() {
        work.shutdownNow()
        super.onDestroy()
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    /** Identify every .img in the folder (off the UI thread: it reads the images). */
    private fun scan() {
        empty.text = "Looking at the images…"
        work.execute {
            val files = (dir.listFiles() ?: emptyArray())
                .filter { it.isFile && it.name.endsWith(".img", true) }
            val found = files.map { Entry(it, Native.nativeIdentify(it.path)) }
                .sortedWith(compareBy({ !it.runnable }, { it.release }, { it.territory }, { it.nsb }))
            ui.post {
                entries = found
                empty.setText(R.string.no_images)
                list.adapter = Rows()
            }
        }
    }

    /** Run it: the cabinet boots it, and the dongle answers for what the image says it is. */
    private fun runImage(e: Entry) {
        if (!e.runnable) {
            Toast.makeText(this, e.note.ifEmpty { "This image cannot run here." }, Toast.LENGTH_LONG).show()
            return
        }
        startActivity(Intent(this, CabinetActivity::class.java).putExtra(CabinetActivity.EXTRA_IMAGE, e.file.path))
        finish()
    }

    /* ---- import: any document the picker offers, copied into the folder ---- */

    private fun pickImport() {
        startActivityForResult(
            Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"),
            REQ_IMPORT,
        )
    }

    @Deprecated("framework Activity API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if ((requestCode == REQ_IMPORT) && (resultCode == RESULT_OK) && (uri != null)) importImage(uri)
    }

    private fun importImage(uri: Uri) {
        var name = "imported.img"
        var size = -1L
        contentResolver.query(uri, null, null, null, null)?.use { c ->
            if (c.moveToFirst()) {
                c.getColumnIndex(OpenableColumns.DISPLAY_NAME).takeIf { it >= 0 }?.let { name = c.getString(it) ?: name }
                c.getColumnIndex(OpenableColumns.SIZE).takeIf { it >= 0 }?.let { if (!c.isNull(it)) size = c.getLong(it) }
            }
        }
        name = name.replace('/', '_')
        // Every rig folder's image is HardDisk.img: named after what it is instead,
        // once it is here and can be read, so several can sit side by side.
        if (name.equals(GENERIC_NAME, ignoreCase = true)) return copyIn(uri, File(dir, GENERIC_NAME), size, byContents = true)
        val dest = File(dir, name)
        if (dest.exists()) {
            AlertDialog.Builder(this)
                .setTitle(R.string.import_image)
                .setMessage("$name is already here. Replace it?")
                .setPositiveButton(android.R.string.ok) { _, _ -> copyIn(uri, dest, size) }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        } else copyIn(uri, dest, size)
    }

    private fun copyIn(uri: Uri, dest: File, size: Long, byContents: Boolean = false) {
        val bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 1000
            isIndeterminate = size <= 0
            setPadding(dp(24), dp(16), dp(24), dp(8))
        }
        val cancelled = AtomicBoolean(false)
        val dialog = AlertDialog.Builder(this)
            .setTitle("Copying ${dest.name}")
            .setView(bar)
            .setNegativeButton(android.R.string.cancel) { _, _ -> cancelled.set(true) }
            .setCancelable(false)
            .show()
        val part = if (byContents) File(dir, "import-${System.currentTimeMillis()}.part") else File(dest.path + ".part")
        work.execute {
            val ok = try {
                contentResolver.openInputStream(uri)!!.use { input ->
                    part.outputStream().use { out ->
                        val buf = ByteArray(4 shl 20)
                        var done = 0L
                        while (!cancelled.get()) {
                            val n = input.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n)
                            done += n
                            if (size > 0) ui.post { bar.progress = (done * 1000 / size).toInt() }
                        }
                    }
                }
                !cancelled.get()
            } catch (e: Exception) {
                false
            }
            if (ok && byContents) {
                part.renameTo(unique(contentsName(part)))
            } else if (ok) {
                dest.delete()
                part.renameTo(dest)
            } else part.delete()
            ui.post {
                dialog.dismiss()
                if (!ok && !cancelled.get())
                    Toast.makeText(this, "Could not copy ${dest.name} (is there room?)", Toast.LENGTH_LONG).show()
                scan()
            }
        }
    }

    /** "IGO 6 (DE) NSB 1234.img": what the image says it is, as a file name. */
    private fun contentsName(f: File): String {
        val e = Entry(f, Native.nativeIdentify(f.path))
        val base = listOf(e.title, if (e.nsb.isNotEmpty()) "NSB ${e.nsb}" else "").filter { it.isNotEmpty() }.joinToString(" ")
        return base.replace(Regex("[/\\\\:*?\"<>|]"), "_").trim().ifEmpty { GENERIC_NAME.substringBeforeLast('.') } + ".img"
    }

    /** That name, or "name (2).img" and so on if it is taken. */
    private fun unique(name: String): File {
        var f = File(dir, name)
        var n = 2
        while (f.exists()) f = File(dir, "${name.substringBeforeLast('.')} (${n++}).img")
        return f
    }

    private fun confirmDelete(e: Entry) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete)
            .setMessage("Delete ${e.file.name} from this app's folder?\n(Only this copy.)")
            .setPositiveButton(R.string.delete) { _, _ ->
                e.file.delete()
                scan()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** The list's rows: what the image is, then its NSB, file and size (or why it cannot run). */
    private inner class Rows : BaseAdapter() {
        override fun getCount() = entries.size
        override fun getItem(position: Int) = entries[position]
        override fun getItemId(position: Int) = position.toLong()
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val v = convertView ?: layoutInflater.inflate(android.R.layout.simple_list_item_2, parent, false)
            val e = entries[position]
            val t1 = v.findViewById<TextView>(android.R.id.text1)
            val t2 = v.findViewById<TextView>(android.R.id.text2)
            t1.text = e.title.ifEmpty { e.file.name }
            val bytes = e.file.length()
            val where = listOfNotNull(
                if (e.nsb.isNotEmpty()) "NSB ${e.nsb}" else null,
                e.file.name,
                if (bytes >= 100_000_000) "%.1f GB".format(bytes / 1e9) else "%.0f MB".format(bytes / 1e6),
            ).joinToString("  ·  ")
            // A runnable image with a note (a Photo Play 2.0 that ppfix has not repaired)
            // says so under the rest, in amber.
            t2.text = when {
                !e.runnable -> e.note.ifEmpty { "Cannot run here" }
                e.note.isNotEmpty() -> "$where\n${e.note}"
                else -> where
            }
            t1.setTextColor(if (e.runnable) Color.WHITE else Color.GRAY)
            t2.setTextColor(
                when {
                    !e.runnable -> Color.GRAY
                    e.note.isNotEmpty() -> Color.rgb(0xF2, 0xB8, 0x3A)
                    else -> Color.rgb(0xB8, 0xB8, 0xC0)
                }
            )
            v.setPadding(dp(16), dp(10), dp(16), dp(10))
            return v
        }
    }

    companion object {
        private const val REQ_IMPORT = 1
        private const val GENERIC_NAME = "HardDisk.img"
    }
}
