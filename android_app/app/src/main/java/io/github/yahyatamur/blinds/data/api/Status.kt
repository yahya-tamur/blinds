package io.github.yahyatamur.blinds.data.api

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

// The JSON of GET /api/status (firmware/main/web.c, api_status). Times are the controller's
// local time; fields the firmware sends as null are nullable, and fields an older firmware or
// the mock server leaves out have defaults.

@Serializable
data class Status(
    val seconds: Int? = null,               // local time of day in seconds; null until the clock is set
    val dir: String = "",                   // "up", "down", or "" when idle
    @SerialName("remaining_ms") val remainingMs: Long = 0,
    @SerialName("total_ms") val totalMs: Long = 0,
    val jog: Boolean = false,
    val pos: Int = 0,                       // Hall counts
    @SerialName("stop_top") val stopTop: Int? = null,
    @SerialName("stop_bottom") val stopBottom: Int? = null,
    val irms: Double = 0.0,
    val mode: String = "",                  // "spread" or "stealth"
    @SerialName("speed_up") val speedUp: Double = 0.0,
    @SerialName("speed_down") val speedDown: Double = 0.0,
    val driver: Driver? = null,
    val today: String? = null,              // "YYYY-MM-DD", local
    val location: Location? = null,
    val sun: SunTimes? = null,
    @SerialName("max_alarms") val maxAlarms: Int = 0,
    val alarms: List<Alarm> = emptyList(),
    val stats: Stats? = null,
    @SerialName("log_next") val logNext: Long = 0,
    val log: List<String> = emptyList(),
) {
    val moving: Boolean get() = dir.isNotEmpty()
}

@Serializable
data class Driver(
    val current: Int,
    @SerialName("open_a") val openA: Boolean,
    @SerialName("open_b") val openB: Boolean,
    val fault: Boolean,
)

@Serializable
data class Location(val lat: Double, val lon: Double)

// Local minutes since midnight; null when the sun doesn't reach that angle today.
@Serializable
data class SunTimes(
    val dawn: Int? = null,
    val sunrise: Int? = null,
    val sunset: Int? = null,
    val dusk: Int? = null,
)

@Serializable
data class Alarm(
    val kind: String,                       // "recurring", "once" or "sun"
    val dir: String,                        // "up" or "down"
    val minute: Int? = null,                // recurring: local minutes since midnight
    val at: Long? = null,                   // once: Unix time
    val event: String? = null,              // sun: dawn, sunrise, sunset, dusk
    val offset: Int? = null,                // sun: minutes, -180..180
    val next: Long? = null,                 // Unix time of the next run; null if not known
    val `when`: String? = null,             // the same as "YYYY-MM-DD HH:MM", local
) {
    // The fields /api/alarms/remove needs: the same as the page's alarmFields().
    fun removeFields(): Map<String, String> = when (kind) {
        "once" -> mapOf("k" to "once", "at" to at.toString(), "d" to dir)
        "sun" -> mapOf("k" to "sun", "e" to event.orEmpty(), "o" to offset.toString(), "d" to dir)
        else -> mapOf("k" to "recurring", "m" to minute.toString(), "d" to dir)
    }
}

@Serializable
data class Stats(
    @SerialName("uptime_s") val uptimeS: Long = 0,
    val heap: Long = 0,
    @SerialName("heap_min") val heapMin: Long = 0,
    @SerialName("heap_largest") val heapLargest: Long = 0,
    val rssi: Int? = null,
    val updating: Boolean = false,
    val reset: String? = null,
    @SerialName("before_reset") val beforeReset: String? = null,
    val version: String? = null,
)
