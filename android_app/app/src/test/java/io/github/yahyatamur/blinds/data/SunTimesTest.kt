package io.github.yahyatamur.blinds.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Duration
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime

class SunTimesTest {
    private val berlin = ZoneId.of("Europe/Berlin")

    private fun near(expected: ZonedDateTime, actual: ZonedDateTime?, minutes: Long = 3) {
        assertTrue("expected about $expected, got $actual",
            actual != null && Duration.between(expected, actual).abs().toMinutes() <= minutes)
    }

    @Test fun berlinInNovember() {
        // Berlin (52.5 N, 13.4 E), 14 November 2026: NOAA's solar calculator gives sunrise 7:25, sunset 16:16 (CET)
        val d = LocalDate.of(2026, 11, 14)
        near(ZonedDateTime.of(2026, 11, 14, 7, 25, 0, 0, berlin), SunTimes.at(d, 52.5, 13.4, SunTimes.Event.SUNRISE)?.atZone(berlin))
        near(ZonedDateTime.of(2026, 11, 14, 16, 16, 0, 0, berlin), SunTimes.at(d, 52.5, 13.4, SunTimes.Event.SUNSET)?.atZone(berlin))
    }

    @Test fun nextIsTomorrowOnceTodaysHasPassed() {
        val evening = ZonedDateTime.of(2026, 11, 14, 20, 0, 0, 0, berlin)
        val rise = SunTimes.next(evening, 52.5, 13.4, SunTimes.Event.SUNRISE)!!
        assertEquals(LocalDate.of(2026, 11, 15), rise.toLocalDate())
        val set = SunTimes.next(evening, 52.5, 13.4, SunTimes.Event.SUNSET)!!
        assertEquals(LocalDate.of(2026, 11, 15), set.toLocalDate())
    }

    @Test fun polarNight() {
        // Tromsø-ish latitude in December: the sun doesn't rise
        assertNull(SunTimes.at(LocalDate.of(2026, 12, 21), 69.6, 18.9, SunTimes.Event.SUNRISE))
    }

    @Test fun aroundTwoAlarmsInTheMorning() {
        // Alarms tomorrow at 6:50 (blinds) and 7:00 (phone), before sunrise: today's sunset, then tomorrow's sunrise
        val a = SunTimes.around(ZonedDateTime.of(2026, 11, 15, 6, 50, 0, 0, berlin),
                                ZonedDateTime.of(2026, 11, 15, 7, 0, 0, 0, berlin), 52.5, 13.4)
        assertEquals(listOf(SunTimes.Event.SUNSET, SunTimes.Event.SUNRISE), a.map { it.event })
        assertEquals(LocalDate.of(2026, 11, 14), a[0].at.toLocalDate())
        assertEquals(LocalDate.of(2026, 11, 15), a[1].at.toLocalDate())
    }

    @Test fun sunriseBetweenTheAlarms() {
        // Blinds 7:15, phone 7:45, sunrise about 7:27 in between: sunset before, sunrise between, sunset after
        val a = SunTimes.around(ZonedDateTime.of(2026, 11, 15, 7, 15, 0, 0, berlin),
                                ZonedDateTime.of(2026, 11, 15, 7, 45, 0, 0, berlin), 52.5, 13.4)
        assertEquals(listOf(SunTimes.Event.SUNSET, SunTimes.Event.SUNRISE, SunTimes.Event.SUNSET), a.map { it.event })
        assertTrue(a[1].at.isAfter(ZonedDateTime.of(2026, 11, 15, 7, 15, 0, 0, berlin)))
        assertEquals(LocalDate.of(2026, 11, 15), a[2].at.toLocalDate())
    }
}
