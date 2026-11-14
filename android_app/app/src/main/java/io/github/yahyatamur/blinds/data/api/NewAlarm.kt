package io.github.yahyatamur.blinds.data.api

// An alarm to add with POST /api/alarms/add. Adding one in a slot that is already used replaces it.

enum class Direction(val wire: String) { UP("up"), DOWN("down") }

enum class SunEvent(val wire: String) { DAWN("dawn"), SUNRISE("sunrise"), SUNSET("sunset"), DUSK("dusk") }

sealed interface NewAlarm {
    val dir: Direction
    fun fields(): Map<String, String>

    // Every day at a local time.
    data class Recurring(override val dir: Direction, val minuteOfDay: Int) : NewAlarm {
        init { require(minuteOfDay in 0 until 24 * 60) }
        override fun fields() = mapOf("d" to dir.wire, "k" to "recurring", "m" to minuteOfDay.toString())
    }

    // Once, at a Unix time on a whole minute, within the next 24 h (the firmware refuses others).
    data class Once(override val dir: Direction, val at: Long) : NewAlarm {
        init { require(at % 60 == 0L) { "at must be a whole minute" } }
        override fun fields() = mapOf("d" to dir.wire, "k" to "once", "at" to at.toString())
    }

    // At a sun event plus an offset in minutes (needs a location on the controller).
    data class Sun(override val dir: Direction, val event: SunEvent, val offsetMinutes: Int) : NewAlarm {
        init { require(offsetMinutes in -180..180) }
        override fun fields() =
            mapOf("d" to dir.wire, "k" to "sun", "e" to event.wire, "o" to offsetMinutes.toString())
    }
}
