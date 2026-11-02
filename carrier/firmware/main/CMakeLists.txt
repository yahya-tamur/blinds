idf_component_register(
    SRCS "main.c" "tmc2209.c" "tests/bringup.c" "tests/ir.c" "tests/motor.c" "tests/hall.c"
         "tests/stallguard.c" "tests/spreadcycle.c" "tests/hall_motor.c"
         "app.c" "logbuf.c" "motor_steps.c" "switch_input.c" "remote.c" "clock.c" "alarms.c" "wifi.c" "web.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "../web/index.html" "../web/moving.html" "../web/style.css" "../web/favicon.svg"
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_rmt esp_driver_i2c esp_driver_ledc esp_driver_gptimer esp_driver_pcnt
                  esp_timer nvs_flash esp_wifi esp_netif esp_event esp_http_server)
