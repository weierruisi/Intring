#include <stdio.h>
#include "imu_buffer.h"
#include "data_share.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/semphr.h"

static const char* TAG = "imu_buffer";

void IMUbuffer_init(CircularBuffer_t* pBuffer)
{
    if (pBuffer == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_init null pBuffer");
        return;
    }
    pBuffer->head = 0;
    pBuffer->tail = 0;
    pBuffer->count = 0;
    pBuffer->bufMutex = xSemaphoreCreateMutex();
    if (pBuffer->bufMutex == NULL) 
    {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex err");
        return;
    }
}

bool IMUbuffer_isfull(CircularBuffer_t* pBuffer)
{
    return (pBuffer != NULL) && (pBuffer->count == kFeatureElementCount);
}

bool IMUbuffer_isempty(CircularBuffer_t* pBuffer)
{
    return (pBuffer == NULL) || (pBuffer->count == 0);
}

void IMUbuffer_add(CircularBuffer_t* pBuffer, int8_t value)
{
    if (pBuffer == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_add null pBuffer");
        return;
    }
    if (pBuffer->bufMutex == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_add no mutex");
        return;
    }
    xSemaphoreTake(pBuffer->bufMutex, portMAX_DELAY);
    pBuffer->buffer[pBuffer->head] = value;
    pBuffer->head = (pBuffer->head + 1) % kFeatureElementCount;

    if(IMUbuffer_isfull(pBuffer))
        pBuffer->tail = (pBuffer->tail + 1) % kFeatureElementCount;
    else
        pBuffer->count++;
    xSemaphoreGive(pBuffer->bufMutex);
}

bool IMUbuffer_get(CircularBuffer_t* pBuffer, uint16_t offset, int8_t* value)
{
    if (pBuffer == NULL || value == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_get null arg");
        return false;
    }
    if (pBuffer->bufMutex == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_get no mutex");
        return false;
    }
    if(offset >= kFeatureElementCount || !IMUbuffer_isfull(pBuffer)){
        ESP_LOGE(TAG, "IMUbuffer_get err");
        return false;
    }

    xSemaphoreTake(pBuffer->bufMutex, portMAX_DELAY);
    uint16_t index = offset + pBuffer->tail;
    index %= kFeatureElementCount;
    *value = pBuffer->buffer[index];
    xSemaphoreGive(pBuffer->bufMutex);

    return true;
}

void IMUbuffer_clear(CircularBuffer_t* pBuffer)
{
    if (pBuffer == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_clear null pBuffer");
        return;
    }
    if (pBuffer->bufMutex == NULL) {
        ESP_LOGE(TAG, "IMUbuffer_clear no mutex");
        return;
    }
    xSemaphoreTake(pBuffer->bufMutex, portMAX_DELAY);
    pBuffer->tail = pBuffer->head;
    pBuffer->count = 0;
    xSemaphoreGive(pBuffer->bufMutex);
}
