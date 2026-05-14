#include "app_ble_ota.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "app_ble_hid.h"

#define OTA_TAG "BLE_OTA"

#define OTA_GATTS_APP_ID 0x55
#define OTA_SERVICE_UUID 0xFFF0
#define OTA_CTRL_CHAR_UUID 0xFFF1
#define OTA_DATA_CHAR_UUID 0xFFF2

#define OTA_HANDLE_COUNT 8

#define IDX_SVC 0
#define IDX_CTRL_CHAR_DECL 1
#define IDX_CTRL_CHAR_VAL 2
#define IDX_CTRL_CCCD 3
#define IDX_DATA_CHAR_DECL 4
#define IDX_DATA_CHAR_VAL 5
#define IDX_DATA_CCCD 6

#define OTA_CTRL_START 0x01
#define OTA_CTRL_FINISH 0x02
#define OTA_CTRL_ABORT 0x03
#define OTA_BEGIN_IMAGE_SIZE OTA_WITH_SEQUENTIAL_WRITES
#define OTA_LOCAL_MTU 247
#define OTA_CONN_INT_MIN 6
#define OTA_CONN_INT_MAX 12
#define OTA_CONN_LATENCY 0
#define OTA_CONN_TIMEOUT 400
#define OTA_DATA_LOG_INTERVAL_PKTS 80

static uint16_t s_handle_table[OTA_HANDLE_COUNT];
static esp_gatt_if_t s_gatts_if = 0;
static uint16_t s_conn_id = 0;
static bool s_is_connected = false;
static bool s_ota_running = false;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static uint32_t s_ota_pkt_count = 0;
static uint32_t s_ota_total_bytes = 0;
static int64_t s_ota_start_us = 0;

static uint8_t s_char_prop_write_notify = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t s_cccd_default[2] = {0x00, 0x00};

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint16_t ota_service_uuid = OTA_SERVICE_UUID;
static const uint16_t ota_ctrl_uuid = OTA_CTRL_CHAR_UUID;
static const uint16_t ota_data_uuid = OTA_DATA_CHAR_UUID;

static const esp_gatts_attr_db_t ota_gatt_db[OTA_HANDLE_COUNT] = {
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                  sizeof(uint16_t), sizeof(ota_service_uuid), (uint8_t *)&ota_service_uuid}},

    [IDX_CTRL_CHAR_DECL] = {{ESP_GATT_AUTO_RSP},
                            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                             ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                             (uint8_t *)&s_char_prop_write_notify}},

    [IDX_CTRL_CHAR_VAL] = {{ESP_GATT_RSP_BY_APP},
                           {ESP_UUID_LEN_16, (uint8_t *)&ota_ctrl_uuid,
                            ESP_GATT_PERM_WRITE, 20, 0, NULL}},

    [IDX_CTRL_CCCD] = {{ESP_GATT_AUTO_RSP},
                       {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                        sizeof(uint16_t), sizeof(s_cccd_default), s_cccd_default}},

    [IDX_DATA_CHAR_DECL] = {{ESP_GATT_AUTO_RSP},
                            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid,
                             ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                             (uint8_t *)&s_char_prop_write_notify}},

    [IDX_DATA_CHAR_VAL] = {{ESP_GATT_RSP_BY_APP},
                           {ESP_UUID_LEN_16, (uint8_t *)&ota_data_uuid,
                            ESP_GATT_PERM_WRITE, 244, 0, NULL}},

    [IDX_DATA_CCCD] = {{ESP_GATT_AUTO_RSP},
                       {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                        sizeof(uint16_t), sizeof(s_cccd_default), s_cccd_default}},
};

static void ota_notify_status(uint8_t status)
{
    if (!s_is_connected || s_gatts_if == 0) {
        return;
    }
    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_handle_table[IDX_CTRL_CHAR_VAL],
                                1, &status, false);
}

static void ota_abort_internal(void)
{
    if (s_ota_running) {
        esp_ota_abort(s_ota_handle);
    }
    s_ota_running = false;
    app_ble_hid_set_ota_transfer_state(false);
    s_ota_handle = 0;
    s_update_partition = NULL;
}

static void handle_ctrl_write(uint8_t *data, uint16_t len)
{
    clear_sleep_count();
    ESP_LOGI(OTA_TAG, "RX 0x%04X/0x%04X len=%u", OTA_SERVICE_UUID, OTA_CTRL_CHAR_UUID, (unsigned)len);
    if (len < 1) {
        ota_notify_status(0xE0);
        return;
    }

    switch (data[0]) {
    case OTA_CTRL_START:
        ota_abort_internal();
        s_update_partition = esp_ota_get_next_update_partition(NULL);
        if (s_update_partition == NULL) {
            ESP_LOGE(OTA_TAG, "No OTA partition found");
            ota_notify_status(0xE1);
            return;
        }
        if (esp_ota_begin(s_update_partition, OTA_BEGIN_IMAGE_SIZE, &s_ota_handle) != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_begin failed");
            ota_notify_status(0xE2);
            return;
        }
        s_ota_running = true;
        s_ota_pkt_count = 0;
        s_ota_total_bytes = 0;
        s_ota_start_us = esp_timer_get_time();
        app_ble_hid_set_ota_transfer_state(true);
        ota_notify_status(0x11);
        ESP_LOGI(OTA_TAG, "OTA START");
        break;

    case OTA_CTRL_FINISH:
        if (!s_ota_running) {
            ota_notify_status(0xE3);
            return;
        }
        if (esp_ota_end(s_ota_handle) != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_end failed");
            ota_abort_internal();
            ota_notify_status(0xE4);
            return;
        }
        if (esp_ota_set_boot_partition(s_update_partition) != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_set_boot_partition failed");
            ota_abort_internal();
            ota_notify_status(0xE5);
            return;
        }
        s_ota_running = false;
        app_ble_hid_set_ota_transfer_state(false);
        s_ota_handle = 0;
        ota_notify_status(0x12);
        if (s_ota_start_us > 0) {
            int64_t elapsed_ms = (esp_timer_get_time() - s_ota_start_us) / 1000;
            ESP_LOGI(OTA_TAG, "OTA FINISH bytes=%lu pkts=%lu elapsed=%lld ms, rebooting...",
                     (unsigned long)s_ota_total_bytes,
                     (unsigned long)s_ota_pkt_count,
                     (long long)elapsed_ms);
        } else {
            ESP_LOGI(OTA_TAG, "OTA FINISH, rebooting...");
        }
        esp_restart();
        break;

    case OTA_CTRL_ABORT:
        ota_abort_internal();
        ota_notify_status(0x13);
        ESP_LOGW(OTA_TAG, "OTA ABORT");
        break;

    default:
        ota_notify_status(0xEF);
        break;
    }
}

