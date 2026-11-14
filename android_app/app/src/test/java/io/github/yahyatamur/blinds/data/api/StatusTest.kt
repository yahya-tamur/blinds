package io.github.yahyatamur.blinds.data.api

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class StatusTest {
    private fun load(name: String) = javaClass.classLoader!!.getResource(name)!!.readText()

    @Test fun parsesMockServerStatus() {
        val s = BlindsApi.json.decodeFromString<Status>(load("status_mock.json"))
        assertEquals(20, s.maxAlarms)
        assertEquals(Location(52.5, 13.4), s.location)
        assertEquals(3, s.alarms.size)
        val (rec, once, sun) = s.alarms
        assertEquals(420, rec.minute)
        assertEquals("once", once.kind); assertEquals(once.at, once.next)
        assertEquals("sunset", sun.event); assertEquals(-15, sun.offset)
        assertNull(s.stats!!.beforeReset)   // the mock doesn't send it
    }

    @Test fun parsesNullsAndUnknownFields() {
        val s = BlindsApi.json.decodeFromString<Status>(load("status_board_nulls.json"))
        assertNull(s.seconds); assertNull(s.today); assertNull(s.sun); assertNull(s.driver)
        assertTrue(s.moving)
        assertEquals(12, s.stopTop); assertNull(s.stopBottom)
        assertNull(s.alarms.single().next)
        assertNull(s.stats!!.rssi)
        assertEquals("run up 40%", s.stats!!.beforeReset)
        assertEquals("I app: \"quoted\" line", s.log.single())
    }

    @Test fun removeFieldsMatchThePage() {
        assertEquals(mapOf("k" to "once", "at" to "1794670980", "d" to "down"),
            Alarm(kind = "once", dir = "down", at = 1794670980).removeFields())
        assertEquals(mapOf("k" to "sun", "e" to "dusk", "o" to "-30", "d" to "up"),
            Alarm(kind = "sun", dir = "up", event = "dusk", offset = -30).removeFields())
        assertEquals(mapOf("k" to "recurring", "m" to "0", "d" to "up"),
            Alarm(kind = "recurring", dir = "up", minute = 0).removeFields())
    }

    @Test fun newAlarmFields() {
        assertEquals(mapOf("d" to "up", "k" to "once", "at" to "1794670980"),
            NewAlarm.Once(Direction.UP, 1794670980).fields())
        assertEquals(mapOf("d" to "down", "k" to "sun", "e" to "sunrise", "o" to "180"),
            NewAlarm.Sun(Direction.DOWN, SunEvent.SUNRISE, 180).fields())
        assertFalse(runCatching { NewAlarm.Once(Direction.UP, 1794670981) }.isSuccess)
    }
}
