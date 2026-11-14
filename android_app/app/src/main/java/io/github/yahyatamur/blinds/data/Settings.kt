package io.github.yahyatamur.blinds.data

import android.content.Context
import androidx.core.content.edit
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// The app's own settings, kept on the phone (SharedPreferences), never in the APK.
class Settings(context: Context) {
    private val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    init {
        // Versions 0.1.1-0.2.4 kept a Wi-Fi password, encrypted with an Android Keystore key: delete both
        context.deleteSharedPreferences("secrets")
        runCatching { java.security.KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.deleteEntry("blinds_secrets") }
    }

    private val _address = MutableStateFlow(prefs.getString(KEY_ADDRESS, null) ?: DEFAULT_ADDRESS)
    // "blinds.lan", an IP address with an optional port, or a full http:// URL
    val address: StateFlow<String> = _address.asStateFlow()

    fun setAddress(value: String) {
        val v = value.trim().trimEnd('/').ifEmpty { DEFAULT_ADDRESS }
        prefs.edit { putString(KEY_ADDRESS, v) }
        _address.value = v
    }

    private val _homeWifi = MutableStateFlow(prefs.getString(KEY_WIFI, null).orEmpty())
    // The name (SSID) of the Wi-Fi network the controller is on; empty until the user sets it.
    val homeWifi: StateFlow<String> = _homeWifi.asStateFlow()

    // Only the name: the phone joins the network through its own Wi-Fi settings ("Switch Wi-Fi")
    fun setHomeWifi(value: String) {
        prefs.edit { putString(KEY_WIFI, value.trim()) }
        _homeWifi.value = value.trim()
    }

    // The combined alarm form's last values, so the usual alarm is one tap away. blindsOffset: minutes from
    // the phone alarm to the blinds, negative = before.
    data class CombinedForm(val minuteOfDay: Int = 7 * 60, val up: Boolean = true, val blindsOffset: Int = -10)

    var combinedForm: CombinedForm
        get() = CombinedForm(
            prefs.getInt("combined_minute", 7 * 60), prefs.getBoolean("combined_up", true),
            prefs.getInt("combined_offset", -10),
        )
        set(f) = prefs.edit {
            putInt("combined_minute", f.minuteOfDay); putBoolean("combined_up", f.up); putInt("combined_offset", f.blindsOffset)
            remove("wake_minute"); remove("wake_up"); remove("wake_offset"); remove("wake_clock")   // the old form's keys
        }

    companion object {
        const val DEFAULT_ADDRESS = "blinds.lan"   // APP_HOSTNAME in firmware/main/app_config.h, via the router's DNS
        private const val KEY_ADDRESS = "controller_address"
        private const val KEY_WIFI = "home_wifi"
    }
}
