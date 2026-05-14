#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_bt.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "keyRead.h"
// #include "lsm6ds3tr.h"
#include "project_config.h"
#include "data_share.h"
#include "app_gesture_detect.h"
#include "air_mouse.h"
#include "lsm6ds3tr_quaternion_test.h"

// #include "light_sleep_example.h"
#include "app_ble_hid.h"

/**
 * Brief:
 * This example Implemented BLE HID device profile related functions, in which
 * the HID device has 4 Reports (1 is mouse, 2 is keyboard and LED, 3 is
 * Consumer Devices, 4 is Vendor devices). Users can choose different reports
 * according to their own application scenarios. BLE HID profile inheritance and
 * USB HID class.
 */

/**
 * Note:
 * 1. Win10 does not support vendor report , So SUPPORT_REPORT_VENDOR is always
 * set to FALSE, it defines in hidd_le_prf_int.h
 * 2. Update connection parameters are not allowed during iPhone HID encryption,
 * slave turns off the ability to automatically update connection parameters
 * during encryption.
 * 3. After our HID device is connected, the iPhones write 1 to the Report
 * Characteristic Configuration Descriptor, even if the HID encryption is not
 * completed. This should actually be written 1 after the HID encryption is
 * completed. we modify the permissions of the Report Characteristic
 * Configuration Descriptor to `ESP_GATT_PERM_READ |
 * ESP_GATT_PERM_WRITE_ENCRYPTED`. if you got `GATT_INSUF_ENCRYPTION` error,
 * please ignore.
 */

#define HID_APP_TAG "HID_APP"
#define AIR_MOUSE_OFFSET_PARA   0.0005
#define TRACKBALL_MOUSE_STEP            7
#define TRACKBALL_SMOOTH_ALPHA          0.78f
#define TRACKBALL_ACCEL_STEP_FRAMES     2
#define TRACKBALL_ACCEL_GAIN_MAX        12
#define TRACKBALL_DIAGONAL_SCALE_NUM    5
#define TRACKBALL_DIAGONAL_SCALE_DEN    6
#define TRACKBALL_STREAK_MAX            24
#define TRACKBALL_REVERSE_BRAKE_STEP    2
#define TRACKBALL_IDLE_DECAY_FRAMES     2
#define GESTURE_VOLUME_STEP_MIN         2
#define GESTURE_VOLUME_STEP_MAX         20
#define GESTURE_VOLUME_RESET_TIMEOUT_MS 3600

esp_pm_config_t pm_config = {};

// 任务句柄
TaskHandle_t track_ball_task_handle = NULL;

// 消息队列
QueueHandle_t queue1 = NULL;
QueueHandle_t mouse_queue_handle = NULL;
#if CONFIG_COLLECT_DATA_EN
QueueHandle_t collect_data_queue = NULL;
#endif
//信号量
SemaphoreHandle_t Semaphore_gesture = NULL;
SemaphoreHandle_t led_sys_mode_flag = NULL;
SemaphoreHandle_t led_work_mode_flag = NULL;

// 系统模式
typedef enum{
  SYS_WIN = 0,
  SYS_ANDROID
} sys_t;
sys_t sys = SYS_WIN;

static mouse_cmd_t ball_press_state = HID_KEY_RESERVED;

/* Forward declarations */
static void send_gesture_command(gesture_class_t gesture, sys_t sys, uint16_t conn_id);
static void handle_key_ball(void);
static void handle_key_touch_a_b(void);
static void handle_key_touch_a_b_ball(void);
static void handle_key_touch_a(key_enum_t keynum);
static void handle_key_touch_b(key_enum_t keynum);
static void handle_key_touch_a_double_click(void);
static void handle_key_touch_b_double_click(void);

// Air Mouse参数
float offset_x = 0, offset_y = 0;
// BLE_HID参数
int16_t mouse_dirCount[2] = {0};
static uint16_t hid_conn_id = 0;
static bool sec_conn = false;
static bool ota_conn = false;
static bool ota_transfer_running = false;
#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))

static void hidd_event_callback(esp_hidd_cb_event_t event,
                                esp_hidd_cb_param_t *param);

#define HIDD_DEVICE_NAME "Intring V1.0"
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

static uint8_t adv_config_done = 0;
static bool adv_data_ready = false;

