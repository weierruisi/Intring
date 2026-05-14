/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "light_sleep.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "project_config.h"
#include "esp_log.h"

/* Most development boards have "boot" button attached to GPIO0.
 * You can also change this to another pin.
 */


static const char *TAG = "gpio_wakeup";

void wait_gpio_inactive(void)
{
    printf("Waiting for GPIO%d and GPIO%d to go high...\n", GPIO_WAKEUP_NUM_1, GPIO_WAKEUP_NUM_2);
    while (gpio_get_level(GPIO_WAKEUP_NUM_1) == GPIO_WAKEUP_LEVEL && \
           gpio_get_level(GPIO_WAKEUP_NUM_2) == GPIO_WAKEUP_LEVEL) {
        ESP_LOGE(TAG, "gpio wake up level set err");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t register_gpio_wakeup(void)
{
    // /* Initialize GPIO */
    // gpio_config_t config = {
    //         .pin_bit_mask = BIT64(GPIO_WAKEUP_NUM),
    //         .mode = GPIO_MODE_INPUT,
    //         .pull_down_en = false,
    //         .pull_up_en = GPIO_PULLUP_ENABLE,
    //         // .intr_type = GPIO_INTR_DISABLE
    // };

    // ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "Initialize GPIO%d failed", GPIO_WAKEUP_NUM);

    /* Enable wake up from GPIO */
    // ESP_RETURN_ON_ERROR(gpio_wakeup_enable(GPIO_WAKEUP_NUM, GPIO_WAKEUP_LEVEL == 0 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL),
    //                     TAG, "Enable gpio wakeup failed");
    ESP_RETURN_ON_ERROR(esp_deep_sleep_enable_gpio_wakeup((1ULL << GPIO_WAKEUP_NUM_1), GPIO_WAKEUP_LEVEL == 0 ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH),
                        TAG, "Enable gpio wakeup failed");
    ESP_RETURN_ON_ERROR(esp_deep_sleep_enable_gpio_wakeup((1ULL << GPIO_WAKEUP_NUM_2), GPIO_WAKEUP_LEVEL == 0 ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH),
                        TAG, "Enable gpio wakeup failed");
    // ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "Configure gpio as wakeup source failed");

    /* Make sure the GPIO is inactive and it won't trigger wakeup immediately */
    wait_gpio_inactive();
    ESP_LOGI(TAG, "gpio wakeup source is ready");

    return ESP_OK;
}
