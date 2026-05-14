#ifndef APP_GESTURE_DETECT_H
#define APP_GESTURE_DETECT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
    GESTURE_LEFT = 0,
    GESTURE_RIGHT,
    GESTURE_UP,
    GESTURE_DOEN,
    GESTURE_O,
    GESTURE_X,
    GESTURE_D,
    GESTURE_IDLE,
} gesture_class_t;

int8_t float2int(float value);

void app_gesture_detect_init(void);
void app_gesture_detect_discard_for_ms(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif
