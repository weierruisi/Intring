#ifndef __LSM6DS3TR_H__
#define __LSM6DS3TR_H__

#include "lsm6ds3tr_c_read_data_polling.h"
#include "lsm6ds3tr_quaternion_test.h"
#include "project_config.h"

void lsm6ds3tr_init(void);

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern QueueHandle_t mouse_queue_handle;
void lsm6ds3tr_GetAcc(float *buf);
void lsm6ds3tr_GetAgl(float *buf);

#if LSM6DS3TR_COLLECT_DATA_EN
void lsm6ds3tr_CollectData(void);
#endif

#endif
