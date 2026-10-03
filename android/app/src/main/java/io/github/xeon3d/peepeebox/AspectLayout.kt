package io.github.xeon3d.peepeebox

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.FrameLayout

/** The cabinet's screen: the largest 4:3 box that fits, centred by its parent. */
class AspectLayout(context: Context, attrs: AttributeSet? = null) : FrameLayout(context, attrs) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = View.MeasureSpec.getSize(widthMeasureSpec)
        val h = View.MeasureSpec.getSize(heightMeasureSpec)
        var bw = w
        var bh = w * 3 / 4
        if (bh > h) {
            bh = h
            bw = h * 4 / 3
        }
        super.onMeasure(
            View.MeasureSpec.makeMeasureSpec(bw, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(bh, View.MeasureSpec.EXACTLY),
        )
    }
}
