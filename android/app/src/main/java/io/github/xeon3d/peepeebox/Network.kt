package io.github.xeon3d.peepeebox

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.net.ConnectivityManager
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
import java.net.Inet4Address

/**
 * The cabinet's network card (the desktop's Tools > Network, cut down to what Android can
 * do).  No cabinet had one, so it is out until fitted; an image given the Ethernet option
 * reaches fun.net through it instead of the modem.  The card is on the switch: either the
 * local one - a fun.net stand-in on the same network, over UDP multicast, as the desktop's
 * Local Switch - or a remote one by its address (host:port), as the desktop's Remote
 * Switch.  Fitting or removing the card, or moving it, restarts the cabinet.
 */
object Network {
    fun show(a: Activity, onApplied: () -> Unit) {
        val n = Native.nativeNetworkGet()
        val dp = a.resources.displayMetrics.density
        fun px(v: Int) = (v * dp).toInt()

        val fitted = Switch(a).apply {
            text = "Network card: ${n[4]}"
            isChecked = n[0] == "1"
        }
        val local = RadioButton(a).apply { id = View.generateViewId(); text = "Same network (the local switch)" }
        val remote = RadioButton(a).apply { id = View.generateViewId(); text = "Remote switch" }
        val how = RadioGroup(a).apply {
            addView(local)
            addView(remote)
            check(if (n[1] == "rswitch") remote.id else local.id)
        }
        val host = EditText(a).apply {
            hint = "The switch's address (host:port)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(n[2])
            isSingleLine = true
        }
        val secret = EditText(a).apply {
            hint = "Shared secret (the same on every side; may be empty)"
            setText(n[3])
            isSingleLine = true
        }
        val here = wifiAddress(a)
        val note = TextView(a).apply {
            text = (if (here != null) "This phone: $here\n" else "This phone is not on a network.\n") +
                "Only an image given the Ethernet option uses the card; the others reach fun.net " +
                "through the modem. A PC's fun.net stand-in joins with the same switch and secret. " +
                "The cabinet restarts to fit the card."
            setPadding(0, px(12), 0, 0)
        }

        fun refresh() {
            val on = fitted.isChecked
            local.isEnabled = on
            remote.isEnabled = on
            secret.isEnabled = on
            host.isEnabled = on
            host.visibility = if (how.checkedRadioButtonId == remote.id) View.VISIBLE else View.GONE
        }
        fitted.setOnCheckedChangeListener { _, _ -> refresh() }
        how.setOnCheckedChangeListener { _, _ -> refresh() }
        refresh()

        val body = LinearLayout(a).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(24), px(8), px(24), 0)
            addView(fitted)
            addView(how)
            addView(host)
            addView(secret)
            addView(note)
        }
        AlertDialog.Builder(a)
            .setTitle("Network")
            .setView(ScrollView(a).apply { addView(body) })
            .setPositiveButton("Apply") { _, _ ->
                val rs = how.checkedRadioButtonId == remote.id
                if (fitted.isChecked && rs && host.text.isBlank()) {
                    Toast.makeText(a, "Give the remote switch's address.", Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                Native.nativeNetworkSet(fitted.isChecked, rs, host.text.toString().trim(), secret.text.toString())
                onApplied()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** The phone's IPv4 address on its current network (for the other side to reach it). */
    private fun wifiAddress(c: Context): String? {
        val cm = c.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val lp = cm.getLinkProperties(cm.activeNetwork) ?: return null
        return lp.linkAddresses.map { it.address }.firstOrNull { it is Inet4Address && !it.isLoopbackAddress }?.hostAddress
    }
}
