package io.github.yahyatamur.blinds

import android.os.Bundle
import android.graphics.Color
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import io.github.yahyatamur.blinds.ui.AppScreen
import io.github.yahyatamur.blinds.ui.theme.BlindsTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Light status and navigation bar icons always: the app is always dark
        enableEdgeToEdge(SystemBarStyle.dark(Color.TRANSPARENT), SystemBarStyle.dark(Color.TRANSPARENT))
        val app = application as BlindsApp
        setContent {
            BlindsTheme { AppScreen(app.settings, app.api, app.phoneWifi) }
        }
    }
}
