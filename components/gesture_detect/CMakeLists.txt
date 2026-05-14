idf_component_register(SRCS "app_gesture_detect.cc" "test_data.cc" "gesture_model.cc"
                       INCLUDE_DIRS "."
                       REQUIRES freertos
                                esp_timer
                                LSM6DS3TR
                                esp-tflite-micro
                                esp-nn
                                main)
