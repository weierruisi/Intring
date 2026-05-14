#ifndef __KEYREAD_H__
#define __KEYREAD_H__
#include "project_config.h"
#include <stdint.h>


typedef enum {
  KEY_NONE                 = 0,
  KEY_BALL                 = 0x01 << 0,
  KEY_TOUCH_A              = 0x01 << 1,
  KEY_TOUCH_B              = 0x01 << 2,
  KEY_TOUCH_A_LPRESS       = 0x01 << 3,
  KEY_TOUCH_B_LPRESS       = 0x01 << 4,
  KEY_TOUCH_A_DOUBLE_CLICK = 0x01 << 5,
  KEY_TOUCH_B_DOUBLE_CLICK = 0x01 << 6,
  KEY_TOUCH_A_B            = 0x01 << 7,           // A+B同时按下
  KEY_TOUCH_A_B_BALL       = 0x01 << 8,      // A+B+BALL三键同时按下
  KEY_TOUCH_A_B_LPRESS     = 0x01 << 9,           // A+B同时长按
} key_enum_t;

typedef enum {
  BALL_NONE = 0x01,
  BALL_FORWARD = 0x02,
  BALL_BACK = 0x04,
  BALL_RIGHT = 0x08,
  BALL_LEFT = 0x10
} ball_enum_t;

void ball_init(void);
uint8_t ball_get_direct(void);
void key_init(void);
key_enum_t key_get_state(void);
#if CONFIG_TOUCH_SWICH_TEST_EN
void touch_key_test(void);
#endif
#if CONFIG_TRACK_BALL_TEST_EN
void track_ball_test(void);
#endif

#endif
