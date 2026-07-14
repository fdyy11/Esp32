set(src_dirs
            LED
            KEY
            UART
            SPILCD
            XL9555
            LEDC
            MYIIC
            MYSPI
            RELAY)

set(include_dirs
            ./LED
            ./KEY
            ./UART
            ./SPILCD
            ./XL9555
            ./LEDC
            ./MYIIC
            ./MYSPI
            ./RELAY)

set(requires
            driver
            esp_driver_gpio
            esp_lcd
            esp_driver_spi
            esp_driver_ledc
            esp_driver_i2c
            esp_driver_uart)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
