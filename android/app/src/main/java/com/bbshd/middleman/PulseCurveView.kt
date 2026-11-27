package com.bbshd.middleman

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View

/**
 * The cadence/target-current graph.
 *
 * Values only, matching the round LCD's style: a filled curve, faint gridlines and
 * a marker where the rider currently is. No title and no legend — the two axis
 * tick labels are the minimum needed to read the shape.
 */
class PulseCurveView(context: Context, attrs: AttributeSet? = null) :
    View(context, attrs) {

    private var params: PasCurve.Parameters? = null
    private var cadenceRpm: Float? = null
    private var markerPct: Float? = null

    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF1E2C44.toInt()
        strokeWidth = 1f
        style = Paint.Style.STROKE
    }
    private val curveFill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x3335D6C4
        style = Paint.Style.FILL
    }
    private val curveStroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF35D6C4.toInt()
        strokeWidth = 3f
        style = Paint.Style.STROKE
    }
    private val guidePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x88FF4FD8.toInt()
        strokeWidth = 1f
        style = Paint.Style.STROKE
    }
    private val markerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFFF4FD8.toInt()
        style = Paint.Style.FILL
    }
    private val markerRing = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFFFFFFF.toInt()
        strokeWidth = 2f
        style = Paint.Style.STROKE
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF7C8CA6.toInt()
        textSize = 20f
    }

    private val curvePath = Path()
    private val fillPath = Path()

    fun setCurve(params: PasCurve.Parameters?) {
        this.params = params
        invalidate()
    }

    fun setMarker(cadence: Float?, pct: Float?) {
        cadenceRpm = cadence
        markerPct = pct
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        val padLeft = 34f
        val padBottom = 26f
        val padTop = 8f
        val padRight = 8f

        val w = width - padLeft - padRight
        val h = height - padTop - padBottom
        if (w <= 0f || h <= 0f) return

        fun xOf(rpm: Float) = padLeft + (rpm / PasCurve.MOTOR_MAX_CADENCE_RPM) * w
        fun yOf(pct: Float) = padTop + (1f - pct / 100f) * h

        // Grid: 25% steps vertically, 30 rpm horizontally.
        for (pct in intArrayOf(25, 50, 75, 100)) {
            canvas.drawLine(padLeft, yOf(pct.toFloat()), padLeft + w, yOf(pct.toFloat()), gridPaint)
        }
        for (rpm in intArrayOf(30, 60, 90, 120)) {
            canvas.drawLine(xOf(rpm.toFloat()), padTop, xOf(rpm.toFloat()), padTop + h, gridPaint)
        }

        // Axis labels — the scale is the one thing a bare curve cannot convey.
        canvas.drawText("100", 2f, yOf(100f) + 7f, labelPaint)
        canvas.drawText("0", 16f, yOf(0f) + 7f, labelPaint)
        canvas.drawText("0", padLeft - 6f, height - 6f, labelPaint)
        canvas.drawText("120", padLeft + w - 30f, height - 6f, labelPaint)

        val p = params
        if (p != null) {
            curvePath.reset()
            fillPath.reset()
            val steps = 120
            for (i in 0..steps) {
                val rpm = PasCurve.MOTOR_MAX_CADENCE_RPM * i / steps
                val pct = PasCurve.currentPct(p, rpm)
                val x = xOf(rpm)
                val y = yOf(pct)
                if (i == 0) {
                    curvePath.moveTo(x, y)
                    fillPath.moveTo(x, yOf(0f))
                    fillPath.lineTo(x, y)
                } else {
                    curvePath.lineTo(x, y)
                    fillPath.lineTo(x, y)
                }
            }
            fillPath.lineTo(padLeft + w, yOf(0f))
            fillPath.close()

            canvas.drawPath(fillPath, curveFill)
            canvas.drawPath(curvePath, curveStroke)
        }

        // Where the rider is right now: vertical and horizontal guides plus a
        // ringed core, because the marker colour sits close to the curve fill.
        val cad = cadenceRpm
        val pct = markerPct
        if (cad != null && pct != null) {
            val cx = xOf(cad.coerceIn(0f, PasCurve.MOTOR_MAX_CADENCE_RPM))
            val cy = yOf(pct.coerceIn(0f, 100f))
            canvas.drawLine(cx, padTop, cx, padTop + h, guidePaint)
            canvas.drawLine(padLeft, cy, padLeft + w, cy, guidePaint)
            canvas.drawCircle(cx, cy, 6f, markerPaint)
            canvas.drawCircle(cx, cy, 6f, markerRing)
        }
    }
}