static uint8_t hidd_service_uuid128[] = {
    /* LSB
       <-------------------------------------------------------------------------------->
       MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = false,
    .include_txpower = false,
    .min_interval = 0,
    .max_interval = 0,
    .appearance = 0x03c0,  // HID Generic,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_data_t hidd_scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0,
    .max_interval = 0,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = 0,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min = 0x40,   // 80ms - 适度降低广播频率
    .adv_int_max = 0x60,  // 160ms - 平衡功耗和连接速度
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t LEDworking = 0;
static bool ledc_ready = false;
static const ledc_mode_t k_ledc_mode = LEDC_LOW_SPEED_MODE;
static const ledc_channel_t k_ledc_channel = LEDC_CHANNEL_0;
static const ledc_timer_t k_ledc_timer = LEDC_TIMER_0;
static const uint32_t k_ledc_max_duty = 1023;

static void led_set_duty(uint32_t duty)
{
        if (duty > k_ledc_max_duty)
                duty = k_ledc_max_duty;

        if (ledc_ready) {
                ledc_set_duty(k_ledc_mode, k_ledc_channel, duty);
                ledc_update_duty(k_ledc_mode, k_ledc_channel);
        } else {
                gpio_set_level(CONFIG_GPIO_LED_PM_NUM, duty ? 1 : 0);
        }
}

static void led_set_onoff(bool on)
{
        led_set_duty(on ? k_ledc_max_duty : 0);
}

static void start_advertising_if_ready(void)
{
        if (adv_config_done != 0 || !adv_data_ready)
                return;

        esp_err_t ret = esp_ble_gap_start_advertising(&hidd_adv_params);
        if (ret != ESP_OK)
                ESP_LOGE(HID_APP_TAG, "esp_ble_gap_start_advertising failed: %s",
                         esp_err_to_name(ret));
}

static void refresh_link_state_and_notify(bool prev_conn)
{
        bool now_conn = sec_conn || ota_conn;
        if (!prev_conn && now_conn) {
                BaseType_t ret = xSemaphoreGive(led_sys_mode_flag);
                if (ret == pdFALSE) {
                        ESP_LOGE(HID_APP_TAG, "xSemaphoreGive led_sys_mode_flag err");
                }
                clear_sleep_count();
        } else if (prev_conn && !now_conn) {
                xSemaphoreGive(led_work_mode_flag);
        }
}

void app_ble_hid_set_hid_link_state(bool connected)
{
        bool prev_conn = sec_conn || ota_conn;
        bool prev_hid_conn = sec_conn;
        sec_conn = connected;
        if (prev_hid_conn != connected)
                clear_sleep_count();
        refresh_link_state_and_notify(prev_conn);
}

void app_ble_hid_set_ota_link_state(bool connected)
{
        bool prev_conn = sec_conn || ota_conn;
        bool prev_ota_conn = ota_conn;
        ota_conn = connected;
        if (prev_ota_conn != connected)
                clear_sleep_count();
        refresh_link_state_and_notify(prev_conn);
}

void app_ble_hid_set_ota_transfer_state(bool running)
{
        ota_transfer_running = running;
}

void app_ble_hid_restart_advertising(void)
{
        start_advertising_if_ready();
}

/*
 * led_blink - Blink LED with specified pattern
 * @count: Number of blinks
 * @on_ms: LED on duration (ms)
 * @off_ms: LED off duration (ms)
 */
static void led_blink(int count, int on_ms, int off_ms)
{
        for (int i = 0; i < count; i++) {
                led_set_onoff(true);
                vTaskDelay(pdMS_TO_TICKS(on_ms));
                led_set_onoff(false);
                if (i < count - 1)
                        vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
}

static void les_task(void *ptr)
{
        /* Connection indication: fast blink until connected */
        static bool conn_blink_state = 1;
        const uint8_t breath_pwm_period = 20;
        const uint8_t breath_min_duty = 4;
        static uint8_t breath_duty = 4;
        static int8_t breath_dir = 1;
        static uint8_t breath_pwm_counter = 0;
        const TickType_t breath_tick = pdMS_TO_TICKS(6);
        // LEDworking = 1;
        // while (!sec_conn) {
        //         gpio_set_level(CONFIG_GPIO_LED_PM_NUM, 1);
        //         vTaskDelay(pdMS_TO_TICKS(50));
        //         gpio_set_level(CONFIG_GPIO_LED_PM_NUM, 0);
        //         vTaskDelay(pdMS_TO_TICKS(50));
        // }
        // LEDworking = 0;
        // vTaskDelay(pdMS_TO_TICKS(1000));

        /* Event-driven LED indication */
        while (1) {
                bool link_connected = sec_conn || ota_conn;

                if(link_connected && conn_blink_state) {
                        conn_blink_state = 0;
                }

                if(!link_connected) {
                        conn_blink_state = 1;
                        LEDworking = 1;
                        led_set_onoff(true);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        led_set_onoff(false);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        LEDworking = 0;
                }
                else if (ota_transfer_running) {
                        LEDworking = 1;
                        breath_pwm_counter++;
                        if (breath_pwm_counter >= breath_pwm_period) {
                                breath_pwm_counter = 0;
                                if (breath_dir > 0) {
                                        if (breath_duty < breath_pwm_period) {
                                                breath_duty++;
                                        } else {
                                                breath_dir = -1;
                                        }
                                } else {
                                        if (breath_duty > breath_min_duty) {
                                                breath_duty--;
                                        } else {
                                                breath_dir = 1;
                                        }
                                }
                        }
                        uint32_t duty = ((uint32_t)breath_duty * k_ledc_max_duty) / breath_pwm_period;
                        led_set_duty(duty);
                        vTaskDelay(breath_tick);
                }
                else if (link_connected && xSemaphoreTake(led_sys_mode_flag, 0) == pdTRUE) {
                        LEDworking = 1;
                        /* System mode indication: 2 blinks (Win) or 4 blinks (Android) */
                        led_blink(sys == SYS_WIN ? 2 : 4,
                                 sys == SYS_WIN ? 200 : 100,
                                 sys == SYS_WIN ? 200 : 100);
                        led_set_onoff(true);
                        LEDworking = 0;
                } else if (link_connected && xSemaphoreTake(led_work_mode_flag, 0) == pdTRUE) {
                        LEDworking = 1;
                        /* Work mode indication: 2 blinks */
                        led_blink(2, 200, 200);
                        led_set_onoff(true);
                        LEDworking = 0;
                } else {
                        LEDworking = 0;
                        led_set_onoff(true);
                        vTaskDelay(pdMS_TO_TICKS(20));
                }
        }
}

static void hidd_event_callback(esp_hidd_cb_event_t event,
                                esp_hidd_cb_param_t *param) {
  switch (event) {
  case ESP_HIDD_EVENT_REG_FINISH: {
    if (param->init_finish.state == ESP_HIDD_INIT_OK) {
      esp_err_t ret = esp_ble_gap_set_device_name(HIDD_DEVICE_NAME);
      if (ret != ESP_OK) {
        ESP_LOGE(HID_APP_TAG, "esp_ble_gap_set_device_name failed: %s",
                 esp_err_to_name(ret));
        break;
      }

      adv_data_ready = false;
      adv_config_done = ADV_CONFIG_FLAG | SCAN_RSP_CONFIG_FLAG;

      ret = esp_ble_gap_config_adv_data(&hidd_adv_data);
      if (ret != ESP_OK) {
        ESP_LOGE(HID_APP_TAG, "esp_ble_gap_config_adv_data failed: %s",
                 esp_err_to_name(ret));
        adv_config_done &= ~ADV_CONFIG_FLAG;
      }

      ret = esp_ble_gap_config_adv_data(&hidd_scan_rsp_data);
      if (ret != ESP_OK) {
        ESP_LOGE(HID_APP_TAG, "esp_ble_gap_config_scan_rsp_data failed: %s",
                 esp_err_to_name(ret));
        adv_config_done &= ~SCAN_RSP_CONFIG_FLAG;
      }

      adv_data_ready = true;
      start_advertising_if_ready();
    }
    break;
  }
  case ESP_BAT_EVENT_REG: {
    break;
  }
  case ESP_HIDD_EVENT_DEINIT_FINISH:
    break;
  case ESP_HIDD_EVENT_BLE_CONNECT: {
    ESP_LOGI(HID_APP_TAG, "ESP_HIDD_EVENT_BLE_CONNECT");
    app_ble_hid_set_hid_link_state(true);
    hid_conn_id = param->connect.conn_id;
    break;
  }
  case ESP_HIDD_EVENT_BLE_DISCONNECT: {
    app_ble_hid_set_hid_link_state(false);
    ESP_LOGI(HID_APP_TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
    start_advertising_if_ready();
    break;
  }
  case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
    ESP_LOGI(HID_APP_TAG, "%s, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT",
             __func__);
    ESP_LOG_BUFFER_HEX(HID_APP_TAG, param->vendor_write.data,
                       param->vendor_write.length);
    break;
  }
  case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
    ESP_LOGI(HID_APP_TAG, "ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT");
    ESP_LOG_BUFFER_HEX(HID_APP_TAG, param->led_write.data,
                       param->led_write.length);
    break;
  }
  default:
    break;
  }
  return;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
  switch (event) {
  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ESP_LOGI(HID_APP_TAG, "ADV data set complete, status=%d",
             param->adv_data_cmpl.status);
    if (param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS)
      adv_config_done &= ~ADV_CONFIG_FLAG;
    start_advertising_if_ready();
    break;
  case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
    ESP_LOGI(HID_APP_TAG, "Scan response set complete, status=%d",
             param->scan_rsp_data_cmpl.status);
    if (param->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS)
      adv_config_done &= ~SCAN_RSP_CONFIG_FLAG;
    start_advertising_if_ready();
    break;
  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      ESP_LOGI(HID_APP_TAG, "Advertising started");
    } else {
      ESP_LOGE(HID_APP_TAG, "Advertising start failed, status=0x%x",
               param->adv_start_cmpl.status);
    }
    break;
  case ESP_GAP_BLE_SEC_REQ_EVT:
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
      ESP_LOGD(HID_APP_TAG, "%x:", param->ble_security.ble_req.bd_addr[i]);
    }
    esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
    break;
  case ESP_GAP_BLE_AUTH_CMPL_EVT:
    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr,
           sizeof(esp_bd_addr_t));
    ESP_LOGI(HID_APP_TAG, "remote BD_ADDR: %08x%04x",
             (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) +
                 bd_addr[3],
             (bd_addr[4] << 8) + bd_addr[5]);
    ESP_LOGI(HID_APP_TAG, "address type = %d",
             param->ble_security.auth_cmpl.addr_type);
    ESP_LOGI(HID_APP_TAG, "pair status = %s",
             param->ble_security.auth_cmpl.success ? "success" : "fail");
    if (!param->ble_security.auth_cmpl.success) {
      ESP_LOGE(HID_APP_TAG, "fail reason = 0x%x",
               param->ble_security.auth_cmpl.fail_reason);
      app_ble_hid_set_hid_link_state(false);
      start_advertising_if_ready();
    }
    break;
  default:
    break;
  }
}


