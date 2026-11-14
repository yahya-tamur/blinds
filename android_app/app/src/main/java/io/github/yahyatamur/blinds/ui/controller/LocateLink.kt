package io.github.yahyatamur.blinds.ui.controller

import java.net.URI
import java.net.URLDecoder
import java.util.Locale
import kotlin.math.floor

// The controller page's "Use my location" button opens locate.html (on GitHub Pages, or on the mock
// server) with #return=<the controller page>, because a plain-http page can't ask the browser for a
// location. In the app the WebView doesn't go there: the app gets the location itself and loads the
// return page with #lat=..&lon=.., which the page saves exactly as when locate.html sends it back.
object LocateLink {

    // The return URL if this is a link to locate.html, otherwise null.
    fun returnUrl(url: String): String? {
        val uri = runCatching { URI(url) }.getOrNull() ?: return null
        if (uri.scheme !in listOf("http", "https") || uri.path?.endsWith("/locate.html") != true) return null
        val ret = uri.rawFragment.orEmpty().split('&')
            .firstOrNull { it.startsWith("return=") }
            ?.let { URLDecoder.decode(it.removePrefix("return="), "UTF-8") } ?: return null
        val back = runCatching { URI(ret) }.getOrNull() ?: return null
        return if (back.scheme in listOf("http", "https") && !back.host.isNullOrEmpty()) ret else null
    }

    // True when both URLs are on the same host and port, so a return link can only lead back to the controller.
    fun sameOrigin(a: String, b: String): Boolean {
        val x = runCatching { URI(a) }.getOrNull() ?: return false
        val y = runCatching { URI(b) }.getOrNull() ?: return false
        return x.scheme == y.scheme && x.host.equals(y.host, ignoreCase = true) && x.port == y.port
    }

    // The return page with the location, rounded to one decimal as locate.html does.
    fun withLocation(returnUrl: String, lat: Double, lon: Double): String =
        returnUrl.substringBefore('#') + "#lat=" + oneDecimal(lat) + "&lon=" + oneDecimal(lon)

    // Halves round up, like JavaScript's Math.round in locate.html; + 0.0 turns -0.0 into 0.0.
    fun oneDecimal(x: Double): String = String.format(Locale.ROOT, "%.1f", floor(x * 10 + 0.5) / 10 + 0.0)
}
