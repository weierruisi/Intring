#ifndef APP_BLE_OTA_H
#define APP_BLE_OTA_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_ble_ota_init(void);
bool app_ble_ota_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
