package io.github.yahyatamur.blinds.ui

import android.Manifest
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import io.github.yahyatamur.blinds.data.Settings
import io.github.yahyatamur.blinds.data.api.BlindsApi
import io.github.yahyatamur.blinds.system.PhoneWifi
import io.github.yahyatamur.blinds.ui.combined.CombinedScreen
import io.github.yahyatamur.blinds.ui.controller.ControllerBrowser
import io.github.yahyatamur.blinds.ui.controller.ControllerPage
import io.github.yahyatamur.blinds.ui.settings.SettingsScreen
import kotlinx.coroutines.delay

private enum class Tab(val label: String, val icon: ImageVector) {
    COMBINED("Combined", Icons.Filled.Notifications),   // phone + blinds alarm; first, and the tab the app opens on
    BLINDS("Blinds", Icons.Filled.Home),
    SETTINGS("Settings", Icons.Filled.Settings),
}

// Whether the app can use the controller. It is contacted only while the phone is on the controller's Wi-Fi
// (OffNetwork otherwise: nothing is sent, and the page is unloaded). One check for the whole app: unless the
// controller answers, the app shows Settings (with "Switch Wi-Fi") and the other tabs are disabled.
sealed interface ControllerLink {
    data object OffNetwork : ControllerLink
    data object Checking : ControllerLink
    data class Up(val status: io.github.yahyatamur.blinds.data.api.Status) : ControllerLink
    data class Down(val reason: String) : ControllerLink
}

@Composable
fun AppScreen(settings: Settings, api: BlindsApi, phoneWifi: PhoneWifi) {
    var tab by rememberSaveable { mutableStateOf(Tab.COMBINED) }
    var wanted by rememberSaveable { mutableStateOf<Tab?>(null) }   // the tab to go back to once the controller answers
    val address by settings.address.collectAsStateWithLifecycle()
    val homeWifi by settings.homeWifi.collectAsStateWithLifecycle()
    val phone by phoneWifi.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }

    // One WebView for the app's life, so switching tabs doesn't reload the controller page
    val context = LocalContext.current
    val browser = remember { ControllerBrowser(context) }
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    DisposableEffect(lifecycle) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START -> { browser.onStart(); phoneWifi.start() }
                Lifecycle.Event.ON_RESUME -> phoneWifi.refresh()   // e.g. back from the Wi-Fi or location settings
                Lifecycle.Event.ON_STOP -> { browser.onStop(); phoneWifi.stop() }
                else -> {}
            }
        }
        lifecycle.addObserver(observer)
        onDispose { lifecycle.removeObserver(observer); browser.destroy() }
    }

    // The controller check, only on its Wi-Fi: every 3 s while it doesn't answer, every 30 s while it does;
    // at once after joining the network or a new address
    val onHomeWifi = homeWifi.isNotEmpty() && phone.wifiEnabled && phone.ssid == PhoneWifi.Ssid.On(homeWifi)
    var link by remember { mutableStateOf<ControllerLink>(ControllerLink.Checking) }
    LaunchedEffect(address, onHomeWifi) {
        if (!onHomeWifi) { link = ControllerLink.OffNetwork; return@LaunchedEffect }
        link = ControllerLink.Checking
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (true) {
                api.status()
                    .onSuccess { link = ControllerLink.Up(it) }
                    .onFailure { link = ControllerLink.Down(it.message ?: it.javaClass.simpleName) }
                delay(if (link is ControllerLink.Up) 30_000 else 3_000)
            }
        }
    }

    // The controller page: loaded while the controller answers (in the background, so the Blinds tab is ready),
    // unloaded otherwise so it doesn't poll the controller from another network
    val up = link is ControllerLink.Up
    LaunchedEffect(address, up) { if (up) browser.show(address) else if (!onHomeWifi) browser.blank() }

    // Not usable: to Settings, the other tabs disabled. Usable again: back to the tab that was open.
    val blocked = link is ControllerLink.Down || link is ControllerLink.OffNetwork
    LaunchedEffect(blocked, up) {
        if (blocked) {
            if (tab != Tab.SETTINGS) { wanted = tab; tab = Tab.SETTINGS }
        } else if (up) {
            wanted?.let { tab = it }
            wanted = null
        }
    }

    val askNamePermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        phoneWifi.refresh()
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            NavigationBar {
                Tab.entries.forEach {
                    NavigationBarItem(
                        selected = tab == it,
                        onClick = { tab = it; if (it != Tab.SETTINGS) wanted = null },
                        enabled = !blocked || it == Tab.SETTINGS,
                        icon = { Icon(it.icon, contentDescription = null) },
                        label = { Text(it.label) },
                    )
                }
            }
        },
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding)) {
            when (tab) {
                Tab.BLINDS -> ControllerPage(browser, address, snackbar)
                Tab.COMBINED -> CombinedScreen(
                    api, settings,
                    location = (link as? ControllerLink.Up)?.status?.location,
                    statusKnown = link is ControllerLink.Up,
                    snackbar = snackbar,
                    onSet = { tab = Tab.BLINDS; browser.showAlarms() },
                )
                Tab.SETTINGS -> SettingsScreen(
                    link = link,
                    address = address,
                    homeWifi = homeWifi,
                    phone = phone,
                    onSaveAddress = { settings.setAddress(it) },
                    onSaveHomeWifi = { settings.setHomeWifi(it) },
                    onAskNamePermission = {
                        if (phoneWifi.hasNamePermission()) phoneWifi.refresh()
                        else askNamePermission.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION))
                    },
                    onSwitchWifi = { PhoneWifi.openWifiSettings(context) },
                    onLocationSettings = { PhoneWifi.openLocationSettings(context) },
                )
            }
        }
    }
}
