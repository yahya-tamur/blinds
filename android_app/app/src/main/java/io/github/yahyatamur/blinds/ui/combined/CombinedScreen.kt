package io.github.yahyatamur.blinds.ui.combined

import android.text.format.DateFormat
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.yahyatamur.blinds.data.Settings
import io.github.yahyatamur.blinds.data.SunTimes
import io.github.yahyatamur.blinds.data.api.BlindsApi
import io.github.yahyatamur.blinds.data.api.Direction
import io.github.yahyatamur.blinds.data.api.NewAlarm
import io.github.yahyatamur.blinds.system.ClockAlarms
import io.github.yahyatamur.blinds.ui.theme.PageColors
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.time.LocalTime
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter

// The "Combined" tab: one action sets a phone alarm in the clock app and a one-time blinds alarm at an
// offset from it (e.g. the blinds go up 10 minutes before the phone rings). The phone's next alarm is listed
// with the blinds alarms on the controller's page, where the app goes after setting one.
// Blinds-only alarms are set on the controller's page (the Blinds tab).
@Composable
fun CombinedScreen(
    api: BlindsApi,
    settings: Settings,
    location: io.github.yahyatamur.blinds.data.api.Location?,   // the controller's, for sunrise and sunset
    statusKnown: Boolean,             // the app has heard from the controller (only then is "no location" true)
    snackbar: SnackbarHostState,
    onSet: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val saved = remember { settings.combinedForm }
    var minuteOfDay by rememberSaveable { mutableIntStateOf(saved.minuteOfDay) }
    var up by rememberSaveable { mutableStateOf(saved.up) }
    var offset by rememberSaveable { mutableIntStateOf(saved.blindsOffset) }
    var picking by remember { mutableStateOf(false) }
    var sending by remember { mutableStateOf(false) }
    // "Now" moves on, so the summary (today/tomorrow, blinds time already past) stays right
    var now by remember { mutableStateOf(ZonedDateTime.now()) }
    LaunchedEffect(Unit) { while (true) { delay(10_000); now = ZonedDateTime.now() } }

    val time = LocalTime.of(minuteOfDay / 60, minuteOfDay % 60)
    val is24 = DateFormat.is24HourFormat(context)
    val fmt = DateTimeFormatter.ofPattern(if (is24) "H:mm" else "h:mm a")
    val plan = CombinedPlan.of(now, time, offset)
    val arrow = if (up) "↑" else "↓"

    fun day(t: ZonedDateTime) = when (java.time.temporal.ChronoUnit.DAYS.between(now.toLocalDate(), t.toLocalDate())) {
        0L -> "today"
        1L -> "tomorrow"
        -1L -> "yesterday"
        else -> t.dayOfWeek.getDisplayName(java.time.format.TextStyle.FULL, java.util.Locale.getDefault())
    }

    // The blinds alarm first: if the controller can't take it, no phone alarm is left behind on its own
    fun submit() = scope.launch {
        val p = CombinedPlan.of(ZonedDateTime.now(), time, offset) as? CombinedPlan.Ok ?: return@launch
        sending = true
        val result = api.addAlarm(NewAlarm.Once(if (up) Direction.UP else Direction.DOWN, p.blindsAt.toEpochSecond()))
        sending = false
        result.exceptionOrNull()?.let { e ->
            snackbar.showSnackbar(
                if (e is BlindsApi.ApiException) "The controller refused the blinds alarm: its list may be full or its clock not set yet. No phone alarm was set."
                else "Can't reach the controller, so nothing was set. Is the app connected to its Wi-Fi?",
                withDismissAction = true, duration = SnackbarDuration.Long,
            )
            return@launch
        }
        settings.combinedForm = Settings.CombinedForm(minuteOfDay, up, offset)
        val label = "Blinds $arrow ${fmt.format(p.blindsAt)}"
        val msg = if (ClockAlarms.setOneTime(context, p.clockAt.toLocalTime(), label))
            "Alarm ${day(p.clockAt)} at ${fmt.format(p.clockAt)}, blinds $arrow at ${fmt.format(p.blindsAt)}: set."
        else "Blinds $arrow at ${fmt.format(p.blindsAt)}: set. No clock app took the phone alarm."
        onSet()   // to the Blinds tab, where the new alarms are listed with the phone's next alarm
        snackbar.showSnackbar(msg, withDismissAction = true, duration = SnackbarDuration.Long)
    }

    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        WakeCard {
            CardTitle("Combined alarm")

            // The phone alarm's time: tap to change
            Text(
                fmt.format(time),
                fontSize = 56.sp,
                fontWeight = FontWeight.Light,
                color = PageColors.text,
                modifier = Modifier.fillMaxWidth().clickable { picking = true }.padding(vertical = 4.dp),
            )

            // The blinds: direction, and when relative to the alarm
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                SegmentedButton(
                    selected = up, onClick = { up = true },
                    shape = SegmentedButtonDefaults.itemShape(0, 2),
                    colors = SegmentedButtonDefaults.colors(activeContainerColor = PageColors.up, activeContentColor = PageColors.onUp),
                    icon = {},
                ) { Text("▲ Up") }
                SegmentedButton(
                    selected = !up, onClick = { up = false },
                    shape = SegmentedButtonDefaults.itemShape(1, 2),
                    colors = SegmentedButtonDefaults.colors(activeContainerColor = PageColors.down, activeContentColor = PageColors.text),
                    icon = {},
                ) { Text("▼ Down") }
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                Step("−5") { offset = (offset - 5).coerceIn(-OFFSET_MAX, OFFSET_MAX) }
                Step("−1") { offset = (offset - 1).coerceIn(-OFFSET_MAX, OFFSET_MAX) }
                Text(offsetText(offset), color = PageColors.text, modifier = Modifier.weight(1f).padding(horizontal = 4.dp),
                     style = MaterialTheme.typography.bodyLarge, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
                Step("+1") { offset = (offset + 1).coerceIn(-OFFSET_MAX, OFFSET_MAX) }
                Step("+5") { offset = (offset + 5).coerceIn(-OFFSET_MAX, OFFSET_MAX) }
            }

            // What will happen, in time order with the sunrises and sunsets around it, or why it can't
            if (plan is CombinedPlan.Ok) {
                data class Line(val at: ZonedDateTime, val text: String, val alarm: Boolean)
                val lines = mutableListOf(
                    Line(plan.clockAt, "Phone alarm ${day(plan.clockAt)} at ${fmt.format(plan.clockAt)}", true),
                    Line(plan.blindsAt, "Blinds ${if (up) "up" else "down"} ${day(plan.blindsAt)} at ${fmt.format(plan.blindsAt)}", true),
                )
                location?.let { loc ->
                    val (first, last) = listOf(plan.clockAt, plan.blindsAt).sorted().let { it[0] to it[1] }
                    for (o in SunTimes.around(first, last, loc.lat, loc.lon)) {
                        val name = if (o.event == SunTimes.Event.SUNRISE) "Sunrise" else "Sunset"
                        lines += Line(o.at, "$name ${day(o.at)} at ${fmt.format(o.at)}", false)
                    }
                }
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    for (l in lines.sortedBy { it.at }) {
                        Text(l.text, color = if (l.alarm) PageColors.text else PageColors.muted,
                             style = MaterialTheme.typography.bodyMedium)
                    }
                    if (statusKnown && location == null) {
                        Text("Set the location on the Blinds tab to see sunrise and sunset here.",
                             color = PageColors.muted, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
            val problem = when (plan) {
                is CombinedPlan.Ok -> null
                is CombinedPlan.BlindsInPast -> "The blinds time (${fmt.format(plan.blindsAt)}) has already passed. " +
                    "Pick a later alarm or move the blinds closer to it."
                is CombinedPlan.BlindsTooFar -> "The blinds would move ${day(plan.blindsAt)} at ${fmt.format(plan.blindsAt)}, more " +
                    "than 24 hours from now; the controller takes one-time alarms up to 24 hours ahead."
            }
            problem?.let { Text(it, color = PageColors.dangerText, style = MaterialTheme.typography.bodyMedium) }

            Button(
                onClick = { submit() },
                enabled = plan is CombinedPlan.Ok && !sending,
                modifier = Modifier.fillMaxWidth().height(52.dp),
            ) { Text(if (sending) "Setting…" else "Set alarm", style = MaterialTheme.typography.titleMedium) }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("The phone alarm goes into the Clock app; change or remove it there.",
                     color = PageColors.muted, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
                TextButton(onClick = { ClockAlarms.showAlarms(context) }) { Text("Open Clock") }
            }
        }
    }

    if (picking) TimeDialog(time, is24, onDismiss = { picking = false }) { picked ->
        minuteOfDay = picked.hour * 60 + picked.minute
        picking = false
    }
}

private const val OFFSET_MAX = 120

private fun offsetText(m: Int) = when {
    m > 0 -> "$m min after"     // the "Blinds" label above says what moves; this is relative to the alarm
    m < 0 -> "${-m} min before"
    else -> "with the alarm"
}

@Composable
internal fun WakeCard(content: @Composable () -> Unit) {
    Surface(color = PageColors.card, shape = RoundedCornerShape(16.dp), modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) { content() }
    }
}

// Like the page's card headings (h2 in style.css)
@Composable
internal fun CardTitle(text: String) {
    Text(text.uppercase(), color = PageColors.muted, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
}

@Composable
private fun Step(label: String, onClick: () -> Unit) {
    OutlinedButton(onClick = onClick, contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp),
                   modifier = Modifier.widthIn(min = 48.dp)) { Text(label) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TimeDialog(initial: LocalTime, is24: Boolean, onDismiss: () -> Unit, onPick: (LocalTime) -> Unit) {
    val state = rememberTimePickerState(initial.hour, initial.minute, is24)
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = { onPick(LocalTime.of(state.hour, state.minute)) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
        text = { Column { Spacer(Modifier.height(8.dp)); TimePicker(state = state) } },
    )
}