/* Trackball motion state for smoothing and adaptive acceleration */
static struct {
        uint8_t streak;
        uint8_t idle_frames;
        int8_t last_dir_x;
        int8_t last_dir_y;
        float filt_x;
        float filt_y;
        float residue_x;
        float residue_y;
} trackball_motion = {0};

/* Gesture volume acceleration state */
static struct {
        uint16_t last_consumer_key;
        uint8_t step;
        int64_t last_ts_us;
} gesture_volume_state = {0};

static int8_t sign_i16(int16_t val)
{
        if (val > 0)
                return 1;
        if (val < 0)
                return -1;
        return 0;
}

static void trackball_motion_reset(void)
{
        trackball_motion.streak = 0;
        trackball_motion.idle_frames = 0;
        trackball_motion.last_dir_x = 0;
        trackball_motion.last_dir_y = 0;
        trackball_motion.filt_x = 0;
        trackball_motion.filt_y = 0;
        trackball_motion.residue_x = 0;
        trackball_motion.residue_y = 0;
}

static inline bool is_volume_consumer_key(uint16_t key)
{
        return (key == HID_CONSUMER_VOLUME_UP || key == HID_CONSUMER_VOLUME_DOWN);
}

#if CONFIG_COLLECT_DATA_EN
hid_dev_work_state_t hid_ring_state = HID_DEV_AIR_MOUSE;
#else
hid_dev_work_state_t hid_ring_state = HID_DEV_TRACKBALL;
#endif
uint8_t gesture_recognition_on = false;

/*
 * nvs_read_sys_mode - Read system mode from NVS
 * @sys_mode: Output parameter for system mode
 *
 * Returns: ESP_OK on success, defaults to SYS_WIN if not found
 */
esp_err_t nvs_read_sys_mode(sys_t *sys_mode)
{
        nvs_handle_t nvs_handle;
        esp_err_t err;
        uint8_t sys_value;

        err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
                *sys_mode = SYS_WIN;
                ESP_LOGE(HID_APP_TAG, "nvs_read_sys_mode failed to open NVS: %s", esp_err_to_name(err));
                return err;
        }

        err = nvs_get_u8(nvs_handle, "sys_mode", &sys_value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
                *sys_mode = SYS_WIN;
                ESP_LOGI(HID_APP_TAG, "sys_mode not found, using default: %d", *sys_mode);
                err = nvs_set_u8(nvs_handle, "sys_mode", (uint8_t)*sys_mode);
                if (err == ESP_OK)
                        err = nvs_commit(nvs_handle);
                if (err != ESP_OK)
                        ESP_LOGW(HID_APP_TAG, "Failed to persist default sys_mode: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return ESP_OK;
        }
        if (err != ESP_OK) {
                nvs_close(nvs_handle);
                ESP_LOGE(HID_APP_TAG, "Failed to read sys_mode: %s", esp_err_to_name(err));
                *sys_mode = SYS_WIN;
                return err;
        }

        *sys_mode = (sys_t)sys_value;
        ESP_LOGI(HID_APP_TAG, "Read sys_mode: %d", *sys_mode);
        nvs_close(nvs_handle);
        return ESP_OK;
}

/*
 * nvs_write_sys_mode - Write system mode to NVS
 * @sys_mode: System mode to save
 *
 * Returns: ESP_OK on success
 */
esp_err_t nvs_write_sys_mode(sys_t sys_mode)
{
        nvs_handle_t nvs_handle;
        esp_err_t err;

        err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK)
                return err;

        err = nvs_set_u8(nvs_handle, "sys_mode", (uint8_t)sys_mode);
        if (err == ESP_OK) {
                err = nvs_commit(nvs_handle);
                ESP_LOGI(HID_APP_TAG, "Saved sys_mode: %d", sys_mode);
        }

        nvs_close(nvs_handle);
        return err;
}

/*
 * nvs_read_gyro_offset - Read gyroscope zero offset from NVS
 * @offset_x_val: Output X-axis offset
 * @offset_y_val: Output Y-axis offset
 *
 * Returns: ESP_OK on success, defaults to 0 if not found
 */
