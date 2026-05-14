#ifndef DATA_SHARE_H
#define DATA_SHARE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"
#include "esp_timer.h"
#include "imu_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern CircularBuffer_t IMUbuffer;

extern QueueHandle_t collect_data_queue;
extern QueueHandle_t gesture_queue;
extern QueueHandle_t mouse_queue_handle;

extern esp_timer_handle_t s_quat_timer;

extern TaskHandle_t app_gesture_detect_handle;

extern SemaphoreHandle_t app_gesture_detect_suspend_flag;

#ifdef __cplusplus
}
#endif

#endif
