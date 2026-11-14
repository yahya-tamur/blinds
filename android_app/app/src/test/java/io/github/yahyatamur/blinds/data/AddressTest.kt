package io.github.yahyatamur.blinds.data

import org.junit.Assert.assertEquals
import org.junit.Test

class AddressTest {
    @Test fun pageUrl() {
        assertEquals("http://blinds.lan/", controllerPageUrl("blinds.lan"))
        assertEquals("http://10.0.2.2:8766/", controllerPageUrl(" 10.0.2.2:8766/ "))
        assertEquals("https://x.test/", controllerPageUrl("https://x.test"))
    }
}