esp_err_t nvs_read_gyro_offset(float *offset_x_val, float *offset_y_val)
{
        nvs_handle_t nvs_handle;
        esp_err_t err;
        size_t size = sizeof(float);
        bool need_save_default = false;

        err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
                *offset_x_val = 0;
                *offset_y_val = 0;
                ESP_LOGE(HID_APP_TAG, "nvs_read_gyro_offset failed to open NVS: %s", esp_err_to_name(err));
                return err;
        }

        /* Read X offset */
        err = nvs_get_blob(nvs_handle, "offset_x_val", offset_x_val, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
                *offset_x_val = 0;
                ESP_LOGI(HID_APP_TAG, "offset_x not found, using default: 0");
                need_save_default = true;
        } else if (err != ESP_OK) {
                ESP_LOGE(HID_APP_TAG, "Failed to read offset_x: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return err;
        } else {
                ESP_LOGI(HID_APP_TAG, "Read offset_x: %.3f", *offset_x_val);
        }

        /* Read Y offset */
        size = sizeof(float);
        err = nvs_get_blob(nvs_handle, "offset_y_val", offset_y_val, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
                *offset_y_val = 0;
                ESP_LOGI(HID_APP_TAG, "offset_y not found, using default: 0");
                need_save_default = true;
        } else if (err != ESP_OK) {
                ESP_LOGE(HID_APP_TAG, "Failed to read offset_y: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                return err;
        } else {
                ESP_LOGI(HID_APP_TAG, "Read offset_y: %.3f", *offset_y_val);
        }

        if (need_save_default) {
                esp_err_t save_err = nvs_set_blob(nvs_handle, "offset_x_val", offset_x_val, sizeof(float));
                if (save_err == ESP_OK)
                        save_err = nvs_set_blob(nvs_handle, "offset_y_val", offset_y_val, sizeof(float));
                if (save_err == ESP_OK)
                        save_err = nvs_commit(nvs_handle);
                if (save_err != ESP_OK)
                        ESP_LOGW(HID_APP_TAG, "Failed to persist default gyro offsets: %s", esp_err_to_name(save_err));
        }

        nvs_close(nvs_handle);
        return ESP_OK;
}

/*
 * nvs_write_gyro_offset - Write gyroscope zero offset to NVS
 * @offset_x_val: X-axis offset
 * @offset_y_val: Y-axis offset
 *
 * Returns: ESP_OK on success
 */
esp_err_t nvs_write_gyro_offset(float offset_x_val, float offset_y_val)
{
        nvs_handle_t nvs_handle;
        esp_err_t err;

        err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
                ESP_LOGE(HID_APP_TAG, "Failed to open NVS: %s", esp_err_to_name(err));
                return err;
        }

        /* Write both offsets */
        err = nvs_set_blob(nvs_handle, "offset_x_val", &offset_x_val, sizeof(float));
        if (err == ESP_OK)
                err = nvs_set_blob(nvs_handle, "offset_y_val", &offset_y_val, sizeof(float));

        if (err == ESP_OK) {
                err = nvs_commit(nvs_handle);
                if (err == ESP_OK)
                        ESP_LOGI(HID_APP_TAG, "Saved gyro offset: X=%.3f, Y=%.3f",
                                 offset_x_val, offset_y_val);
        }

        if (err != ESP_OK)
                ESP_LOGE(HID_APP_TAG, "Failed to save gyro offset: %s", esp_err_to_name(err));

        nvs_close(nvs_handle);
        return err;
}

/*
 * Key handler functions
 */
static void handle_key_ball(void)
{
        ESP_LOGI(HID_APP_TAG, "KEY: KEY_BALL");
        ball_press_state = HID_MOUSE_LEFT;
        esp_hidd_send_mouse_value(hid_conn_id, HID_MOUSE_LEFT, 0, 0);
}

