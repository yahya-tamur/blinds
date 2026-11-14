package io.github.yahyatamur.blinds.data

// The controller page's URL for an address as typed in the settings: "blinds.lan", "192.168.8.20:8080"
// or a full "http://..." URL. Always ends in "/".
fun controllerPageUrl(address: String): String {
    val a = address.trim().trimEnd('/')
    return (if (a.startsWith("http://") || a.startsWith("https://")) a else "http://$a") + "/"
}
