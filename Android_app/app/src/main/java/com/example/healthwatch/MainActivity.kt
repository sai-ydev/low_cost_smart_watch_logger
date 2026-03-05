package com.example.healthwatch

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.foundation.Canvas
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.toArgb

// ── History keys ──────────────────────────────────────────────────────────────
private val HR_HISTORY_SIZE   = 60
private val SPO2_HISTORY_SIZE = 60

class MainActivity : ComponentActivity() {

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val permissions = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions += listOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        }
        permissionLauncher.launch(permissions.toTypedArray())

        setContent {
            MaterialTheme {
                HealthDashboard()
            }
        }
    }
}

// ── Main dashboard ────────────────────────────────────────────────────────────
@Composable
fun HealthDashboard(vm: HealthViewModel = viewModel()) {
    val bleState    by vm.bleState.collectAsState()
    val data        by vm.healthData.collectAsState()
    val scrollState  = rememberScrollState()

    // ── HR history ────────────────────────────────────────────────────────────
    val hrHistory   = remember { mutableStateListOf<Float>() }
    val spo2History = remember { mutableStateListOf<Float>() }

    // Accumulate history whenever data changes
    LaunchedEffect(data.heartRate) {
        if (data.heartRate > 0) {
            if (hrHistory.size >= HR_HISTORY_SIZE) hrHistory.removeAt(0)
            hrHistory.add(data.heartRate.toFloat())
        }
    }
    LaunchedEffect(data.spo2) {
        if (data.spo2 > 0) {
            if (spo2History.size >= SPO2_HISTORY_SIZE) spo2History.removeAt(0)
            spo2History.add(data.spo2.toFloat())
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF121212))
            .padding(16.dp)
            .verticalScroll(scrollState),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            text       = "HealthWatch",
            color      = Color.White,
            fontSize   = 26.sp,
            fontWeight = FontWeight.Bold,
            modifier   = Modifier.padding(bottom = 4.dp)
        )

        ConnectionBar(
            bleState     = bleState,
            onConnect    = { vm.connect() },
            onDisconnect = { vm.disconnect() },
            onSyncTime   = { vm.syncTime() }
        )

        // ── Vitals ────────────────────────────────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            MetricCard(
                modifier = Modifier.weight(1f),
                label    = "Heart Rate",
                value    = if (data.heartRate > 0) "${data.heartRate}" else "--",
                unit     = "bpm",
                color    = Color(0xFFEF5350)
            )
            MetricCard(
                modifier = Modifier.weight(1f),
                label    = "SpO2",
                value    = if (data.spo2 > 0) "${data.spo2}" else "--",
                unit     = "%",
                color    = Color(0xFF42A5F5)
            )
        }

        // ── HR Graph ──────────────────────────────────────────────────────────
        if (hrHistory.size >= 2) {
            LiveGraph(
                title   = "Heart Rate",
                history = hrHistory,
                color   = Color(0xFFEF5350),
                yLabel  = "bpm"
            )
        }

        // ── SpO2 Graph ────────────────────────────────────────────────────────
        if (spo2History.size >= 2) {
            LiveGraph(
                title   = "SpO2",
                history = spo2History,
                color   = Color(0xFF42A5F5),
                yLabel  = "%"
            )
        }

        // ── Temperature + Steps ───────────────────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            MetricCard(
                modifier = Modifier.weight(1f),
                label    = "Temperature",
                value    = if (data.temperature > 0f) "%.2f".format(data.temperature) else "--",
                unit     = "°C",
                color    = Color(0xFFFFCA28)
            )
            MetricCard(
                modifier = Modifier.weight(1f),
                label    = "Steps",
                value    = "${data.steps}",
                unit     = "steps",
                color    = Color(0xFF66BB6A)
            )
        }

        // ── IMU ───────────────────────────────────────────────────────────────
        ImuCard(data = data)
    }
}

