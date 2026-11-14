package io.github.yahyatamur.blinds.data.api

import io.github.yahyatamur.blinds.data.controllerPageUrl
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

// The controller's HTTP API (firmware/main/web.h). One suspend function per endpoint; each runs on
// Dispatchers.IO and returns a Result: an IOException when the controller can't be reached, an
// ApiException when it answers with an error (e.g. 400 for an alarm more than 24 h away).
//
// baseUrl is read on every call, so a changed address in the settings applies at once.
class BlindsApi(private val baseUrl: () -> String) {

    class ApiException(val code: Int, message: String) : IOException("HTTP $code: $message")

    suspend fun status(since: Long = 0): Result<Status> =
        request("GET", "/api/status?since=$since").map { json.decodeFromString<Status>(it) }

    suspend fun move(dir: Direction) = post("/api/move", mapOf("d" to dir.wire))
    suspend fun cancel() = post("/api/cancel")

    suspend fun addAlarm(alarm: NewAlarm) = post("/api/alarms/add", alarm.fields())
    suspend fun removeAlarm(alarm: Alarm) = post("/api/alarms/remove", alarm.removeFields())

    // Degrees, north and east positive; the controller keeps one decimal.
    suspend fun setLocation(lat: Double, lon: Double) =
        post("/api/location", mapOf("lat" to "%.1f".format(java.util.Locale.ROOT, lat),
                                    "lon" to "%.1f".format(java.util.Locale.ROOT, lon)))
    suspend fun clearLocation() = post("/api/location", mapOf("clear" to "1"))

    private suspend fun post(path: String, fields: Map<String, String> = emptyMap()): Result<Unit> =
        request("POST", path, fields).map { }

    private suspend fun request(method: String, path: String, fields: Map<String, String>? = null): Result<String> =
        withContext(Dispatchers.IO) {
            runCatching {
                val conn = URL(root() + path).openConnection() as HttpURLConnection
                try {
                    conn.requestMethod = method
                    conn.connectTimeout = CONNECT_TIMEOUT_MS
                    conn.readTimeout = READ_TIMEOUT_MS
                    conn.useCaches = false
                    if (fields != null) {
                        val body = fields.entries.joinToString("&") { (k, v) -> enc(k) + "=" + enc(v) }.toByteArray()
                        conn.doOutput = true
                        conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                        conn.setFixedLengthStreamingMode(body.size)
                        conn.outputStream.use { it.write(body) }
                    }
                    val code = conn.responseCode
                    if (code !in 200..299) {
                        val msg = conn.errorStream?.bufferedReader()?.use { it.readText() }.orEmpty()
                        throw ApiException(code, msg.trim().take(200))
                    }
                    conn.inputStream.bufferedReader().use { it.readText() }
                } finally {
                    conn.disconnect()
                }
            }
        }

    // "blinds.lan", "192.168.8.20:80" or a full "http://..." URL all work.
    private fun root(): String = controllerPageUrl(baseUrl()).trimEnd('/')

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8")

    companion object {
        const val CONNECT_TIMEOUT_MS = 3000
        const val READ_TIMEOUT_MS = 5000
        val json = Json { ignoreUnknownKeys = true }
    }
}
