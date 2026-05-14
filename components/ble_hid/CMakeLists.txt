idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       REQUIRES bt
                                esp_timer
                                nvs_flash
                                freertos
                                driver
                                key
                                air_mouse
                                gesture_detect
                                light_sleep
                                LSM6DS3TR)
