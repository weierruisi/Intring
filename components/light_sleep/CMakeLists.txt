idf_component_register(SRCS "gpio_wakeup.c" "app_sleep.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver
                                freertos
                                esp_pm
                                esp_timer
                                LSM6DS3TR
                                main)
