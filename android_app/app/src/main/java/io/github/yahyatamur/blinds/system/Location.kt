package io.github.yahyatamur.blinds.system

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.CancellationSignal
import android.os.SystemClock
import androidx.core.content.ContextCompat
import androidx.core.location.LocationManagerCompat
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.coroutines.resume

// One approximate location for the controller's sun times (it keeps 0.1 degree, about 11 km).
// Uses the platform LocationManager, no Google Play services.
object PhoneLocation {
    const val PERMISSION = Manifest.permission.ACCESS_COARSE_LOCATION
    private const val MAX_AGE_MS = 10 * 60 * 1000L   // like locate.html's maximumAge
    private const val TIMEOUT_MS = 20_000L
    private const val FUSED = "fused"   // LocationManager.FUSED_PROVIDER, API 31+

    sealed interface Result {
        data class Found(val location: Location) : Result
        data object NoPermission : Result
        data object LocationOff : Result
        data object NotFound : Result
    }

    fun hasPermission(context: Context) =
        ContextCompat.checkSelfPermission(context, PERMISSION) == PackageManager.PERMISSION_GRANTED

    suspend fun get(context: Context): Result {
        if (!hasPermission(context)) return Result.NoPermission
        val lm = context.getSystemService(LocationManager::class.java)
        if (!LocationManagerCompat.isLocationEnabled(lm)) return Result.LocationOff
        val providers = lm.getProviders(true)
        try {
            // A recent fix is as good as a new one at this precision
            providers.mapNotNull { lm.getLastKnownLocation(it) }
                .filter { SystemClock.elapsedRealtimeNanos() - it.elapsedRealtimeNanos < MAX_AGE_MS * 1_000_000 }
                .maxByOrNull { it.elapsedRealtimeNanos }
                ?.let { return Result.Found(it) }
            // Ask every enabled provider at once and take the first answer: network is usually quickest
            // indoors, GPS (allowed with approximate permission on Android 12+, fuzzed) works without it.
            val wanted = listOf(FUSED, LocationManager.NETWORK_PROVIDER, LocationManager.GPS_PROVIDER).filter { it in providers }
            if (wanted.isEmpty()) return Result.NotFound
            val fix = withTimeoutOrNull(TIMEOUT_MS) {
                suspendCancellableCoroutine { cont ->
                    val cancel = CancellationSignal()
                    cont.invokeOnCancellation { cancel.cancel() }
                    var unanswered = wanted.size
                    val done = { loc: Location? ->
                        unanswered--
                        if (cont.isActive && (loc != null || unanswered == 0)) {
                            cancel.cancel()
                            cont.resume(loc)
                        }
                    }
                    for (p in wanted) {
                        try {
                            LocationManagerCompat.getCurrentLocation(lm, p, cancel, ContextCompat.getMainExecutor(context)) { done(it) }
                        } catch (_: SecurityException) {
                            done(null)   // GPS with approximate permission before Android 12
                        }
                    }
                }
            }
            return fix?.let { Result.Found(it) } ?: Result.NotFound
        } catch (e: SecurityException) {
            return Result.NoPermission
        }
    }
}
