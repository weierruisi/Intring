#ifndef LSM6DS3TR_C_READ_DATA_POLLING_H
#define LSM6DS3TR_C_READ_DATA_POLLING_H

#define ESP_IDF
/* Includes ------------------------------------------------------------------*/
#include "lsm6ds3tr-c_reg.h"
#include <string.h>
#include <stdio.h>

#if defined(NUCLEO_F401RE)
#include "stm32f4xx_hal.h"
#include "usart.h"
#include "gpio.h"
#include "i2c.h"

#elif defined(STEVAL_MKI109V3)
#include "stm32f4xx_hal.h"
#include "usbd_cdc_if.h"
#include "gpio.h"
#include "spi.h"
#include "tim.h"

#elif defined(SPC584B_DIS)
#include "components.h"

#elif defined(ESP_IDF)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

#endif


#if LSM6DS3TR_C_READ_DATA_POLLING_EN
void lsm6ds3tr_c_read_data_polling(void);
#endif

/* Private macro -------------------------------------------------------------*/
#define    BOOT_TIME            15 //ms
#define    TX_BUF_DIM         1000

#define I2C_NUM I2C_NUM_0

/* Private functions ---------------------------------------------------------*/

/*
 *   WARNING:
 *   Functions declare in this section are defined at the end of this file
 *   and are strictly related to the hardware platform used.
 *
 */
int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len);
int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len);
void tx_com( uint8_t *tx_buffer, uint16_t len );
void platform_delay(uint32_t ms);
void platform_init(void);

#endif