static void handle_data_write(uint8_t *data, uint16_t len)
{
    clear_sleep_count();
    if (!s_ota_running) {
        ota_notify_status(0xE6);
        return;
    }
    if (len == 0) {
        return;
    }
    if (esp_ota_write(s_ota_handle, data, len) != ESP_OK) {
        ESP_LOGE(OTA_TAG, "esp_ota_write failed");
        ota_abort_internal();
        ota_notify_status(0xE7);
        return;
    }

    s_ota_pkt_count++;
    s_ota_total_bytes += len;
    if ((s_ota_pkt_count % OTA_DATA_LOG_INTERVAL_PKTS) == 0) {
        ESP_LOGI(OTA_TAG, "OTA RX progress bytes=%lu pkts=%lu",
                 (unsigned long)s_ota_total_bytes,
                 (unsigned long)s_ota_pkt_count);
    }
}

static void ota_gatts_event_handler(esp_gatts_cb_event_t event,
                                    esp_gatt_if_t gatts_if,
                                    esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        esp_ble_gap_set_device_name("Intring V1.0");
        esp_ble_gatts_create_attr_tab(ota_gatt_db, gatts_if, OTA_HANDLE_COUNT, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK &&
            param->add_attr_tab.num_handle == OTA_HANDLE_COUNT) {
            memcpy(s_handle_table, param->add_attr_tab.handles, sizeof(s_handle_table));
            esp_ble_gatts_start_service(s_handle_table[IDX_SVC]);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_is_connected = true;
        s_conn_id = param->connect.conn_id;
        app_ble_hid_set_ota_link_state(true);
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.min_int = OTA_CONN_INT_MIN;
        conn_params.max_int = OTA_CONN_INT_MAX;
        conn_params.latency = OTA_CONN_LATENCY;
        conn_params.timeout = OTA_CONN_TIMEOUT;
        esp_err_t conn_ret = esp_ble_gap_update_conn_params(&conn_params);
        if (conn_ret != ESP_OK) {
            ESP_LOGW(OTA_TAG, "update_conn_params failed: %s", esp_err_to_name(conn_ret));
        } else {
            ESP_LOGI(OTA_TAG, "Requested fast conn params min=%u max=%u",
                     conn_params.min_int, conn_params.max_int);
        }
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_is_connected = false;
        s_conn_id = 0;
        ota_abort_internal();
        app_ble_hid_set_ota_link_state(false);
        app_ble_hid_restart_advertising();
        break;

    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            if (!s_is_connected || param->write.conn_id != s_conn_id) {
                ESP_LOGW(OTA_TAG, "Drop write from stale conn_id=%u (active=%u)",
                         (unsigned)param->write.conn_id, (unsigned)s_conn_id);
                break;
            }
            if (param->write.need_rsp) {
                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(rsp));
                rsp.attr_value.handle = param->write.handle;
                rsp.attr_value.len = 0;
                esp_err_t rsp_ret = esp_ble_gatts_send_response(
                    gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, &rsp);
                if (rsp_ret != ESP_OK) {
                    ESP_LOGW(OTA_TAG, "send_response failed: %s", esp_err_to_name(rsp_ret));
                    break;
                }
            }

            if (param->write.handle == s_handle_table[IDX_CTRL_CHAR_VAL]) {
                handle_ctrl_write(param->write.value, param->write.len);
            } else if (param->write.handle == s_handle_table[IDX_DATA_CHAR_VAL]) {
                handle_data_write(param->write.value, param->write.len);
            }
        }
        break;

    default:
        break;
    }
}

esp_err_t app_ble_ota_init(void)
{
    esp_err_t mtu_ret = esp_ble_gatt_set_local_mtu(OTA_LOCAL_MTU);
    if (mtu_ret != ESP_OK) {
        ESP_LOGW(OTA_TAG, "set_local_mtu(%d) failed: %s", OTA_LOCAL_MTU, esp_err_to_name(mtu_ret));
    } else {
        ESP_LOGI(OTA_TAG, "Local MTU set to %d", OTA_LOCAL_MTU);
    }

    esp_err_t ret = esp_ble_gatts_register_callback(ota_gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(OTA_TAG, "register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gatts_app_register(OTA_GATTS_APP_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(OTA_TAG, "app register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(OTA_TAG, "BLE OTA service initialized");
    return ESP_OK;
}

bool app_ble_ota_is_running(void)
{
    return s_ota_running;
}
