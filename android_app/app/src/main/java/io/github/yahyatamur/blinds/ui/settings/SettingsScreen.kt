package io.github.yahyatamur.blinds.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import io.github.yahyatamur.blinds.BuildConfig
import io.github.yahyatamur.blinds.data.Settings
import io.github.yahyatamur.blinds.system.PhoneWifi
import io.github.yahyatamur.blinds.ui.ControllerLink
import io.github.yahyatamur.blinds.ui.theme.PageColors
import kotlinx.coroutines.delay

@Composable
fun SettingsScreen(
    link: ControllerLink,
    address: String,
    homeWifi: String,
    phone: PhoneWifi.State,           // the phone's own network, when Android tells the app
    onSaveAddress: (String) -> Unit,
    onSaveHomeWifi: (String) -> Unit,
    onAskNamePermission: () -> Unit,  // precise location, which Android needs to tell the network name
    onSwitchWifi: () -> Unit,         // the phone's Wi-Fi settings
    onLocationSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier.fillMaxSize().imePadding().verticalScroll(rememberScrollState()).padding(24.dp),   // scrolls above the keyboard
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Settings", style = MaterialTheme.typography.headlineSmall)

        ConnectionCard(link, address, homeWifi, phone, onSwitchWifi, onAskNamePermission, onLocationSettings)

        WifiNameField(homeWifi, phone.ssid, onSaveHomeWifi, onAskNamePermission)

        HorizontalDivider()
        AutosaveField(
            title = "Controller address",
            label = "Host name or IP address (default ${Settings.DEFAULT_ADDRESS})",
            saved = address,
            keyboard = KeyboardType.Uri,
            onSave = onSaveAddress,
        )

        HorizontalDivider()
        Text("App version ${BuildConfig.VERSION_NAME}", style = MaterialTheme.typography.bodySmall,
             color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

// Whether the app can use the controller, and what to do when it can't. The app contacts the controller
// only on its Wi-Fi; the phone joins that network in its own Wi-Fi settings.
@Composable
private fun ConnectionCard(
    link: ControllerLink,
    address: String,
    homeWifi: String,
    phone: PhoneWifi.State,
    onSwitchWifi: () -> Unit,
    onAskNamePermission: () -> Unit,
    onLocationSettings: () -> Unit,
) {
    val ok = link is ControllerLink.Up
    val (title, detail) = when (link) {
        is ControllerLink.Up -> "Connected to the controller" to null
        ControllerLink.Checking -> "Checking the controller…" to null
        is ControllerLink.Down -> "The controller doesn't answer" to "$address on $homeWifi: ${link.reason}"
        ControllerLink.OffNetwork -> "Not on the controller's Wi-Fi" to when {
            homeWifi.isEmpty() -> "Enter the controller's Wi-Fi name below."
            !phone.wifiEnabled -> "Wi-Fi is off."
            else -> when (val s = phone.ssid) {
                is PhoneWifi.Ssid.On -> "The phone is on ${s.name}. Switch to $homeWifi."
                PhoneWifi.Ssid.NotOnWifi -> "The phone isn't on Wi-Fi. Switch to $homeWifi."
                PhoneWifi.Ssid.Unknown ->
                    if (!phone.namePermission) "Allow precise location so the app can see which Wi-Fi the phone is on."
                    else "Turn on location so the app can see which Wi-Fi the phone is on."
            }
        }
    }
    val nameHidden = link is ControllerLink.OffNetwork && homeWifi.isNotEmpty() && phone.wifiEnabled &&
        phone.ssid == PhoneWifi.Ssid.Unknown

    Surface(color = if (ok) PageColors.card else PageColors.neutral, shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (link is ControllerLink.Checking) CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                Text(title, style = MaterialTheme.typography.titleMedium, color = PageColors.text)
            }
            detail?.let { Text(it, style = MaterialTheme.typography.bodyMedium, color = PageColors.text) }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                when {
                    nameHidden && !phone.namePermission -> Button(onClick = onAskNamePermission) { Text("Allow") }
                    nameHidden -> Button(onClick = onLocationSettings) { Text("Location settings") }
                    homeWifi.isNotEmpty() -> if (ok) OutlinedButton(onClick = onSwitchWifi) { Text("Switch Wi-Fi") }
                                             else Button(onClick = onSwitchWifi) { Text("Switch Wi-Fi") }
                }
            }
        }
    }
}

// The controller's network name (ssid in wifi.properties). Saved when editing ends; "Use current" fills in the
// phone's current network.
@Composable
private fun WifiNameField(saved: String, phoneSsid: PhoneWifi.Ssid, onSave: (String) -> Unit, onAskNamePermission: () -> Unit) {
    var text by rememberSaveable(saved) { mutableStateOf(saved) }
    var note by rememberSaveable { mutableStateOf<String?>(null) }
    var waiting by rememberSaveable { mutableStateOf(false) }
    var focused by remember { mutableStateOf(false) }   // save only when focus leaves, not on the first "unfocused" call
    val focus = LocalFocusManager.current

    // "Use current": once Android tells the phone's network name, use it
    LaunchedEffect(waiting, phoneSsid) {
        if (!waiting) return@LaunchedEffect
        (phoneSsid as? PhoneWifi.Ssid.On)?.let { text = it.name; onSave(it.name); note = null; waiting = false; return@LaunchedEffect }
        delay(4000)
        waiting = false
        note = if (phoneSsid == PhoneWifi.Ssid.NotOnWifi) "The phone isn't on a Wi-Fi network."
               else "Android didn't give the network name: it needs precise location allowed and location on."
    }

    Text("Controller's Wi-Fi", style = MaterialTheme.typography.titleMedium)
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedTextField(
            value = text,
            onValueChange = { text = it },
            singleLine = true,
            label = { Text("Network name") },
            keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { focus.clearFocus() }),
            modifier = Modifier.weight(1f).onFocusChanged {
                if (focused && !it.isFocused && text.trim() != saved) onSave(text)
                focused = it.isFocused
            },
        )
        OutlinedButton(onClick = {
            val now = phoneSsid as? PhoneWifi.Ssid.On
            if (now != null) { text = now.name; onSave(now.name); note = null } else { waiting = true; onAskNamePermission() }
        }, modifier = Modifier.padding(top = 8.dp)) { Text("Use current") }
    }
    note?.let { Text(it, style = MaterialTheme.typography.bodyMedium, color = PageColors.text) }
}

// A titled text field saved when editing ends (Done, or leaving the field).
@Composable
private fun AutosaveField(title: String, label: String, saved: String, keyboard: KeyboardType, onSave: (String) -> Unit) {
    var text by rememberSaveable(saved) { mutableStateOf(saved) }
    var focused by remember { mutableStateOf(false) }   // save only when focus leaves, not on the first "unfocused" call
    val focus = LocalFocusManager.current
    Text(title, style = MaterialTheme.typography.titleMedium)
    OutlinedTextField(
        value = text,
        onValueChange = { text = it },
        singleLine = true,
        label = { Text(label) },
        keyboardOptions = KeyboardOptions(keyboardType = keyboard, imeAction = ImeAction.Done, autoCorrectEnabled = false),
        keyboardActions = KeyboardActions(onDone = { focus.clearFocus() }),
        modifier = Modifier.fillMaxWidth().onFocusChanged {
            if (focused && !it.isFocused && text.trim() != saved) onSave(text)
            focused = it.isFocused
        },
    )
}
