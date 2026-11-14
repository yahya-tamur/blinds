package io.github.yahyatamur.blinds.system

import android.app.ActivityOptions
import android.app.AlarmManager
import android.app.PendingIntent
import android.content.ActivityNotFoundException
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.provider.AlarmClock
import androidx.core.content.ContextCompat
import java.time.LocalTime

// The phone's clock app, through the public AlarmClock intents. Android gives apps no way to list, edit
// or delete clock alarms, and SET_ALARM returns no ID, so the app only ever adds one; the user removes it
// in the clock app. Must be started from the foreground (an Activity context).
object ClockAlarms {

    // A one-time alarm at the next occurrence of `time` (no days = one-time; there is no date extra).
    // Returns false when no clock app takes the intent.
    fun setOneTime(context: Context, time: LocalTime, label: String): Boolean {
        val intent = Intent(AlarmClock.ACTION_SET_ALARM)
            .putExtra(AlarmClock.EXTRA_HOUR, time.hour)
            .putExtra(AlarmClock.EXTRA_MINUTES, time.minute)
            .putExtra(AlarmClock.EXTRA_MESSAGE, label)
            .putExtra(AlarmClock.EXTRA_SKIP_UI, true)   // set it without opening the clock app
        return try {
            context.startActivity(intent)
            true
        } catch (_: ActivityNotFoundException) {
            false
        }
    }

    // The phone's next alarm clock (from any app, usually the clock app): only its time and an intent that
    // opens it. Android doesn't say which app set it or what its label is.
    data class Next(val triggerMs: Long, val show: PendingIntent?)

    fun next(context: Context): Next? =
        context.getSystemService(AlarmManager::class.java).nextAlarmClock?.let { Next(it.triggerTime, it.showIntent) }

    // Calls onChange whenever the next alarm clock changes (set, removed, snoozed, dismissed) until the
    // returned function is called.
    fun watchNext(context: Context, onChange: () -> Unit): () -> Unit {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) = onChange()
        }
        ContextCompat.registerReceiver(context, receiver, IntentFilter(AlarmManager.ACTION_NEXT_ALARM_CLOCK_CHANGED),
                                       ContextCompat.RECEIVER_NOT_EXPORTED)   // a system broadcast still arrives
        return { runCatching { context.unregisterReceiver(receiver) } }
    }

    // Opens whatever set the next alarm (its showIntent), or the clock app's alarm list.
    fun open(context: Context, next: Next?) {
        val show = next?.show ?: return showAlarms(context)
        try {
            // Android 14+: our app is in front, so let the other app's PendingIntent start its activity
            val options = if (Build.VERSION.SDK_INT >= 34) ActivityOptions.makeBasic().apply {
                @Suppress("DEPRECATION")
                setPendingIntentBackgroundActivityStartMode(ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED)
            }.toBundle() else null
            show.send(context, 0, null, null, null, null, options)
        } catch (_: PendingIntent.CanceledException) {
            showAlarms(context)
        }
    }

    // Opens the clock app's alarm list, where the user edits or removes alarms.
    fun showAlarms(context: Context) {
        try {
            context.startActivity(Intent(AlarmClock.ACTION_SHOW_ALARMS))
        } catch (_: ActivityNotFoundException) {
        }
    }
}
