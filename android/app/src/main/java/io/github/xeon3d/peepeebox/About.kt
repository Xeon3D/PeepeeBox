package io.github.xeon3d.peepeebox

import android.app.Activity
import android.app.AlertDialog

/** The About box, from the Machine Manager's menu and the cabinet's More menu. */
object About {
    fun show(a: Activity) {
        val version = a.packageManager.getPackageInfo(a.packageName, 0).versionName
        AlertDialog.Builder(a)
            .setIcon(R.mipmap.ic_launcher)
            .setTitle("${a.getString(R.string.app_name)} $version")
            .setMessage(R.string.about_text)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }
}
