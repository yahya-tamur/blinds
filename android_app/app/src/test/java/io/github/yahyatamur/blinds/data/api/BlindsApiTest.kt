package io.github.yahyatamur.blinds.data.api

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.IOException
import java.net.InetSocketAddress

// BlindsApi against a tiny local HTTP server that records what it receives.
class BlindsApiTest {
    private lateinit var server: HttpServer
    private val received = mutableListOf<Triple<String, String, String>>()   // method, path+query, body
    private lateinit var api: BlindsApi

    @Before fun start() {
        server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { ex ->
            val body = ex.requestBody.readBytes().decodeToString()
            received += Triple(ex.requestMethod, ex.requestURI.toString(), body)
            val (code, reply) = when {
                ex.requestURI.path == "/api/status" -> 200 to """{"dir":"","alarms":[],"max_alarms":20}"""
                body.contains("at=60") -> 400 to "more than 24 h ahead"
                else -> 200 to "ok"
            }
            val bytes = reply.toByteArray()
            ex.sendResponseHeaders(code, bytes.size.toLong())
            ex.responseBody.use { it.write(bytes) }
        }
        server.start()
        api = BlindsApi { "127.0.0.1:${server.address.port}/" }
    }

    @After fun stop() = server.stop(0)

    @Test fun statusIsAGetWithSince() = runTest {
        val s = api.status(since = 7).getOrThrow()
        assertEquals(20, s.maxAlarms)
        assertEquals(Triple("GET", "/api/status?since=7", ""), received.single())
    }

    @Test fun postsAreFormEncoded() = runTest {
        api.move(Direction.DOWN).getOrThrow()
        api.addAlarm(NewAlarm.Sun(Direction.UP, SunEvent.DAWN, -5)).getOrThrow()
        api.setLocation(52.46, -13.44).getOrThrow()
        assertEquals(listOf(
            Triple("POST", "/api/move", "d=down"),
            Triple("POST", "/api/alarms/add", "d=up&k=sun&e=dawn&o=-5"),
            Triple("POST", "/api/location", "lat=52.5&lon=-13.4"),
        ), received)
    }

    @Test fun errorStatusBecomesApiException() = runTest {
        val e = api.addAlarm(NewAlarm.Once(Direction.UP, 60)).exceptionOrNull()
        assertTrue(e is BlindsApi.ApiException && e.code == 400 && e.message!!.contains("24 h"))
    }

    @Test fun unreachableIsAnIOException() = runTest {
        server.stop(0)
        val e = api.cancel().exceptionOrNull()
        assertTrue("got $e", e is IOException && e !is BlindsApi.ApiException)
    }
}