// ── Live graph card ───────────────────────────────────────────────────────────
@Composable
fun LiveGraph(
    title:   String,
    history: List<Float>,
    color:   Color,
    yLabel:  String
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF1E1E1E)),
        shape    = RoundedCornerShape(12.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {

            // Title + current value
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(text = title, color = Color.Gray, fontSize = 12.sp)
                Text(
                    text = if (history.isNotEmpty())
                        "%.0f %s".format(history.last(), yLabel)
                    else "--",
                    color = color,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Graph
            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(120.dp)
            ) {
                if (history.size < 2) return@Canvas

                val minVal = history.min()
                val maxVal = history.max()
                val range  = (maxVal - minVal).coerceAtLeast(1f)

                val stepX  = size.width / (history.size - 1).toFloat()
                val padY   = size.height * 0.1f

                // Grid lines
                val gridColor = android.graphics.Color.argb(60, 255, 255, 255)
                for (i in 0..3) {
                    val y = padY + (size.height - 2 * padY) * i / 3f
                    drawLine(
                        color = Color(gridColor),
                        start = Offset(0f, y),
                        end   = Offset(size.width, y),
                        strokeWidth = 1f
                    )
                }

                // Line path
                val path = Path()
                history.forEachIndexed { index, value ->
                    val x = index * stepX
                    val y = padY + (size.height - 2 * padY) *
                            (1f - (value - minVal) / range)
                    if (index == 0) path.moveTo(x, y)
                    else            path.lineTo(x, y)
                }

                // Filled area under line
                val fillPath = Path().apply {
                    addPath(path)
                    lineTo((history.size - 1) * stepX, size.height)
                    lineTo(0f, size.height)
                    close()
                }
                drawPath(
                    path  = fillPath,
                    color = color.copy(alpha = 0.15f)
                )

                // Main line
                drawPath(
                    path   = path,
                    color  = color,
                    style  = Stroke(width = 3f)
                )

                // Latest value dot
                val lastX = (history.size - 1) * stepX
                val lastY = padY + (size.height - 2 * padY) *
                        (1f - (history.last() - minVal) / range)
                drawCircle(
                    color  = color,
                    radius = 6f,
                    center = Offset(lastX, lastY)
                )
                drawCircle(
                    color  = Color.Black,
                    radius = 3f,
                    center = Offset(lastX, lastY)
                )
            }
        }
    }
}

// ── Connection bar ────────────────────────────────────────────────────────────
@Composable
fun ConnectionBar(
    bleState:     BleState,
    onConnect:    () -> Unit,
    onDisconnect: () -> Unit,
    onSyncTime:   () -> Unit
) {
    val statusColor = when (bleState) {
        BleState.CONNECTED              -> Color(0xFF66BB6A)
        BleState.CONNECTING,
        BleState.SCANNING               -> Color(0xFFFFCA28)
        BleState.DISCONNECTED           -> Color(0xFFEF5350)
    }
    val statusText = when (bleState) {
        BleState.CONNECTED    -> "Connected"
        BleState.CONNECTING   -> "Connecting..."
        BleState.SCANNING     -> "Scanning..."
        BleState.DISCONNECTED -> "Disconnected"
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF1E1E1E)),
        shape    = RoundedCornerShape(12.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment         = Alignment.CenterVertically,
            horizontalArrangement     = Arrangement.SpaceBetween
        ) {
            Row(
                verticalAlignment     = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .background(statusColor, shape = RoundedCornerShape(50))
                )
                Text(text = statusText, color = Color.White, fontSize = 14.sp)
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (bleState == BleState.DISCONNECTED) {
                    Button(
                        onClick = onConnect,
                        colors  = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFF42A5F5))
                    ) { Text("Connect") }
                } else if (bleState == BleState.CONNECTED) {
                    Button(
                        onClick = onSyncTime,
                        colors  = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFF66BB6A))
                    ) { Text("Sync Time") }
                    Button(
                        onClick = onDisconnect,
                        colors  = ButtonDefaults.buttonColors(
                            containerColor = Color(0xFFEF5350))
                    ) { Text("Disconnect") }
                }
            }
        }
    }
}

// ── Metric card ───────────────────────────────────────────────────────────────
@Composable
fun MetricCard(
    modifier: Modifier = Modifier,
    label:    String,
    value:    String,
    unit:     String,
    color:    Color
) {
    Card(
        modifier = modifier,
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF1E1E1E)),
        shape    = RoundedCornerShape(12.dp)
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(text = label, color = Color.Gray,  fontSize = 12.sp)
            Text(text = value, color = color,
                fontSize = 32.sp, fontWeight = FontWeight.Bold)
            Text(text = unit,  color = Color.Gray,  fontSize = 12.sp)
        }
    }
}

// ── IMU card ──────────────────────────────────────────────────────────────────
@Composable
fun ImuCard(data: HealthData) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF1E1E1E)),
        shape    = RoundedCornerShape(12.dp)
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(text = "IMU", color = Color.Gray, fontSize = 12.sp)

            Text(text = "Accelerometer (g)", color = Color.White,
                fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                ImuValue(label = "X", value = data.accelX, modifier = Modifier.weight(1f))
                ImuValue(label = "Y", value = data.accelY, modifier = Modifier.weight(1f))
                ImuValue(label = "Z", value = data.accelZ, modifier = Modifier.weight(1f))
            }

            HorizontalDivider(color = Color(0xFF333333))

            Text(text = "Gyroscope (dps)", color = Color.White,
                fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                ImuValue(label = "X", value = data.gyroX, modifier = Modifier.weight(1f))
                ImuValue(label = "Y", value = data.gyroY, modifier = Modifier.weight(1f))
                ImuValue(label = "Z", value = data.gyroZ, modifier = Modifier.weight(1f))
            }
        }
    }
}

@Composable
fun ImuValue(label: String, value: Float, modifier: Modifier = Modifier) {
    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(text = label, color = Color.Gray, fontSize = 11.sp)
        Text(
            text       = "%+.2f".format(value),
            color      = Color(0xFFCE93D8),
            fontSize   = 13.sp,
            fontWeight = FontWeight.Medium
        )
    }
}