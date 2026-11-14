package io.github.yahyatamur.blinds.ui.combined

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalTime
import java.time.ZoneId
import java.time.ZonedDateTime

class CombinedPlanTest {
    private val zone = ZoneId.of("Europe/Berlin")
    private fun at(y: Int, mo: Int, d: Int, h: Int, mi: Int, s: Int = 0) = ZonedDateTime.of(y, mo, d, h, mi, s, 0, zone)
    private fun t(h: Int, m: Int) = LocalTime.of(h, m)

    @Test fun blindsBeforeTomorrowsAlarm() {
        val p = CombinedPlan.of(at(2026, 11, 14, 22, 30, 12), t(7, 0), -10) as CombinedPlan.Ok
        assertEquals(at(2026, 11, 15, 7, 0), p.clockAt)
        assertEquals(at(2026, 11, 15, 6, 50), p.blindsAt)
        assertEquals(0L, p.blindsAt.toEpochSecond() % 60)   // the firmware wants a whole minute
    }

    @Test fun laterToday() {
        val p = CombinedPlan.of(at(2026, 11, 14, 6, 0), t(7, 0), 5) as CombinedPlan.Ok
        assertEquals(at(2026, 11, 14, 7, 0), p.clockAt)
        assertEquals(at(2026, 11, 14, 7, 5), p.blindsAt)
    }

    @Test fun theCurrentMinuteMeansTomorrow() {
        val p = CombinedPlan.of(at(2026, 11, 14, 7, 0, 30), t(7, 0), 0) as CombinedPlan.Ok
        assertEquals(at(2026, 11, 15, 7, 0), p.clockAt)
    }

    @Test fun blindsTimeAlreadyPast() {
        // 6:55 now, alarm 7:00, blinds 10 min earlier = 6:50, already gone
        assertTrue(CombinedPlan.of(at(2026, 11, 14, 6, 55), t(7, 0), -10) is CombinedPlan.BlindsInPast)
    }

    @Test fun blindsMoreThanADayAhead() {
        // 7:00 now, alarm 6:50 tomorrow, blinds 15 min after = 7:05 tomorrow: the controller refuses it
        assertTrue(CombinedPlan.of(at(2026, 11, 14, 7, 0), t(6, 50), 15) is CombinedPlan.BlindsTooFar)
    }

    @Test fun blindsAcrossMidnight() {
        val p = CombinedPlan.of(at(2026, 11, 14, 20, 0), t(0, 5), -10) as CombinedPlan.Ok
        assertEquals(at(2026, 11, 15, 0, 5), p.clockAt)
        assertEquals(at(2026, 11, 14, 23, 55), p.blindsAt)
    }

    @Test fun springForwardGap() {
        // 29 March 2026, 02:00-03:00 doesn't exist in Berlin: an alarm at 02:30 rings at 03:30
        val p = CombinedPlan.of(at(2026, 3, 28, 22, 0), t(2, 30), 0) as CombinedPlan.Ok
        assertEquals(at(2026, 3, 29, 3, 30), p.clockAt)
    }

    @Test fun fallBackDayCanPushBlindsPast24Hours() {
        // 25 October 2026 has 25 hours in Berlin: from Saturday 7:30, Sunday 7:00 is 24.5 real hours away
        assertTrue(CombinedPlan.of(at(2026, 10, 24, 7, 30), t(7, 0), 0) is CombinedPlan.BlindsTooFar)
        assertTrue(CombinedPlan.of(at(2026, 10, 24, 7, 30), t(7, 0), -60) is CombinedPlan.Ok)
    }
}
