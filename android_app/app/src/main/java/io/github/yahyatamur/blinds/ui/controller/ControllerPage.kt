package io.github.yahyatamur.blinds.ui.controller

import android.content.ActivityNotFoundException
import android.content.Intent
import android.provider.Settings as AndroidSettings
import android.view.ViewGroup
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import io.github.yahyatamur.blinds.system.PhoneLocation
import kotlinx.coroutines.launch

// The "Blinds" tab: the controller's own page, plus the app's side of its "Use my location" button.
@Composable
fun ControllerPage(
    browser: ControllerBrowser,
    address: String,
    snackbar: SnackbarHostState,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var locating by remember { mutableStateOf(false) }

    LaunchedEffect(address) { browser.show(address) }
    BackHandler(enabled = browser.canGoBack) { browser.goBack() }

    fun message(text: String, action: String? = null, onAction: () -> Unit = {}) = scope.launch {
        val r = snackbar.showSnackbar(text, actionLabel = action, withDismissAction = action == null,
                                      duration = if (action != null) SnackbarDuration.Long else SnackbarDuration.Short)
        if (r == SnackbarResult.ActionPerformed) onAction()
    }

    fun fetchLocation(back: String) = scope.launch {
        locating = true
        val result = PhoneLocation.get(context)
        locating = false
        when (result) {
            is PhoneLocation.Result.Found -> browser.deliverLocation(back, result.location.latitude, result.location.longitude)
            PhoneLocation.Result.LocationOff -> {
                browser.cancelLocate()
                message("Location is off on this phone.", "Turn on") {
                    try {
                        context.startActivity(Intent(AndroidSettings.ACTION_LOCATION_SOURCE_SETTINGS))
                    } catch (_: ActivityNotFoundException) {
                    }
                }
            }
            PhoneLocation.Result.NoPermission -> {
                browser.cancelLocate()
                message("No location permission. You can type the location on the page instead.")
            }
            PhoneLocation.Result.NotFound -> {
                browser.cancelLocate()
                message("The phone couldn't find its location. Try again, or type it on the page.")
            }
        }
    }

    val askPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        val back = browser.locateRequest ?: return@rememberLauncherForActivityResult
        if (granted) fetchLocation(back) else {
            browser.cancelLocate()
            message("Location permission refused. You can type the location on the page instead.")
        }
    }

    LaunchedEffect(browser.locateRequest) {
        val back = browser.locateRequest ?: return@LaunchedEffect
        if (PhoneLocation.hasPermission(context)) fetchLocation(back) else askPermission.launch(PhoneLocation.PERMISSION)
    }

    Box(modifier.fillMaxSize()) {
        AndroidView(
            factory = { browser.webView.also { (it.parent as? ViewGroup)?.removeView(it) } },
            modifier = Modifier.fillMaxSize(),
        )
        // An unreachable controller is handled by Settings (AppScreen); here only the loading bar
        if (browser.error == null && browser.progress < 100) {
            LinearProgressIndicator(progress = { browser.progress / 100f }, modifier = Modifier.fillMaxWidth())
        }
        if (locating) {
            Surface(
                modifier = Modifier.align(Alignment.BottomCenter).padding(16.dp),
                shape = MaterialTheme.shapes.medium,
                tonalElevation = 6.dp,
                shadowElevation = 6.dp,
            ) {
                Row(Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
                    Text("Getting the phone's location…")
                }
            }
        }
    }
}
