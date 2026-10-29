idf_component_register(
    SRCS "main.c" "tmc2209.c" "test_switches.c" "test_ir.c" "test_motor.c" "test_stallguard.c" "test_rtc.c" "test_spreadcycle.c"
         "app.c" "logbuf.c" "motor_steps.c" "switch_input.c" "remote.c" "clock.c" "alarms.c" "wifi.c" "web.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "../web/index.html" "../web/moving.html" "../web/style.css" "../web/favicon.svg"
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_rmt esp_driver_i2c esp_driver_ledc esp_driver_gptimer
                  esp_timer nvs_flash esp_wifi esp_netif esp_event esp_http_server)
