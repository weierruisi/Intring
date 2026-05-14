#ifndef IMU_BUFFER_H
#define IMU_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "model_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int8_t buffer[kFeatureElementCount];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    SemaphoreHandle_t bufMutex;
}CircularBuffer_t;

void IMUbuffer_init(CircularBuffer_t* pBuffer);
bool IMUbuffer_isfull(CircularBuffer_t* pBuffer);
bool IMUbuffer_isempty(CircularBuffer_t* pBuffer);
void IMUbuffer_add(CircularBuffer_t* pBuffer, int8_t value);
bool IMUbuffer_get(CircularBuffer_t* pBuffer, uint16_t offset, int8_t* value);
void IMUbuffer_clear(CircularBuffer_t* pBuffer);

#ifdef __cplusplus
}
#endif

#endif

