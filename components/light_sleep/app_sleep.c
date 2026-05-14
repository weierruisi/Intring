/*
 * Application Sleep Management
 *
 * Implements automatic sleep functionality with timer-based inactivity
 * detection and GPIO wakeup support.
 */

#include "app_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data_share.h"
#include "driver/gptimer.h"
#include "driver/uart.h"
#include "lsm6ds3tr_quaternion_test.h"
#include "app_ble_hid.h"

static const char *TAG = "app_sleep";

/* Timer handle */
static gptimer_handle_t sleep_timer;

/* Timer data structure */
struct queue_element {
        uint64_t event_count;
};

/* FreeRTOS queue */
static QueueHandle_t sleep_queue;

/*
 * clear_sleep_count - Reset sleep timer counter
 *
 * Call this on any user activity to prevent sleep
 */
void clear_sleep_count(void)
{
        gptimer_set_raw_count(sleep_timer, 0);
}

/*
 * timer_on_alarm_cb - Timer alarm callback
 * @timer: Timer handle
 * @edata: Event data
 * @user_ctx: User context (queue handle)
 *
 * Returns: true if higher priority task was woken
 *
 * Called when sleep timer expires, sends event to queue
 */
static bool timer_on_alarm_cb(gptimer_handle_t timer,
                              const gptimer_alarm_event_data_t *edata,
                              void *user_ctx)
{
        BaseType_t high_task_awoken = pdFALSE;
        QueueHandle_t queue = (QueueHandle_t)user_ctx;
        struct queue_element ele;

        /* Retrieve count value from event data */
        ele.event_count = edata->count_value;

        /* Send event data to main task via queue */
        xQueueSendFromISR(queue, &ele, &high_task_awoken);

        return (high_task_awoken == pdTRUE);
}

/*
 * app_sleep - Sleep management task
 * @ptr: Task parameter (unused)
 *
 * Monitors inactivity and triggers deep sleep after timeout
 */
void app_sleep(void *ptr)
{
        struct queue_element ele;
        gptimer_config_t timer_config = {
                .clk_src = GPTIMER_CLK_SRC_DEFAULT,
                .direction = GPTIMER_COUNT_UP,
                .resolution_hz = 1 * 1000 * 1000,
                .intr_priority = 0,
        };
        gptimer_alarm_config_t alarm_config = {
                .reload_count = 0,
                .alarm_count = 30 * 1000000,  /* 30 seconds */
                .flags.auto_reload_on_alarm = true,
        };
        gptimer_event_callbacks_t cbs = {
                .on_alarm = timer_on_alarm_cb,
        };

        /* Initialize queue */
        sleep_queue = xQueueCreate(1, sizeof(struct queue_element));

        /* Create general purpose timer */
        ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &sleep_timer));
        ESP_ERROR_CHECK(gptimer_set_alarm_action(sleep_timer, &alarm_config));
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(sleep_timer, &cbs,
                                                         sleep_queue));
        ESP_ERROR_CHECK(gptimer_enable(sleep_timer));
        ESP_ERROR_CHECK(gptimer_start(sleep_timer));

        while (1) {
                xQueueReceive(sleep_queue, &ele, portMAX_DELAY);
                printf("event_count: %lld\r\n", ele.event_count);
                printf("Entering deep sleep\n");

                /* Disable IMU if air mouse or gesture detection is active */
                if (((hid_ring_state & HID_DEV_AIR_MOUSE) ==
                     HID_DEV_AIR_MOUSE) ||
                    ((hid_ring_state & HID_DEV_AIR_GESTURE) ==
                     HID_DEV_AIR_GESTURE)) {
                        printf("IMU_SLEEP\n");
                        lsm6ds3tr_set_state(IMU_SLEEP);
                }

                /* Wait for UART TX to complete */
                uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);

                /* Enter deep sleep */
                esp_deep_sleep_start();

                /* Re-enable IMU after wakeup */
                if (((hid_ring_state & HID_DEV_AIR_MOUSE) ==
                     HID_DEV_AIR_MOUSE) ||
                    ((hid_ring_state & HID_DEV_AIR_GESTURE) ==
                     HID_DEV_AIR_GESTURE)) {
                        printf("IMU_ACT\n");
                        lsm6ds3tr_set_state(IMU_ACT);
                }

                ESP_LOGI(TAG, "esp wake up");
        }
}

/*
 * app_sleep_init - Initialize sleep management
 *
 * Registers GPIO wakeup sources and creates sleep task
 */
void app_sleep_init(void)
{
        register_gpio_wakeup();
        xTaskCreate(app_sleep, "app_sleep", 4096, NULL, 5, NULL);
}
