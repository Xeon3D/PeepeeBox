package io.github.xeon3d.peepeebox

import android.app.Activity
import android.app.AlertDialog
import android.text.InputType
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast

/**
 * The modem on COM4 (0x2E8, IRQ 10), as the desktop's Tools > Modem: none, or one of the
 * two parts the fun.net cabinets are found with.  There is no telephone network: a dial
 * connects to a TCP host instead - a fun.net stand-in (fun.net-server, port 23) - and with
 * no host the line is dead.  Fitting, removing or changing the modem, or moving its line,
 * restarts the cabinet; the speaker is heard (or not) at once.
 */
object Modem {
    fun show(a: Activity, onApplied: () -> Unit) {
        val m = Native.nativeModemGet()
        val parts = (4 until m.size step 2).map { m[it] to m[it + 1] }
        val dp = a.resources.displayMetrics.density
        fun px(v: Int) = (v * dp).toInt()

        val none = RadioButton(a).apply { id = View.generateViewId(); text = "None" }
        val choice = RadioGroup(a).apply { addView(none) }
        val ids = parts.map { (part, name) ->
            RadioButton(a).apply {
                id = View.generateViewId()
                text = name
                choice.addView(this)
                if (m[0] == part) choice.check(id)
            }.id to part
        }.toMap()
        if (m[0].isEmpty()) choice.check(none.id)

        val host = EditText(a).apply {
            hint = "Host a dial connects to (empty: no line)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(m[1])
            isSingleLine = true
        }
        val port = EditText(a).apply {
            hint = "Port"
            inputType = InputType.TYPE_CLASS_NUMBER
            setText(m[2])
            isSingleLine = true
        }
        val sounds = Switch(a).apply {
            text = "Modem sounds (dialling, the handshake)"
            isChecked = m[3] == "1"
            setPadding(0, px(8), 0, 0)
        }
        val note = TextView(a).apply {
            text = "The cabinets that were on fun.net had one of these on COM4. Whatever number " +
                "the cabinet dials, the call goes to this host: a fun.net stand-in (its port is 23 " +
                "by default) on this network or the Internet. The cabinet restarts to fit the " +
                "modem or change its line."
            setPadding(0, px(12), 0, 0)
        }

        fun refresh() {
            val on = choice.checkedRadioButtonId != none.id
            host.isEnabled = on
            port.isEnabled = on
        }
        choice.setOnCheckedChangeListener { _, _ -> refresh() }
        refresh()

        val body = LinearLayout(a).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(24), px(8), px(24), 0)
            addView(TextView(a).apply { text = "Modem on COM4" })
            addView(choice)
            addView(host)
            addView(port)
            addView(sounds)
            addView(note)
        }
        AlertDialog.Builder(a)
            .setTitle("Modem")
            .setView(ScrollView(a).apply { addView(body) })
            .setPositiveButton("Apply") { _, _ ->
                val part = ids[choice.checkedRadioButtonId] ?: ""
                val p = port.text.toString().trim().toIntOrNull()
                if (part.isNotEmpty() && host.text.isNotBlank() && (p == null || p !in 1..32767)) {
                    Toast.makeText(a, "The port is a number from 1 to 32767.", Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                Native.nativeModemSet(part, host.text.toString().trim(), p ?: 23, sounds.isChecked)
                onApplied()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }
}
