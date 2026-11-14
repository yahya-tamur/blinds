package io.github.yahyatamur.blinds.ui.controller

import android.annotation.SuppressLint
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.view.ViewGroup
import android.webkit.JavascriptInterface
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.net.toUri
import io.github.yahyatamur.blinds.data.controllerPageUrl
import io.github.yahyatamur.blinds.system.ClockAlarms

// The WebView that shows the controller's own page, kept alive while the user switches tabs.
// Links on the controller stay in it; links elsewhere open in the browser; the locate.html link
// becomes a location request for the app (see LocateLink).
@SuppressLint("SetJavaScriptEnabled")
class ControllerBrowser(context: Context) {
    var progress by mutableIntStateOf(0)            // 0..100 while a page loads, 100 when done
        private set
    var error by mutableStateOf<String?>(null)      // why the page didn't load, null when it did
        private set
    var canGoBack by mutableStateOf(false)
        private set
    var locateRequest by mutableStateOf<String?>(null)   // return URL waiting for the phone's location
        private set

    private var pageUrl: String? = null
    private var freshLoad = false   // clear the history once a show() has loaded, so Back can't reach about:blank

    val webView: WebView = WebView(context).apply {
        layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        settings.javaScriptEnabled = true    // the page is static HTML filled in from /api/status by JavaScript
        settings.domStorageEnabled = true
        webViewClient = Client()
        addJavascriptInterface(PageBridge(context), "BlindsApp")   // read by firmware/web/index.html
        webChromeClient = object : android.webkit.WebChromeClient() {
            override fun onProgressChanged(view: WebView, newProgress: Int) { this@ControllerBrowser.progress = newProgress }
        }
    }

    // Shows the controller at this address; loads only when the address changed.
    fun show(address: String) {
        val url = controllerPageUrl(address)
        if (url == pageUrl) return
        pageUrl = url
        freshLoad = true
        webView.loadUrl(url)
    }

    // Unloads the page (its own polling stops) while the phone isn't on the controller's network; show() loads it again
    fun blank() {
        if (pageUrl == null) return
        pageUrl = null
        error = null
        webView.loadUrl("about:blank")
    }

    fun retry() {
        val url = pageUrl ?: return
        error = null
        webView.loadUrl(url)
    }

    // Load the page again, e.g. after the app joined or left the controller's network.
    fun reload() = retry()

    fun goBack() = webView.goBack()

    // Scrolls the controller's page to its alarm list (after the app set a combined alarm)
    fun showAlarms() {
        webView.evaluateJavascript("document.getElementById('alarms')?.scrollIntoView({block: 'center', behavior: 'smooth'})", null)
    }

    fun deliverLocation(returnUrl: String, lat: Double, lon: Double) {
        locateRequest = null
        webView.loadUrl(LocateLink.withLocation(returnUrl, lat, lon))
    }

    fun cancelLocate() { locateRequest = null }

    fun onStart() { webView.onResume(); webView.resumeTimers() }
    fun onStop() { webView.onPause(); webView.pauseTimers() }   // stop the page's status polling in the background
    fun destroy() { (webView.parent as? ViewGroup)?.removeView(webView); webView.destroy() }

    // What the controller's page can ask the app (window.BlindsApp): the phone's next alarm, to list it with
    // the blinds alarms, and opening the clock app when that row is tapped. Only the controller's page loads in
    // this WebView (other links open in the browser), and all it can read is that alarm's time.
    private inner class PageBridge(private val context: Context) {
        @JavascriptInterface
        fun nextPhoneAlarm(): String = ClockAlarms.next(context)?.let { """{"at":${it.triggerMs / 1000}}""" } ?: ""

        @JavascriptInterface
        fun openClock() {
            webView.post { ClockAlarms.open(context, ClockAlarms.next(context)) }
        }
    }

    private inner class Client : WebViewClient() {
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
            val url = request.url.toString()
            val here = view.url ?: pageUrl.orEmpty()
            LocateLink.returnUrl(url)?.let { back ->
                if (LocateLink.sameOrigin(back, here)) locateRequest = back
                return true
            }
            if (LocateLink.sameOrigin(url, here)) return false
            try {
                view.context.startActivity(Intent(Intent.ACTION_VIEW, url.toUri()))
            } catch (_: ActivityNotFoundException) {
            }
            return true
        }

        override fun onPageStarted(view: WebView, url: String?, favicon: Bitmap?) { error = null }

        override fun onPageFinished(view: WebView, url: String?) {
            if (freshLoad && url != "about:blank") {
                freshLoad = false
                view.clearHistory()
                canGoBack = false
            }
        }

        override fun onReceivedError(view: WebView, request: WebResourceRequest, err: WebResourceError) {
            if (request.isForMainFrame) error = err.description?.toString()?.ifEmpty { null } ?: "error ${err.errorCode}"
        }

        override fun onReceivedHttpError(view: WebView, request: WebResourceRequest, response: WebResourceResponse) {
            if (request.isForMainFrame) error = "HTTP ${response.statusCode} ${response.reasonPhrase.orEmpty()}".trim()
        }

        override fun doUpdateVisitedHistory(view: WebView, url: String?, isReload: Boolean) {
            canGoBack = view.canGoBack()
        }
    }
}
