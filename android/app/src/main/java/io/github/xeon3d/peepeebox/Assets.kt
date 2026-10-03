package io.github.xeon3d.peepeebox

import android.content.Context
import java.io.File

/**
 * What PeepeeBox ships beside its executable, shipped here as assets:
 *
 * - roms/: the 4DPS BIOS and the CL-GD5480's. The emulator opens ROMs with fopen(), so
 *   they are unpacked into the app's files directory - again whenever the app is updated.
 * - nvr/: the cabinet's settled CMOS and flash. A blank CMOS stops the BIOS on a hard
 *   disk error, so a missing file is seeded; one already there is the cabinet's own from
 *   then on, and is never overwritten (PeepeeBox/nvr/README.md).
 */
object Assets {
    fun unpack(context: Context, dir: File) {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val stamp = "${info.longVersionCode}:${info.lastUpdateTime}"
        val roms = File(dir, "roms")
        val stampFile = File(roms, ".unpacked")
        if (!stampFile.isFile || stampFile.readText() != stamp) {
            copyTree(context, "roms", roms, overwrite = true)
            stampFile.writeText(stamp)
        }
        copyTree(context, "nvr", File(dir, "nvr"), overwrite = false)
    }

    private fun copyTree(context: Context, asset: String, dest: File, overwrite: Boolean) {
        val children = context.assets.list(asset) ?: emptyArray()
        if (children.isEmpty()) {
            if (!overwrite && dest.exists()) return
            dest.parentFile?.mkdirs()
            context.assets.open(asset).use { input -> dest.outputStream().use { input.copyTo(it) } }
            return
        }
        dest.mkdirs()
        for (child in children) copyTree(context, "$asset/$child", File(dest, child), overwrite)
    }
}