static void handle_key_touch_a_b(void)
{
        if ((hid_ring_state & HID_DEV_AIR_MOUSE) != HID_DEV_AIR_MOUSE) {
                ESP_LOGI(HID_APP_TAG,
                         "KEY: KEY_TOUCH_A_B_LPRESS - Not in air mouse mode, restart directly");
                esp_restart();
                return;
        }

        float err_x = 1, err_y = 1, target_x = 0, target_y = 0;

        ESP_LOGI(HID_APP_TAG,
                 "KEY: KEY_TOUCH_A_B_LPRESS - Air mouse calibration start");

        xSemaphoreGive(led_work_mode_flag);

        lsm6ds3tr_set_state(IMU_ACT);
        esp_timer_start_periodic(s_quat_timer, 10000);

        while (!((err_y <= AIR_MOUSE_OFFSET_PARA) && (err_y >= -AIR_MOUSE_OFFSET_PARA) &&
                 (err_x <= AIR_MOUSE_OFFSET_PARA) && (err_x >= -AIR_MOUSE_OFFSET_PARA))) {
                float airmouse_data[3] = {0};
                if (xQueueReceive(mouse_queue_handle, airmouse_data, 0) == pdTRUE) {
                        err_y = airmouse_data[0] - offset_y - target_y;
                        err_x = airmouse_data[1] - offset_x - target_x;

                        if (err_y > AIR_MOUSE_OFFSET_PARA)
                                offset_y += AIR_MOUSE_OFFSET_PARA/10.0;
                        else if (err_y < -AIR_MOUSE_OFFSET_PARA)
                                offset_y -= AIR_MOUSE_OFFSET_PARA/10.0;

                        if (err_x > AIR_MOUSE_OFFSET_PARA)
                                offset_x += AIR_MOUSE_OFFSET_PARA/10.0;
                        else if (err_x < -AIR_MOUSE_OFFSET_PARA)
                                offset_x -= AIR_MOUSE_OFFSET_PARA/10.0;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
        }

        ESP_LOGI(HID_APP_TAG, "Calibration done: offset_x=%.3f, offset_y=%.3f",
                 offset_x, offset_y);
        nvs_write_gyro_offset(offset_x, offset_y);
        esp_restart();
}

static void handle_key_touch_a_b_ball(void)
{
        ESP_LOGI(HID_APP_TAG, "KEY: KEY_TOUCH_A_B_BALL - Switching system mode");
        sys = (sys == SYS_WIN) ? SYS_ANDROID : SYS_WIN;
        ESP_LOGI(HID_APP_TAG, "New mode: %s", sys == SYS_WIN ? "Windows" : "Android");
        nvs_write_sys_mode(sys);
        esp_restart();
}

static void handle_key_touch_a(key_enum_t keynum)
{
        ESP_LOGI(HID_APP_TAG, "KEY: KEY_TOUCH_A");

        if (sys == SYS_ANDROID) {
                for (int i = 5; i > 0; i--)
                        esp_hidd_send_all_mouse_value(hid_conn_id, 0, 0, 0, i, 0);
        } else {
                esp_hidd_send_all_mouse_value(hid_conn_id, 0, 0, 0, 1, 0);
        }

        if (keynum == KEY_TOUCH_A_LPRESS)
                vTaskDelay(pdMS_TO_TICKS(100));
}

static void handle_key_touch_b(key_enum_t keynum)
{
#if CONFIG_COLLECT_DATA_EN
        static uint8_t timer_en_flag = 0;
        if (!timer_en_flag) {
                timer_en_flag = 1;
                lsm6ds3tr_set_state(IMU_ACT);
                esp_timer_start_periodic(s_quat_timer, 10000);
        }
        uint8_t collect_data_flag = 1;
        xQueueSend(collect_data_queue, &collect_data_flag, 0);
#else
        ESP_LOGI(HID_APP_TAG, "KEY: KEY_TOUCH_B");
        if (sys == SYS_ANDROID) {
                for (int i = 5; i > 0; i--)
                        esp_hidd_send_all_mouse_value(hid_conn_id, 0, 0, 0, -i, 0);
        } else {
                esp_hidd_send_all_mouse_value(hid_conn_id, 0, 0, 0, -1, 0);
        }

        if (keynum == KEY_TOUCH_B_LPRESS)
                vTaskDelay(pdMS_TO_TICKS(100));
#endif
}

static void handle_key_touch_a_double_click(void)
{
        ESP_LOGI(HID_APP_TAG, "KEY: KEY_TOUCH_A_DOUBLE_CLICK");

        if ((hid_ring_state & HID_DEV_TRACKBALL) == HID_DEV_TRACKBALL) {
                /* Switch to air mouse */
                hid_ring_state &= ~HID_DEV_TRACKBALL;
                hid_ring_state |= HID_DEV_AIR_MOUSE;
                ESP_LOGI(HID_APP_TAG, "Switched to AIR MOUSE");

                xSemaphoreGive(led_work_mode_flag);

                if ((hid_ring_state & HID_DEV_AIR_GESTURE) != HID_DEV_AIR_GESTURE) {
                        lsm6ds3tr_set_state(IMU_ACT);
                        esp_timer_start_periodic(s_quat_timer, 10000);
                }

                /* Clear mouse queue to remove old IMU data */
                float dummy[3];
                while (xQueueReceive(mouse_queue_handle, dummy, 0) == pdTRUE) {
                        /* Discard old data */
                }

                /* Reset air mouse filter state */
                extern void trig_mouse_reset(void);
                trig_mouse_reset();

                ESP_LOGI(HID_APP_TAG, "Cleared mouse queue and reset filters");

                /* Wait for IMU to stabilize */
                vTaskDelay(pdMS_TO_TICKS(100));

                vTaskSuspend(track_ball_task_handle);
        } else {
                /* Switch to trackball */
                hid_ring_state &= ~HID_DEV_AIR_MOUSE;
                hid_ring_state |= HID_DEV_TRACKBALL;
                ESP_LOGI(HID_APP_TAG, "Switched to TRACKBALL");
                trackball_motion_reset();
                mouse_dirCount[0] = 0;
                mouse_dirCount[1] = 0;

                xSemaphoreGive(led_work_mode_flag);

                if ((hid_ring_state & HID_DEV_AIR_GESTURE) != HID_DEV_AIR_GESTURE) {
                        lsm6ds3tr_set_state(IMU_SLEEP);
                        esp_timer_stop(s_quat_timer);
                }
                vTaskResume(track_ball_task_handle);
        }
}

static void handle_key_touch_b_double_click(void)
{
// #if !CONFIG_COLLECT_DATA_EN
        gesture_recognition_on = !gesture_recognition_on;

        ESP_LOGI(HID_APP_TAG, "Gesture recognition: %s",
                 gesture_recognition_on ? "ON" : "OFF");

        xSemaphoreGive(led_work_mode_flag);

        if (gesture_recognition_on) {
                hid_ring_state |= HID_DEV_AIR_GESTURE;

                /* Clear gesture queue to remove old results */
                gesture_class_t dummy;
                while (xQueueReceive(gesture_queue, &dummy, 0) == pdTRUE) {
                        /* Discard old gesture results */
                }
                ESP_LOGI(HID_APP_TAG, "Cleared gesture queue");

                if ((hid_ring_state & HID_DEV_AIR_MOUSE) != HID_DEV_AIR_MOUSE) {
                        lsm6ds3tr_set_state(IMU_ACT);
                        esp_timer_start_periodic(s_quat_timer, 10000);
                }

                /* Discard all gesture detection results for 1s after enabling. */
                app_gesture_detect_discard_for_ms(1000);

                /* Add delay to let IMU stabilize before resuming gesture task */
                vTaskDelay(pdMS_TO_TICKS(100));
                vTaskResume(app_gesture_detect_handle);
        } else {
                hid_ring_state &= ~HID_DEV_AIR_GESTURE;

                if ((hid_ring_state & HID_DEV_AIR_MOUSE) != HID_DEV_AIR_MOUSE) {
                        lsm6ds3tr_set_state(IMU_SLEEP);
                        esp_timer_stop(s_quat_timer);
                }
                vTaskSuspend(app_gesture_detect_handle);
                xSemaphoreGive(app_gesture_detect_suspend_flag);
                /* Clear IMU buffer */
                IMUbuffer_clear(&IMUbuffer);
        }
// #endif
}

void app_hid_logic_func(void)
{
        static bool ball_pressed_last = false;
        key_enum_t keynum = key_get_state();

#if !CONFIG_COLLECT_DATA_EN
        if (keynum != KEY_NONE)
                clear_sleep_count();
#endif

        /* Release mouse button only on KEY_BALL -> non-KEY_BALL transition. */
        if (keynum == KEY_BALL) {
                ball_pressed_last = true;
        } else {
                ball_press_state = HID_KEY_RESERVED;
                if (ball_pressed_last) {
                        esp_hidd_send_mouse_value(hid_conn_id, HID_KEY_RESERVED, 0, 0);
                        ball_pressed_last = false;
                }
        }

        /* Dispatch key events */
        switch (keynum) {
        case KEY_BALL:
                handle_key_ball();
                break;

#if !CONFIG_COLLECT_DATA_EN
        // case KEY_TOUCH_A_B:
        //         // handle_key_touch_a_b();
        //         printf("hello\r\n");
        case KEY_TOUCH_A_B_LPRESS:
                handle_key_touch_a_b();
                break;
        case KEY_TOUCH_A_B_BALL:
                handle_key_touch_a_b_ball();
                break;
#endif
        case KEY_TOUCH_A:
        case KEY_TOUCH_A_LPRESS:
                handle_key_touch_a(keynum);
                break;
        case KEY_TOUCH_B:
        case KEY_TOUCH_B_LPRESS:
                handle_key_touch_b(keynum);
                break;

#if !CONFIG_COLLECT_DATA_EN
        case KEY_TOUCH_A_DOUBLE_CLICK:
                handle_key_touch_a_double_click();
                break;
        case KEY_TOUCH_B_DOUBLE_CLICK:
                handle_key_touch_b_double_click();
                break;
#endif
        case KEY_NONE:
                /* Handle continuous input modes */
                break;
        default:
                break;
        }

/******************************hid_ring_state*******************************/
        /* Trackball mode */
        if (((hid_ring_state & HID_DEV_TRACKBALL) == HID_DEV_TRACKBALL)) {
                int16_t raw_x = mouse_dirCount[1];
                int16_t raw_y = mouse_dirCount[0];
                int8_t dir_x = sign_i16(raw_x);
                int8_t dir_y = sign_i16(raw_y);
                uint8_t accel_gain;
                float target_x, target_y;
                int16_t out_x, out_y;

                mouse_dirCount[0] = 0;
                mouse_dirCount[1] = 0;

                if (raw_x != 0 || raw_y != 0) {
                        bool reversed = false;

                        if (dir_x != 0 && trackball_motion.last_dir_x != 0 &&
                            dir_x != trackball_motion.last_dir_x)
                                reversed = true;
                        if (dir_y != 0 && trackball_motion.last_dir_y != 0 &&
                            dir_y != trackball_motion.last_dir_y)
                                reversed = true;

                        if (reversed) {
                                if (trackball_motion.streak > TRACKBALL_REVERSE_BRAKE_STEP)
                                        trackball_motion.streak -= TRACKBALL_REVERSE_BRAKE_STEP;
                                else
                                        trackball_motion.streak = 0;
                        }

                        if (trackball_motion.streak < TRACKBALL_STREAK_MAX)
                                trackball_motion.streak++;

                        trackball_motion.idle_frames = 0;
                        if (dir_x != 0)
                                trackball_motion.last_dir_x = dir_x;
                        if (dir_y != 0)
                                trackball_motion.last_dir_y = dir_y;
                } else {
                        if (trackball_motion.idle_frames < 255)
                                trackball_motion.idle_frames++;
                        if (trackball_motion.idle_frames >= TRACKBALL_IDLE_DECAY_FRAMES &&
                            trackball_motion.streak > 0)
                                trackball_motion.streak--;
                        if (trackball_motion.idle_frames >= CONFIG_OP_DELAY_MAX) {
                                trackball_motion.last_dir_x = 0;
                                trackball_motion.last_dir_y = 0;
                        }
                }

                accel_gain = 1 + (trackball_motion.streak / TRACKBALL_ACCEL_STEP_FRAMES);
                if (accel_gain > TRACKBALL_ACCEL_GAIN_MAX)
                        accel_gain = TRACKBALL_ACCEL_GAIN_MAX;

                target_x = (float)raw_x * accel_gain;
                target_y = (float)raw_y * accel_gain;

                /* Keep diagonal speed consistent with single-axis movement. */
                if (raw_x != 0 && raw_y != 0) {
                        target_x = (target_x * TRACKBALL_DIAGONAL_SCALE_NUM) /
                                   TRACKBALL_DIAGONAL_SCALE_DEN;
                        target_y = (target_y * TRACKBALL_DIAGONAL_SCALE_NUM) /
                                   TRACKBALL_DIAGONAL_SCALE_DEN;
                }

                trackball_motion.filt_x =
                        TRACKBALL_SMOOTH_ALPHA * target_x +
                        (1.0f - TRACKBALL_SMOOTH_ALPHA) * trackball_motion.filt_x;
                trackball_motion.filt_y =
                        TRACKBALL_SMOOTH_ALPHA * target_y +
                        (1.0f - TRACKBALL_SMOOTH_ALPHA) * trackball_motion.filt_y;

                trackball_motion.residue_x += trackball_motion.filt_x;
                trackball_motion.residue_y += trackball_motion.filt_y;

                out_x = (int16_t)trackball_motion.residue_x;
                out_y = (int16_t)trackball_motion.residue_y;

                trackball_motion.residue_x -= out_x;
                trackball_motion.residue_y -= out_y;

                if (out_x > 127)
                        out_x = 127;
                else if (out_x < -127)
                        out_x = -127;

                if (out_y > 127)
                        out_y = 127;
                else if (out_y < -127)
                        out_y = -127;

                if (!LEDworking && (out_x != 0 || out_y != 0)) {
#if !CONFIG_COLLECT_DATA_EN
                        clear_sleep_count();
#endif
                        ESP_LOGI(HID_APP_TAG, "x:%d, y:%d",
                                 out_x, out_y);

                        esp_hidd_send_mouse_value(hid_conn_id, ball_press_state,
                                                  (int8_t)out_x, (int8_t)out_y);
                }
        }
        /* Air mouse mode */
        else if (((hid_ring_state & HID_DEV_AIR_MOUSE) == HID_DEV_AIR_MOUSE)) {
#if CONFIG_AIRMOUSE_EN
                float airmouse_data[3] = {0};
                if (xQueueReceive(mouse_queue_handle, airmouse_data, 0) == pdTRUE) {
                        int8_t mouse_x, mouse_y;
                        trig_mouse_compute(airmouse_data[0], airmouse_data[1], airmouse_data[2],
                                          offset_y, offset_x, &mouse_x, &mouse_y);

#if !CONFIG_COLLECT_DATA_EN
                        if (mouse_x * mouse_x + mouse_y * mouse_y >= 5)
                                clear_sleep_count();

                        esp_hidd_send_mouse_value(hid_conn_id, ball_press_state, mouse_x, mouse_y);
#endif
                }
#endif
        }

        /* Gesture recognition */
        if ((hid_ring_state & HID_DEV_AIR_GESTURE) == HID_DEV_AIR_GESTURE) {
                gesture_class_t ges_res;
                if (xQueueReceive(gesture_queue, &ges_res, 0) == pdTRUE) {
#if !CONFIG_COLLECT_DATA_EN
                        clear_sleep_count();
#endif
                        ESP_LOGI(HID_APP_TAG, "Gesture detected: %d", ges_res);
                        send_gesture_command(ges_res, sys, hid_conn_id);
                }
        }
}

/*
 * send_gesture_command - Send gesture command based on system mode
 * @gesture: Detected gesture type
 * @sys: Current system mode (SYS_WIN or SYS_ANDROID)
 * @conn_id: HID connection ID
 */
static void send_gesture_command(gesture_class_t gesture, sys_t sys, uint16_t conn_id)
{
        /* Windows gesture mapping */
        static const struct {
                gesture_class_t gesture;
                uint16_t consumer_key;
                uint8_t keyboard_modifier;
                uint8_t keyboard_key;
        } win_gestures[] = {
                {GESTURE_LEFT, HID_CONSUMER_VOLUME_DOWN, 0, 0},
                {GESTURE_RIGHT, HID_CONSUMER_VOLUME_UP, 0, 0},
                {GESTURE_UP, 0, 0, HID_KEY_PAGE_DOWN},
                {GESTURE_DOEN, 0, 0, HID_KEY_PAGE_UP},
                {GESTURE_O, 0, 0, HID_KEY_PRNT_SCREEN},
                {GESTURE_X, 0, LEFT_ALT_KEY_MASK, HID_KEY_F4},
                {GESTURE_D, 0, LEFT_GUI_KEY_MASK, HID_KEY_D},
        };

        /* Android gesture mapping */
        static const struct {
                gesture_class_t gesture;
                uint16_t consumer_key;
                uint8_t keyboard_modifier;
                uint8_t keyboard_key;
        } android_gestures[] = {
                {GESTURE_LEFT, HID_CONSUMER_SCAN_PREV_TRK, 0, 0},
                {GESTURE_RIGHT, HID_CONSUMER_SCAN_NEXT_TRK, 0, 0},
                {GESTURE_UP, HID_CONSUMER_VOLUME_UP, 0, 0},
                {GESTURE_DOEN, HID_CONSUMER_VOLUME_DOWN, 0, 0},
                {GESTURE_O, 0, 0, HID_KEY_PRNT_SCREEN},
                {GESTURE_X, HID_CONSUMER_POWER, 0, 0},
                {GESTURE_D, 0, LEFT_GUI_KEY_MASK, HID_KEY_H},
        };

        const void *gesture_map;
        size_t map_size;

        if (sys == SYS_WIN) {
                gesture_map = win_gestures;
                map_size = sizeof(win_gestures) / sizeof(win_gestures[0]);
        } else {
                gesture_map = android_gestures;
                map_size = sizeof(android_gestures) / sizeof(android_gestures[0]);
        }

        /* Find and execute gesture command */
        for (size_t i = 0; i < map_size; i++) {
                const typeof(win_gestures[0]) *cmd =
                        (const typeof(win_gestures[0]) *)gesture_map + i;

                if (cmd->gesture != gesture)
                        continue;

                if (cmd->consumer_key) {
                        uint8_t repeat = 1;
                        if (is_volume_consumer_key(cmd->consumer_key)) {
                                const int64_t now_us = esp_timer_get_time();
                                const int64_t timeout_us = (int64_t)GESTURE_VOLUME_RESET_TIMEOUT_MS * 1000;
                                bool reset = false;

                                if (gesture_volume_state.step == 0)
                                        reset = true;
                                else if (gesture_volume_state.last_consumer_key != cmd->consumer_key)
                                        reset = true;
                                else if ((now_us - gesture_volume_state.last_ts_us) > timeout_us)
                                        reset = true;

                                if (reset) {
                                        gesture_volume_state.step = GESTURE_VOLUME_STEP_MIN;
                                } else if (gesture_volume_state.step < GESTURE_VOLUME_STEP_MAX) {
                                        gesture_volume_state.step += GESTURE_VOLUME_STEP_MIN;
                                }

                                gesture_volume_state.last_consumer_key = cmd->consumer_key;
                                gesture_volume_state.last_ts_us = now_us;
                                repeat = gesture_volume_state.step;

                                ESP_LOGI(HID_APP_TAG, "Gesture volume key=0x%X step=%u",
                                         cmd->consumer_key, repeat);
                        } else {
                                gesture_volume_state.last_consumer_key = 0;
                                gesture_volume_state.step = 0;
                                gesture_volume_state.last_ts_us = 0;
                        }

                        for (uint8_t n = 0; n < repeat; n++) {
                                esp_hidd_send_consumer_value(conn_id, cmd->consumer_key, true);
                                esp_hidd_send_consumer_value(conn_id, cmd->consumer_key, false);
                        }
                } else {
                        gesture_volume_state.last_consumer_key = 0;
                        gesture_volume_state.step = 0;
                        gesture_volume_state.last_ts_us = 0;
                        esp_hidd_send_keyboard_value(conn_id, cmd->keyboard_modifier,
                                                    (uint8_t[]){cmd->keyboard_key}, 1);
                        esp_hidd_send_keyboard_value(conn_id, 0,
                                                    (uint8_t[]){HID_KEY_RESERVED}, 1);
                }
                break;
        }
}


void app_ble_hid_task(void *pvParameters) {
  // vTaskDelay(1000 / portTICK_PERIOD_MS);
  while (1) {
    if (sec_conn || ota_conn) {
      vTaskDelay(10 / portTICK_PERIOD_MS);

      app_hid_logic_func();
    } else {
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}

void track_ball_edge_detect(void *pvParameters) {
  ball_init();
  mouse_dirCount[0] = 0;
  mouse_dirCount[1] = 0;
  while (1) {
    uint8_t direct = ball_get_direct();
    /* Suppress contradictory transitions on the same axis (likely glitches). */
    if ((direct & BALL_FORWARD) && (direct & BALL_BACK))
      direct &= ~(BALL_FORWARD | BALL_BACK);
    if ((direct & BALL_RIGHT) && (direct & BALL_LEFT))
      direct &= ~(BALL_RIGHT | BALL_LEFT);

    if ((direct & BALL_FORWARD) == BALL_FORWARD)
      mouse_dirCount[0] -= TRACKBALL_MOUSE_STEP;
    if ((direct & BALL_BACK) == BALL_BACK)
      mouse_dirCount[0] += TRACKBALL_MOUSE_STEP;
    if ((direct & BALL_RIGHT) == BALL_RIGHT)
      mouse_dirCount[1] += TRACKBALL_MOUSE_STEP;
    if ((direct & BALL_LEFT) == BALL_LEFT)
      mouse_dirCount[1] -= TRACKBALL_MOUSE_STEP;

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void vApplicationIdleHook(void) {}

static const char* TAG = "app_ble_hid";

esp_err_t app_ble_hid_init(void) {
  esp_err_t ret = ESP_OK;

  // 消息队列创建
  queue1 = xQueueCreate(1, sizeof(uint16_t));
  if (queue1 == NULL) {ESP_LOGE(TAG, "queue1 err!\r\n"); return ESP_FAIL;}
  // 空鼠使用“邮箱”语义：队列长度必须为1，配合 xQueueOverwrite 使用
  mouse_queue_handle = xQueueCreate(1, 3*sizeof(float));
  if (mouse_queue_handle == NULL) {ESP_LOGE(TAG, "mouse_queue_handle err!\r\n"); return ESP_FAIL;}
  // 信号量创建
  Semaphore_gesture = xSemaphoreCreateBinary();
  if (Semaphore_gesture == NULL) {ESP_LOGE(TAG, "Semaphore_gesture err!\r\n"); return ESP_FAIL;}
  led_sys_mode_flag = xSemaphoreCreateBinary();
  if (led_sys_mode_flag == NULL) {ESP_LOGE(TAG, "led_sys_mode_flag err!\r\n"); return ESP_FAIL;}
  led_work_mode_flag = xSemaphoreCreateBinary();
  if (led_work_mode_flag == NULL) {ESP_LOGE(TAG, "led_work_mode_flag err!\r\n"); return ESP_FAIL;}

#if CONFIG_COLLECT_DATA_EN
  collect_data_queue = xQueueCreate(1, sizeof(uint8_t));
#endif

#if CONFIG_DFS_EN
#if CONFIG_PM_ENABLE
  // Configure dynamic frequency scaling:
  // maximum and minimum frequencies are set in sdkconfig,
  // automatic light sleep is enabled if tickless idle support is enabled.
  ESP_LOGI(HID_APP_TAG, "Configuring PM...");
  pm_config.max_freq_mhz = 80;
  pm_config.min_freq_mhz = 10;
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
  pm_config.light_sleep_enable = 1;
#endif
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
#endif // CONFIG_PM_ENABLE
#endif

  ESP_LOGI(HID_APP_TAG, "Initializing Key...");
  key_init();

  // GPIO初始化
  ESP_LOGI(HID_APP_TAG, "Initializing LED GPIO...");
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << CONFIG_GPIO_LED_PM_NUM);
  io_conf.mode = GPIO_MODE_OUTPUT; // 输出模式
  io_conf.pull_up_en = 1;          // 使能上拉模式
  io_conf.pull_down_en = 0;        // 禁止下拉模式
  gpio_config(&io_conf);
  gpio_set_level(CONFIG_GPIO_LED_PM_NUM, 0);

  ledc_timer_config_t ledc_timer = {
    .speed_mode = k_ledc_mode,
    .duty_resolution = LEDC_TIMER_10_BIT,
    .timer_num = k_ledc_timer,
    .freq_hz = 5000,
    .clk_cfg = LEDC_AUTO_CLK,
  };
  if (ledc_timer_config(&ledc_timer) == ESP_OK) {
    ledc_channel_config_t ledc_channel = {
      .gpio_num = CONFIG_GPIO_LED_PM_NUM,
      .speed_mode = k_ledc_mode,
      .channel = k_ledc_channel,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = k_ledc_timer,
      .duty = 0,
      .hpoint = 0,
    };
    if (ledc_channel_config(&ledc_channel) == ESP_OK) {
      ledc_ready = true;
      led_set_onoff(false);
      ESP_LOGI(HID_APP_TAG, "LEDC enabled for LED breathing");
    } else {
      ESP_LOGW(HID_APP_TAG, "LEDC channel config failed, fallback to GPIO");
    }
  } else {
    ESP_LOGW(HID_APP_TAG, "LEDC timer config failed, fallback to GPIO");
  }

  // Initialize NVS.
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  // 从NVS读取系统模式
  nvs_read_sys_mode(&sys);
  if(sys == SYS_WIN)
    ESP_LOGI(TAG, "当前系统模式为: SYS_WIN");
  else if(sys == SYS_ANDROID)
    ESP_LOGI(TAG, "当前系统模式为: SYS_ANDROID");
  // 从NVS读取空鼠偏移值
  nvs_read_gyro_offset(&offset_x, &offset_y);
  ESP_LOGI(TAG, "当前空鼠偏移值: offset_x=%f, offset_y=%f", offset_x, offset_y);

  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  bt_cfg.bluetooth_mode = ESP_BT_MODE_BLE;
  // 启用蓝牙睡眠模式1（Modem sleep）降低功耗
  bt_cfg.sleep_mode = ESP_BT_SLEEP_MODE_1;
  bt_cfg.sleep_clock = ESP_BT_SLEEP_CLOCK_EXT_32K_XTAL;
  // 优化蓝牙控制器配置以降低功耗
  bt_cfg.ble_max_act = 3;   // 减少活动数量降低功耗

  ret = esp_bt_controller_init(&bt_cfg);
  if (ret) {
    ESP_LOGE(HID_APP_TAG, "%s initialize controller failed", __func__);
    return ret;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret) {
    ESP_LOGE(HID_APP_TAG, "%s enable controller failed", __func__);
    return ret;
  }

  ret = esp_bluedroid_init();
  if (ret) {
    ESP_LOGE(HID_APP_TAG, "%s init bluedroid failed", __func__);
    return ret;
  }

  ret = esp_bluedroid_enable();
  if (ret) {
    ESP_LOGE(HID_APP_TAG, "%s init bluedroid failed", __func__);
    return ret;
  }

  if ((ret = esp_hidd_profile_init()) != ESP_OK) {
    ESP_LOGE(HID_APP_TAG, "%s init bluedroid failed", __func__);
  }

  /// register the callback function to the gap module
  esp_ble_gap_register_callback(gap_event_handler);
  esp_hidd_register_callbacks(hidd_event_callback);

  /* Set BLE security parameters */
  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
  esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
  uint8_t key_size = 16;
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

  /* Print and adjust BLE TX power levels */
  static const struct {
          esp_ble_power_type_t type;
          const char *name;
  } power_types[] = {
          {ESP_BLE_PWR_TYPE_CONN_HDL0, "CONN_HDL0"},
          {ESP_BLE_PWR_TYPE_CONN_HDL1, "CONN_HDL1"},
          {ESP_BLE_PWR_TYPE_CONN_HDL2, "CONN_HDL2"},
          {ESP_BLE_PWR_TYPE_CONN_HDL3, "CONN_HDL3"},
          {ESP_BLE_PWR_TYPE_CONN_HDL4, "CONN_HDL4"},
          {ESP_BLE_PWR_TYPE_CONN_HDL5, "CONN_HDL5"},
          {ESP_BLE_PWR_TYPE_CONN_HDL6, "CONN_HDL6"},
          {ESP_BLE_PWR_TYPE_CONN_HDL7, "CONN_HDL7"},
          {ESP_BLE_PWR_TYPE_CONN_HDL8, "CONN_HDL8"},
          {ESP_BLE_PWR_TYPE_ADV, "ADV"},
          {ESP_BLE_PWR_TYPE_SCAN, "SCAN"},
          {ESP_BLE_PWR_TYPE_DEFAULT, "DEFAULT"},
  };

  ESP_LOGI(HID_APP_TAG, "BLE TX Power (before adjustment):");
  for (size_t i = 0; i < sizeof(power_types) / sizeof(power_types[0]); i++) {
          esp_power_level_t pwr = esp_ble_tx_power_get(power_types[i].type);
          ESP_LOGI(HID_APP_TAG, "  %s: %d dBm", power_types[i].name, pwr);
  }

  /* Adjust BLE TX power to -3dBm for better range/power balance */
  for (int i = 0; i <= 11; i++) {
          esp_ble_tx_power_set(i, ESP_PWR_LVL_P9);//n3
  }

  ESP_LOGI(HID_APP_TAG, "BLE TX Power (after adjustment to -3dBm):");
  for (size_t i = 0; i < sizeof(power_types) / sizeof(power_types[0]); i++) {
          esp_power_level_t pwr = esp_ble_tx_power_get(power_types[i].type);
          ESP_LOGI(HID_APP_TAG, "  %s: %d dBm", power_types[i].name, pwr);
  }

  xTaskCreate(&app_ble_hid_task, "app_ble_hid_task", 2048, NULL, 4, NULL);
  xTaskCreate(&les_task, "les_task", 2048, NULL, 1, NULL);
  xTaskCreate(&track_ball_edge_detect, "track_ball_edge_detect", 2048, NULL, 4,
              &track_ball_task_handle);

  return ret;
}
