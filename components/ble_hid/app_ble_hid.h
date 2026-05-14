#ifndef __APP_BLE_HID__
#define __APP_BLE_HID__

// #define

// 工作状态
typedef enum {
  HID_DEV_TRACKBALL = 0x01,
  HID_DEV_AIR_MOUSE = 0x02,
  HID_DEV_AIR_GESTURE = 0x04
} hid_dev_work_state_t;

extern hid_dev_work_state_t hid_ring_state;

esp_err_t app_ble_hid_init(void);

void clear_sleep_count(void);
void app_ble_hid_set_hid_link_state(bool connected);
void app_ble_hid_set_ota_link_state(bool connected);
void app_ble_hid_set_ota_transfer_state(bool running);
void app_ble_hid_restart_advertising(void);

#endif
