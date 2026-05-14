#ifndef __LSM6DS3TR_QUATERNION_TEST_H__
#define __LSM6DS3TR_QUATERNION_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
    IMU_ACT = 0,
    IMU_SLEEP
} imu_state_t;

void lsm6ds3tr_set_state(imu_state_t state);
void lsm6ds3tr_quaternion_init(void);
void app_lsm6ds3tr_Quaternion(void);

#ifdef __cplusplus
}
#endif

#endif
