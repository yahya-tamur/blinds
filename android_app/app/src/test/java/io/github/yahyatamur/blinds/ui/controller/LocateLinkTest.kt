package io.github.yahyatamur.blinds.ui.controller

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LocateLinkTest {
    @Test fun findsTheReturnPageOfTheGithubLink() {
        // What the page's button opens (firmware/web/index.html, LOCATE_URL + "#return=" + encodeURIComponent(origin + "/"))
        assertEquals("http://blinds.lan/",
            LocateLink.returnUrl("https://yahya-tamur.github.io/blinds/locate.html#return=http%3A%2F%2Fblinds.lan%2F"))
    }

    @Test fun findsTheReturnPageOfTheMockServerLink() {
        assertEquals("http://10.0.2.2:8766/",
            LocateLink.returnUrl("http://10.0.2.2:8766/locate.html#return=http%3A%2F%2F10.0.2.2%3A8766%2F"))
    }

    @Test fun ignoresOtherLinks() {
        assertNull(LocateLink.returnUrl("http://blinds.lan/update"))
        assertNull(LocateLink.returnUrl("https://yahya-tamur.github.io/blinds/locate.html"))            // no return
        assertNull(LocateLink.returnUrl("https://yahya-tamur.github.io/blinds/locate.html#return=javascript%3Aalert(1)"))
        assertNull(LocateLink.returnUrl("not a url at all"))
    }

    @Test fun sameOriginComparesSchemeHostAndPort() {
        assertTrue(LocateLink.sameOrigin("http://blinds.lan/", "http://BLINDS.lan/#lat=1"))
        assertFalse(LocateLink.sameOrigin("http://blinds.lan/", "http://blinds.lan:8080/"))
        assertFalse(LocateLink.sameOrigin("http://blinds.lan/", "https://blinds.lan/"))
        assertFalse(LocateLink.sameOrigin("http://evil.example/", "http://blinds.lan/"))
    }

    @Test fun locationIsRoundedToOneDecimal() {
        assertEquals("http://blinds.lan/#lat=52.5&lon=-13.4", LocateLink.withLocation("http://blinds.lan/", 52.46, -13.44))
        assertEquals("http://blinds.lan/#lat=0.0&lon=0.1", LocateLink.withLocation("http://blinds.lan/#old", -0.04, 0.05))
    }
}
