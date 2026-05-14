idf_component_register(SRCS "lsm6ds3tr/lsm6ds3tr.c" 
                            "lsm6ds3tr/lsm6ds3tr-c_reg.c"
                            "lsm6ds3tr/lsm6ds3tr_quaternion_test.c"
                            "lsm6ds3tr/lsm6ds3tr_c_read_data_polling.c"
                            "imu_buffer/imu_buffer.c"
                       INCLUDE_DIRS "lsm6ds3tr/."
                                    "imu_buffer/."
                       REQUIRES main
                                driver
                                freertos
                                esp_timer
                                gesture_detect)
