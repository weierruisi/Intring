idf_component_register(SRCS "app_ble_ota.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt app_update ble_hid)
