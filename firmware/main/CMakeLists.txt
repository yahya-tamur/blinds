# Local, setup-specific tests (firmware/local/tests/, git-ignored): built when present
file(GLOB LOCAL_TESTS "${CMAKE_CURRENT_LIST_DIR}/../local/tests/*.c")
set(LOCAL_REQUIRES "")    # extra components for the local tests, set in local/tests/local.cmake
include("${CMAKE_CURRENT_LIST_DIR}/../local/tests/local.cmake" OPTIONAL)

idf_component_register(
    SRCS ${LOCAL_TESTS} "main.c" "tmc2209.c" "tests/bringup.c" "tests/bringup_wifi.c" "tests/ir.c" "tests/motor.c" "tests/hall.c"
         "app.c" "logbuf.c" "motor_steps.c" "switch_input.c" "remote.c" "clock.c" "alarms.c" "sun.c" "wifi.c" "web.c" "stats.c" "ota.c" "travel.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "../web/index.html" "../web/moving.html" "../web/style.css" "../web/favicon.svg" "../web/update.html" "../web/dropdown.js"
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_rmt esp_driver_i2c esp_driver_ledc esp_driver_gptimer esp_driver_pcnt
                  esp_timer nvs_flash esp_wifi esp_netif esp_event esp_http_server app_update esp_app_format ${LOCAL_REQUIRES})

# Wi-Fi network from wifi.properties at the top of the repository (git-ignored; wifi.example.properties
# shows the format). It becomes wifi_secrets.h in the build
# folder, with WIFI_SSID and WIFI_PASSWORD defined only when an ssid is set. CMake reruns when the file changes.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(WIFI_PROPERTIES "${CMAKE_CURRENT_LIST_DIR}/../../wifi.properties")
    get_filename_component(WIFI_PROPERTIES "${WIFI_PROPERTIES}" ABSOLUTE)
    set(WIFI_HEADER "// Generated from wifi.properties by main/CMakeLists.txt. Do not edit.\n#pragma once\n")
    if(EXISTS "${WIFI_PROPERTIES}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WIFI_PROPERTIES}")
        file(READ "${WIFI_PROPERTIES}" wifi_text)
        foreach(key ssid password)
            set(wifi_${key} "")
            if(wifi_text MATCHES "(^|\n)[ \t]*${key}[ \t]*=([^\r\n]*)")
                string(STRIP "${CMAKE_MATCH_2}" wifi_${key})
            endif()
            string(REPLACE "\\" "\\\\" wifi_${key} "${wifi_${key}}")   # as a C string literal
            string(REPLACE "\"" "\\\"" wifi_${key} "${wifi_${key}}")
        endforeach()
        if(NOT wifi_ssid STREQUAL "")
            string(APPEND WIFI_HEADER "#define WIFI_SSID \"${wifi_ssid}\"\n#define WIFI_PASSWORD \"${wifi_password}\"\n")
        endif()
    endif()
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wifi_secrets.h" CONTENT "${WIFI_HEADER}" @ONLY)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
