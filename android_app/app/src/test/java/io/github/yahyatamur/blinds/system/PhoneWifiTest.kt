package io.github.yahyatamur.blinds.system

import io.github.yahyatamur.blinds.system.PhoneWifi.Ssid
import org.junit.Assert.assertEquals
import org.junit.Test

class PhoneWifiTest {
    @Test fun parsesWifiInfoNames() {
        assertEquals(Ssid.On("Blinds-Net"), PhoneWifi.parseSsid("\"Blinds-Net\"", canKnow = true))
        assertEquals(Ssid.On("e4b8ad"), PhoneWifi.parseSsid("e4b8ad", canKnow = true))   // non-UTF-8 names come as hex
        assertEquals(Ssid.Unknown, PhoneWifi.parseSsid("<unknown ssid>", canKnow = true))
        assertEquals(Ssid.Unknown, PhoneWifi.parseSsid("\"Home\"", canKnow = false))
        assertEquals(Ssid.Unknown, PhoneWifi.parseSsid(null, canKnow = true))
    }
}
