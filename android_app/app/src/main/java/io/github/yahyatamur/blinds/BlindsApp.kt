package io.github.yahyatamur.blinds

import android.app.Application
import io.github.yahyatamur.blinds.data.Settings
import io.github.yahyatamur.blinds.data.api.BlindsApi
import io.github.yahyatamur.blinds.system.PhoneWifi

// Builds the objects the whole app shares (manual dependency injection).
class BlindsApp : Application() {
    lateinit var settings: Settings
        private set
    lateinit var api: BlindsApi
        private set
    lateinit var phoneWifi: PhoneWifi
        private set

    override fun onCreate() {
        super.onCreate()
        settings = Settings(this)
        api = BlindsApi { settings.address.value }
        phoneWifi = PhoneWifi(this)
    }
}
