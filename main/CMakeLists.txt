idf_component_register(SRCS "inter_ring_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES ble_ota
                             ble_hid
                             key
                             light_sleep
                             gesture_detect
                             LSM6DS3TR
                             nvs_flash
                             driver
                             esp_timer
                             bt)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
