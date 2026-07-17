idf_component_register(SRCS "main.c"
                    REQUIRES esp_driver_uart esp_driver_gpio esp_driver_i2c nvs_flash BSP WiFi esp_timer esp_system freertos
                    INCLUDE_DIRS ".")
