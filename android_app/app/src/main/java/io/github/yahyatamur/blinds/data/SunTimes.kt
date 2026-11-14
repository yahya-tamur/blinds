package io.github.yahyatamur.blinds.data

import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime
import kotlin.math.acos
import kotlin.math.cos
import kotlin.math.roundToLong
import kotlin.math.sin
import kotlin.math.sqrt

// Sunrise and sunset, the same calculation as the controller (firmware/main/sun.c, sun_time), so the app shows
// the times the controller's sun alarms use. Plain Kotlin, unit-tested.
object SunTimes {
    enum class Event { SUNRISE, SUNSET }

    private const val RAD = Math.PI / 180

    // The moment of the event on that local date, or null when the sun doesn't rise or set that day.
    fun at(date: LocalDate, lat: Double, lon: Double, event: Event): Instant? {
        // Whole days from J2000 (2000-01-01 12:00 UTC) to noon UTC on this date, then to local solar noon
        val n = date.toEpochDay() - 10957.0
        val j = n - lon / 360
        val m = (357.5291 + 0.98560028 * j).mod(360.0)                                    // mean anomaly
        val c = 1.9148 * sin(m * RAD) + 0.0200 * sin(2 * m * RAD) + 0.0003 * sin(3 * m * RAD)
        val l = (m + c + 180 + 102.9372).mod(360.0)                                       // ecliptic longitude
        val transit = j + 0.0053 * sin(m * RAD) - 0.0069 * sin(2 * l * RAD)              // days from J2000
        val sinD = sin(l * RAD) * sin(23.4397 * RAD)
        val cosD = sqrt(1 - sinD * sinD)
        val h0 = -0.833   // the top of the sun on the horizon, with refraction
        val cosW = (sin(h0 * RAD) - sin(lat * RAD) * sinD) / (cos(lat * RAD) * cosD)
        if (cosW < -1 || cosW > 1) return null
        val w = acos(cosW) / RAD / 360                                                    // half the day length, in days
        val t = if (event == Event.SUNRISE) transit - w else transit + w
        return Instant.ofEpochSecond(((t + 10957.5) * 86400).roundToLong())
    }

    data class Occurrence(val event: Event, val at: ZonedDateTime)

    // The sun events around a span (the two parts of a combined alarm): the last sunrise or sunset before
    // `start`, any between `start` and `end`, and the first after `end`. In start's time zone, in time order.
    fun around(start: ZonedDateTime, end: ZonedDateTime, lat: Double, lon: Double): List<Occurrence> {
        val zone = start.zone
        val all = (-1L..(java.time.temporal.ChronoUnit.DAYS.between(start.toLocalDate(), end.toLocalDate()) + 1)).flatMap { d ->
            Event.entries.mapNotNull { e -> at(start.toLocalDate().plusDays(d), lat, lon, e)?.let { Occurrence(e, it.atZone(zone)) } }
        }.sortedBy { it.at }
        val before = all.lastOrNull { !it.at.isAfter(start) }
        val between = all.filter { it.at.isAfter(start) && it.at.isBefore(end) }
        val after = all.firstOrNull { !it.at.isBefore(end) }
        return listOfNotNull(before) + between + listOfNotNull(after)
    }

    // The next time the event happens after `now` (today's or tomorrow's), in now's time zone.
    fun next(now: ZonedDateTime, lat: Double, lon: Double, event: Event): ZonedDateTime? {
        val zone: ZoneId = now.zone
        for (d in 0L..1L) {
            val t = at(now.toLocalDate().plusDays(d), lat, lon, event) ?: continue
            val z = t.atZone(zone)
            if (z.isAfter(now)) return z
        }
        return null
    }
}
