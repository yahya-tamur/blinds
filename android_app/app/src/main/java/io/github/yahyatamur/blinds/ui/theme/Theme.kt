package io.github.yahyatamur.blinds.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Always dark, with the controller page's colours (firmware/web/style.css), so the native tabs
// and the page in the WebView look like one app. There is no light theme on purpose.
object PageColors {
    val bg = Color(0xFF111418)
    val card = Color(0xFF1B2027)
    val text = Color(0xFFE8EAED)
    val muted = Color(0xFF9AA3AE)
    val line = Color(0xFF2B323B)
    val up = Color(0xFFD6A04A)
    val onUp = Color(0xFF3B2600)
    val down = Color(0xFF2C4A92)
    val accent = Color(0xFF4F86EC)
    val danger = Color(0xFFA8323A)
    val dangerText = Color(0xFFE0676D)   // red text on dark (docs/locate.html --danger)
    val neutral = Color(0xFF353D48)
}

private val Colors = darkColorScheme(
    primary = PageColors.accent,
    onPrimary = Color.White,
    primaryContainer = PageColors.down,
    onPrimaryContainer = Color.White,
    secondary = PageColors.up,
    onSecondary = PageColors.onUp,
    secondaryContainer = PageColors.neutral,
    onSecondaryContainer = PageColors.text,
    background = PageColors.bg,
    onBackground = PageColors.text,
    surface = PageColors.bg,
    onSurface = PageColors.text,
    surfaceVariant = PageColors.card,
    onSurfaceVariant = PageColors.muted,
    surfaceContainer = PageColors.card,
    surfaceContainerLow = PageColors.card,
    surfaceContainerHigh = PageColors.neutral,
    outline = PageColors.muted,
    outlineVariant = PageColors.line,
    tertiary = PageColors.up,
    onTertiary = PageColors.onUp,
    tertiaryContainer = PageColors.down,      // e.g. the time picker's AM/PM choice
    onTertiaryContainer = Color.White,
    inverseSurface = PageColors.neutral,      // snackbars
    inverseOnSurface = PageColors.text,
    inversePrimary = PageColors.accent,
    error = PageColors.danger,
    onError = Color.White,
)

@Composable
fun BlindsTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = Colors, content = content)
}
