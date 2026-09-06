package io.github.nbjelanovic.otclient

import android.text.format.DateFormat
import androidx.activity.ComponentActivity
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.util.Date
import kotlinx.coroutines.delay

internal fun configureTrailFullscreen(activity: ComponentActivity) {
    if (android.os.Build.VERSION.SDK_INT >= 28) {
        activity.window.attributes = activity.window.attributes.apply {
            layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
    }
    WindowCompat.setDecorFitsSystemWindows(activity.window, false)
    WindowInsetsControllerCompat(activity.window, activity.window.decorView).apply {
        systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        hide(WindowInsetsCompat.Type.systemBars())
    }
}

/** Device metrics are never substituted with the phone's GPS, battery, or network activity. */
@Composable
internal fun V1StatusStrip(state: TrailAppUiState) {
    val context = LocalContext.current
    val metrics = V1DeviceStatusStripProjector.from(state)
    val clock by produceState(initialValue = DateFormat.getTimeFormat(context).format(Date())) {
        while (true) {
            value = DateFormat.getTimeFormat(context).format(Date())
            delay(1_000)
        }
    }
    val active = MaterialTheme.colorScheme.primary
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp)
        .windowInsetsPadding(WindowInsets.displayCutout.only(WindowInsetsSides.Horizontal))
        .padding(horizontal = 12.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(clock, fontSize = 13.sp, modifier = Modifier.semantics { contentDescription = "Phone time $clock" })
        Canvas(Modifier.size(15.dp, 20.dp).semantics {
            contentDescription = if (metrics.bluetoothConnected) "Bluetooth connected" else "Bluetooth disconnected"
        }) {
            val path = Path().apply {
                moveTo(size.width * .45f, 0f); lineTo(size.width * .45f, size.height)
                lineTo(size.width, size.height * .72f); lineTo(0f, size.height * .23f)
                moveTo(0f, size.height * .77f); lineTo(size.width, size.height * .28f)
                lineTo(size.width * .45f, 0f)
            }
            drawPath(path, if (metrics.bluetoothConnected) active else muted.copy(alpha = .45f), style = Stroke(1.5.dp.toPx()))
        }
        Spacer(Modifier.weight(1f))
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(3.dp),
            modifier = Modifier.semantics(mergeDescendants = true) {
                contentDescription = "Heltec GPS ${metrics.gps.name.lowercase()}; satellites ${metrics.satellites ?: "unavailable"}"
            }) {
            Canvas(Modifier.size(16.dp)) {
                val color = if (metrics.gps == V1DeviceGpsStatus.CURRENT) active else muted
                drawCircle(color, radius = size.minDimension * .28f, style = Stroke(1.dp.toPx()))
                drawLine(color, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height * .2f), 1.dp.toPx())
                drawLine(color, Offset(size.width / 2, size.height * .8f), Offset(size.width / 2, size.height), 1.dp.toPx())
                drawLine(color, Offset(0f, size.height / 2), Offset(size.width * .2f, size.height / 2), 1.dp.toPx())
                drawLine(color, Offset(size.width * .8f, size.height / 2), Offset(size.width, size.height / 2), 1.dp.toPx())
            }
            Text(metrics.satellites?.toString() ?: "—", fontSize = 12.sp)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(3.dp),
            modifier = Modifier.semantics(mergeDescendants = true) {
                contentDescription = "Heltec battery ${metrics.batteryPercent?.let { "$it percent" } ?: "unavailable"}" +
                    if (metrics.externalPower == true) "; external power" else ""
            }) {
            Canvas(Modifier.size(18.dp, 11.dp)) {
                drawRect(muted, size = androidx.compose.ui.geometry.Size(size.width * .85f, size.height), style = Stroke(1.dp.toPx()))
                drawLine(muted, Offset(size.width, size.height * .3f), Offset(size.width, size.height * .7f), 2.dp.toPx())
            }
            Text(metrics.batteryPercent?.let { "$it%" } ?: "—", fontSize = 12.sp)
            if (metrics.externalPower == true) Text("+", fontSize = 12.sp)
        }
        Row(modifier = Modifier.semantics(mergeDescendants = true) {
            contentDescription = "Heltec transmit ${metrics.transmitting ?: "unavailable"}; receive ${metrics.receiving ?: "unavailable"}"
        }) {
            Text("↑", color = if (metrics.transmitting == true) active else muted.copy(alpha = .45f), fontSize = 16.sp)
            Text("↓", color = if (metrics.receiving == true) active else muted.copy(alpha = .45f), fontSize = 16.sp)
            if (metrics.transmitting == null && metrics.receiving == null) Text("—", fontSize = 12.sp, color = muted)
        }
    }
}
