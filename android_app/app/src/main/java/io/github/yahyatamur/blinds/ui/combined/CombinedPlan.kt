package io.github.yahyatamur.blinds.ui.combined

import java.time.Duration
import java.time.LocalTime
import java.time.ZonedDateTime
import java.time.temporal.ChronoUnit

// When a combined alarm runs: the phone's clock alarm at the next occurrence of a time of day, and the
// blinds `blindsOffsetMinutes` before (negative) or after it. Plain Kotlin, so it is unit-tested.
//
// The clock alarm is only an hour and minute (AlarmClock.ACTION_SET_ALARM has no date): the clock app rings
// at its next occurrence, which is what this plan uses too. The blinds alarm is a moment (Unix time), so
// the controller's time zone doesn't matter; the controller takes it only in the future and within 24 hours.
sealed interface CombinedPlan {
    data class Ok(val clockAt: ZonedDateTime, val blindsAt: ZonedDateTime) : CombinedPlan
    data class BlindsInPast(val clockAt: ZonedDateTime, val blindsAt: ZonedDateTime) : CombinedPlan   // 6:55 now, alarm 7:00, blinds -10
    data class BlindsTooFar(val clockAt: ZonedDateTime, val blindsAt: ZonedDateTime) : CombinedPlan   // 7:00 now, alarm 6:50, blinds +15

    companion object {
        const val MAX_AHEAD_S = 24 * 3600L   // ONCE_MAX_AHEAD in firmware/main/alarms.c

        fun of(now: ZonedDateTime, alarmTime: LocalTime, blindsOffsetMinutes: Int): CombinedPlan {
            val clockAt = nextOccurrence(now, alarmTime)
            val blindsAt = clockAt.plusMinutes(blindsOffsetMinutes.toLong())
            if (!blindsAt.isAfter(now)) return BlindsInPast(clockAt, blindsAt)
            if (Duration.between(now, blindsAt).seconds > MAX_AHEAD_S) return BlindsTooFar(clockAt, blindsAt)
            return Ok(clockAt, blindsAt)
        }

        // The next moment after `now` (later than its current minute) when the local clock shows `time`.
        // Uses ZonedDateTime, so DST gaps move forward and overlaps take the earlier instant.
        fun nextOccurrence(now: ZonedDateTime, time: LocalTime): ZonedDateTime {
            val t = time.truncatedTo(ChronoUnit.MINUTES)
            val today = now.with(t).truncatedTo(ChronoUnit.MINUTES)
            return if (today.isAfter(now.truncatedTo(ChronoUnit.MINUTES))) today
                   else now.plusDays(1).with(t).truncatedTo(ChronoUnit.MINUTES)
        }
    }
}
