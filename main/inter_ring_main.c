/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/*
 * Inter-Ring Main - Smart ring device main entry point
 *
 * This module initializes and coordinates all subsystems including:
 * - BLE HID interface
 * - IMU sensor (LSM6DS3TR)
 * - Gesture detection
 * - Power management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* Power management */
#include "driver/gpio.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_hidd_prf_api.h"
#include "esp_pm.h"
#include "hid_dev.h"
#include "app_gesture_detect.h"

#include "app_ble_hid.h"
#include "app_ble_ota.h"
#include "keyRead.h"
#include "lsm6ds3tr.h"
#include "app_sleep.h"
#include "project_config.h"
#include "driver/uart.h"

/*
 * Brief:
 * This example implements BLE HID device profile with 4 Reports:
 * 1. Mouse
 * 2. Keyboard and LED
 * 3. Consumer Devices
 * 4. Vendor devices
 *
 * Users can choose different reports based on application scenarios.
 * BLE HID profile inherits from USB HID class.
 */

/*
 * Note:
 * 1. Win10 does not support vendor report, so SUPPORT_REPORT_VENDOR
 *    is always set to FALSE (defined in hidd_le_prf_int.h)
 * 2. Connection parameter updates are not allowed during iPhone HID
 *    encryption. Slave disables automatic parameter updates during
 *    encryption.
 * 3. iPhones write 1 to Report Characteristic Configuration Descriptor
 *    even before HID encryption completes. We set descriptor permissions
 *    to ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE_ENCRYPTED.
 *    GATT_INSUF_ENCRYPTION errors can be ignored.
 */

#define HID_DEMO_TAG "RING_HID"

void app_main(void)
{
        // vTaskDelay(pdMS_TO_TICKS(1000));
#if CONFIG_TOUCH_SWITCH_TEST_EN
        touch_key_test();
#endif
#if CONFIG_TRACK_BALL_TEST_EN
        track_ball_test();
#endif
        /* Initialize subsystems */
        app_gesture_detect_init();
        app_lsm6ds3tr_Quaternion();
        ESP_ERROR_CHECK(app_ble_hid_init());
        ESP_ERROR_CHECK(app_ble_ota_init());

#if !CONFIG_COLLECT_DATA_EN
        app_sleep_init();
#endif
}
