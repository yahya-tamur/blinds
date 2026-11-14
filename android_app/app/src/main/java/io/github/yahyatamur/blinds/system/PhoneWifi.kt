package io.github.yahyatamur.blinds.system

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.wifi.WifiInfo
import android.net.wifi.WifiManager
import android.os.Build
import androidx.core.content.ContextCompat
import androidx.core.location.LocationManagerCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// Which Wi-Fi network the phone itself is on (its default network, not the app-only connection).
// Android only tells an app the network name with precise location granted and location turned on;
// otherwise the name is Unknown and the app goes by whether the controller answers.
class PhoneWifi(private val context: Context) {

    sealed interface Ssid {
        data object NotOnWifi : Ssid
        data object Unknown : Ssid
        data class On(val name: String) : Ssid
    }

    // namePermission and locationOn: why the name may be Unknown (Android needs both to tell it)
    data class State(
        val wifiEnabled: Boolean = true,
        val ssid: Ssid = Ssid.Unknown,
        val namePermission: Boolean = false,
        val locationOn: Boolean = true,
    )

    private val cm = context.getSystemService(ConnectivityManager::class.java)
    private val wm = context.getSystemService(WifiManager::class.java)
    private val _state = MutableStateFlow(State(wifiEnabled = wm.isWifiEnabled))
    val state: StateFlow<State> = _state.asStateFlow()
    private var callback: ConnectivityManager.NetworkCallback? = null

    fun hasNamePermission() =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    fun start() {
        if (callback != null) return
        val cb = if (Build.VERSION.SDK_INT >= 31) {
            object : ConnectivityManager.NetworkCallback(FLAG_INCLUDE_LOCATION_INFO) {
                override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) = update(caps)
                override fun onLost(network: Network) = update(null)
            }
        } else {
            object : ConnectivityManager.NetworkCallback() {
                override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) = update(caps)
                override fun onLost(network: Network) = update(null)
            }
        }
        callback = cb
        // Read the current network first, then register: the callback can answer at once on another thread
        // (on Android 12+ with the network name, which this direct read doesn't get), and must win
        update(cm.getNetworkCapabilities(cm.activeNetwork))
        cm.registerDefaultNetworkCallback(cb)
    }

    fun stop() {
        callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        callback = null
    }

    // After the location permission changes, the callback doesn't fire again by itself.
    fun refresh() {
        stop()
        start()
    }

    private fun update(caps: NetworkCapabilities?) {
        val ssid = when {
            caps == null || !caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> Ssid.NotOnWifi
            else -> {
                val raw = if (Build.VERSION.SDK_INT >= 31) (caps.transportInfo as? WifiInfo)?.ssid
                          else @Suppress("DEPRECATION") wm.connectionInfo?.ssid
                parseSsid(raw, canKnow())
            }
        }
        _state.value = State(wifiEnabled = wm.isWifiEnabled, ssid = ssid, namePermission = hasNamePermission(),
                             locationOn = LocationManagerCompat.isLocationEnabled(context.getSystemService(LocationManager::class.java)))
    }

    private fun canKnow() = hasNamePermission() &&
        LocationManagerCompat.isLocationEnabled(context.getSystemService(LocationManager::class.java))

    companion object {
        // The phone's Wi-Fi settings page, where the user switches networks; Back returns to the app. (Not the
        // Wi-Fi panel: tapping a network there hands over to the Settings app, whose Back stays in Settings.)
        fun openWifiSettings(context: Context) {
            try {
                context.startActivity(android.content.Intent(android.provider.Settings.ACTION_WIFI_SETTINGS))
            } catch (_: android.content.ActivityNotFoundException) {
            }
        }

        fun openLocationSettings(context: Context) {
            try {
                context.startActivity(android.content.Intent(android.provider.Settings.ACTION_LOCATION_SOURCE_SETTINGS))
            } catch (_: android.content.ActivityNotFoundException) {
            }
        }

        // WifiInfo.getSSID(): "\"name\"" for a UTF-8 name, bare hex for others, "<unknown ssid>" when hidden from the app.
        fun parseSsid(raw: String?, canKnow: Boolean): Ssid {
            if (!canKnow || raw == null || raw == "<unknown ssid>" || raw.isEmpty()) return Ssid.Unknown
            val name = if (raw.length >= 2 && raw.startsWith('"') && raw.endsWith('"')) raw.substring(1, raw.length - 1) else raw
            return Ssid.On(name)
        }
    }
}
