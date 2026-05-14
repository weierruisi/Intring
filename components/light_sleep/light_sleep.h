/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_check.h"

/* Use boot button as gpio input */
#define GPIO_WAKEUP_NUM_1         CONFIG_GPIO_TOUCHKEY_A
#define GPIO_WAKEUP_NUM_2         CONFIG_GPIO_TOUCHKEY_B
/* "Boot" button is active low */
#define GPIO_WAKEUP_LEVEL       0

esp_err_t register_gpio_wakeup(void);


