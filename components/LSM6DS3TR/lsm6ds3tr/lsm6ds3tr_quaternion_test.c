/*
 * LSM6DS3TR Quaternion Test - IMU sensor fusion using Madgwick filter
 *
 * This module implements quaternion-based orientation tracking using
 * the Madgwick AHRS algorithm for sensor fusion.
 */

#include "lsm6ds3tr_quaternion_test.h"
#include "data_share.h"
#include "lsm6ds3tr_c_read_data_polling.h"
#include "project_config.h"
#include "app_gesture_detect.h"
#include "imu_buffer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_IMU_QUATERNTION_TEST_EN

/* Madgwick filter parameter (adjustable based on sampling rate) */
#define BETA 0.5f

/* Quaternion state variables */
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;

/* Task and timer handles */
static TaskHandle_t quat_task_handle;
esp_timer_handle_t s_quat_timer;

/* Reference yaw angle (user forward direction) and lock state */
static float ref_yaw;
static bool ref_yaw_locked;

/* Device context */
static stmdev_ctx_t dev_ctx;
static uint8_t i2c_install_flag;

/*
 * inv_sqrt - Fast inverse square root
 * @x: Input value
 *
 * Returns: 1/sqrt(x)
 */
static float inv_sqrt(float x)
{
        return 1.0f / sqrtf(x);
}

/*
 * madgwick_ahrs_update_imu - Update quaternion using Madgwick AHRS algorithm
 * @gx, @gy, @gz: Gyroscope readings (rad/s)
 * @ax, @ay, @az: Accelerometer readings (g)
 * @dt: Time step (seconds)
 *
 * Updates global quaternion state (q0, q1, q2, q3)
 */
static void madgwick_ahrs_update_imu(float gx, float gy, float gz,
                                     float ax, float ay, float az, float dt)
{
        float recip_norm;
        float s0, s1, s2, s3;
        float q_dot1, q_dot2, q_dot3, q_dot4;
        float _2q0, _2q1, _2q2, _2q3;
        float _4q0, _4q1, _4q2;
        float _8q1, _8q2;
        float q0q0, q1q1, q2q2, q3q3;

        /* Rate of change of quaternion from gyroscope */
        q_dot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        q_dot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
        q_dot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
        q_dot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

        /* Compute feedback only if accelerometer valid (avoid NaN) */
        if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
                /* Normalize accelerometer measurement */
                recip_norm = inv_sqrt(ax * ax + ay * ay + az * az);
                ax *= recip_norm;
                ay *= recip_norm;
                az *= recip_norm;

                /* Auxiliary variables to avoid repeated arithmetic */
                _2q0 = 2.0f * q0;
                _2q1 = 2.0f * q1;
                _2q2 = 2.0f * q2;
                _2q3 = 2.0f * q3;
                _4q0 = 4.0f * q0;
                _4q1 = 4.0f * q1;
                _4q2 = 4.0f * q2;
                _8q1 = 8.0f * q1;
                _8q2 = 8.0f * q2;
                q0q0 = q0 * q0;
                q1q1 = q1 * q1;
                q2q2 = q2 * q2;
                q3q3 = q3 * q3;

                /* Gradient descent algorithm corrective step */
                s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
                s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay -
                     _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
                s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay -
                     _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
                s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 -
                     _2q2 * ay;

                /* Normalize step magnitude */
                recip_norm = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
                s0 *= recip_norm;
                s1 *= recip_norm;
                s2 *= recip_norm;
                s3 *= recip_norm;

                /* Apply feedback step */
                q_dot1 -= BETA * s0;
                q_dot2 -= BETA * s1;
                q_dot3 -= BETA * s2;
                q_dot4 -= BETA * s3;
        }

        /* Integrate quaternion using precise dt */
        q0 += q_dot1 * dt;
        q1 += q_dot2 * dt;
        q2 += q_dot3 * dt;
        q3 += q_dot4 * dt;

        /* Normalize quaternion */
        recip_norm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
        q0 *= recip_norm;
        q1 *= recip_norm;
        q2 *= recip_norm;
        q3 *= recip_norm;
}

/*
 * quaternion_rotate_vector - Rotate vector using quaternion
 * @qw, @qx, @qy, @qz: Quaternion components
 * @vx, @vy, @vz: Input vector (body frame)
 * @rx, @ry, @rz: Output vector (world frame)
 *
 * Transforms vector from body coordinate frame to world coordinate frame
 */
static void quaternion_rotate_vector(float qw, float qx, float qy, float qz,
                                     float vx, float vy, float vz,
                                     float *rx, float *ry, float *rz)
{
        float ww = qw * qw;
        float xx = qx * qx;
        float yy = qy * qy;
        float zz = qz * qz;
        float wx = qw * qx;
        float wy = qw * qy;
        float wz = qw * qz;
        float xy = qx * qy;
        float xz = qx * qz;
        float yz = qy * qz;

        /* Apply rotation matrix R * v */
        *rx = (ww + xx - yy - zz) * vx + 2.0f * (xy - wz) * vy +
              2.0f * (xz + wy) * vz;
        *ry = 2.0f * (xy + wz) * vx + (ww - xx + yy - zz) * vy +
              2.0f * (yz - wx) * vz;
        *rz = 2.0f * (xz - wy) * vx + 2.0f * (yz + wx) * vy +
              (ww - xx - yy + zz) * vz;
}

/*
 * quaternion_get_yaw - Extract yaw angle from quaternion
 * @qw, @qx, @qy, @qz: Quaternion components
 *
 * Returns: Yaw angle (rotation around Z-axis) in range [-pi, pi]
 */
static float quaternion_get_yaw(float qw, float qx, float qy, float qz)
{
        float siny_cosp = 2.0f * (qw * qz + qx * qy);
        float cosy_cosp = 1.0f - 2.0f * (qy * qy + qz * qz);
        return atan2f(siny_cosp, cosy_cosp);
}

/*
 * quat_timer_callback - Timer callback to wake quaternion task
 * @arg: Task handle
 *
 * Called at 100Hz to trigger quaternion update
 */
static void quat_timer_callback(void *arg)
{
        TaskHandle_t task = (TaskHandle_t)arg;
        BaseType_t higher_woken = pdFALSE;

        if (task == NULL)
                return;

        vTaskNotifyGiveFromISR(task, &higher_woken);
        if (higher_woken == pdTRUE)
                portYIELD_FROM_ISR();
}

/*
 * lsm6ds3tr_quaternion_init - Initialize LSM6DS3TR sensor for quaternion mode
 *
 * Configures sensor with 104Hz ODR and appropriate full-scale ranges
 */
void lsm6ds3tr_quaternion_init(void)
{
        uint8_t whoami;
        uint8_t rst;

        if (!i2c_install_flag) {
                i2c_install_flag = 1;

                /* Initialize MEMS driver interface */
                dev_ctx.write_reg = platform_write;
                dev_ctx.read_reg = platform_read;
                dev_ctx.mdelay = platform_delay;

                /* Initialize platform */
                platform_init();

                /* Wait for sensor boot time */
                platform_delay(BOOT_TIME);
        }

        /* Check device ID */
        whoami = 0;
        lsm6ds3tr_c_device_id_get(&dev_ctx, &whoami);
        if ((whoami != LSM6DS3TR_C_ID1) && (whoami != LSM6DS3TR_C_ID2)) {
                printf("Device not found! whoamI: %02x\n", whoami);
                while (1)
                        vTaskDelay(100);
        }
        printf("Device found! whoamI: %02x\n", whoami);

        /* Restore default configuration */
        lsm6ds3tr_c_reset_set(&dev_ctx, PROPERTY_ENABLE);
        do {
                lsm6ds3tr_c_reset_get(&dev_ctx, &rst);
        } while (rst);

        /* Enable Block Data Update */
        lsm6ds3tr_c_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);

        /* Set Output Data Rate - approximately 104Hz */
        lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_XL_ODR_104Hz);
        lsm6ds3tr_c_gy_data_rate_set(&dev_ctx, LSM6DS3TR_C_GY_ODR_104Hz);

        /* Set full scale */
        lsm6ds3tr_c_xl_full_scale_set(&dev_ctx, LSM6DS3TR_C_4g);
        lsm6ds3tr_c_gy_full_scale_set(&dev_ctx, LSM6DS3TR_C_2000dps);

        printf("Starting data loop...\n");

        /* Record current task handle and start 100Hz periodic timer */
        quat_task_handle = xTaskGetCurrentTaskHandle();
        if (s_quat_timer == NULL) {
                esp_timer_create_args_t timer_args = {
                        .callback = &quat_timer_callback,
                        .arg = (void *)quat_task_handle,
                        .dispatch_method = ESP_TIMER_TASK,
                        .name = "quat_100hz",
                };

                if (esp_timer_create(&timer_args, &s_quat_timer) == ESP_OK) {
                        ESP_LOGI("LSM6DS3TR_QUATERNION_TEST",
                                 "esp_timer_create is ok");
                        /* 100Hz -> period 10,000 us */
                        esp_timer_start_periodic(s_quat_timer, 10000);
#if !CONFIG_COLLECT_DATA_EN
                        esp_timer_stop(s_quat_timer);
#endif
                }
        }
}

int64_t last_time_us = 0;
// uint8_t sample_count = 0;
/*
 * lsm6ds3tr_set_state - Set IMU power state
 * @state: Target state (IMU_ACT or IMU_SLEEP)
 *
 * Controls IMU power mode and manages associated filter states
 */
void lsm6ds3tr_set_state(imu_state_t state)
{
        if (state == IMU_ACT) {
                ESP_LOGI("LSM6DS3TR_QUATERNION_TEST", "IMU set state act");

                /* Start IMU */
                lsm6ds3tr_c_xl_data_rate_set(&dev_ctx,
                                              LSM6DS3TR_C_XL_ODR_104Hz);
                lsm6ds3tr_c_gy_data_rate_set(&dev_ctx,
                                              LSM6DS3TR_C_GY_ODR_104Hz);

                /* Wait for IMU to stabilize (important!) */
                vTaskDelay(pdMS_TO_TICKS(50));

                /* Reset air mouse filter state */
                extern void trig_mouse_reset(void);
                trig_mouse_reset();

                last_time_us = 0;
                // sample_count = 0;

                ESP_LOGI("LSM6DS3TR_QUATERNION_TEST",
                         "IMU stabilized and filters reset");
        } else if (state == IMU_SLEEP) {
                ESP_LOGI("LSM6DS3TR_QUATERNION_TEST", "IMU set state sleep");
                lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_XL_ODR_OFF);
                lsm6ds3tr_c_gy_data_rate_set(&dev_ctx, LSM6DS3TR_C_GY_ODR_OFF);
        }
}

/*
 * process_imu_sample - Process single IMU sample and update quaternion
 * @last_time_us: Pointer to last timestamp (updated on return)
 * @sample_count: Pointer to gesture sample counter (updated on return)
 *
 * Returns: true if sample processed successfully, false otherwise
 *
 * This function encapsulates the core IMU processing pipeline:
 * 1. Read raw sensor data
 * 2. Calculate dt and update quaternion
 * 3. Transform acceleration to user frame
 * 4. Feed data to air mouse and gesture recognition
 */
static bool process_imu_sample(int64_t *last_time_us, uint8_t *sample_count)
{
        lsm6ds3tr_c_reg_t reg;
        int16_t raw_accel[3], raw_gyro[3];
        float ax, ay, az, gx, gy, gz;
        float dt, acc_norm;
        int64_t now_us;

        /* Read status register */
        lsm6ds3tr_c_status_reg_get(&dev_ctx, &reg.status_reg);

        /* Check if both accel and gyro data ready */
        if (!reg.status_reg.xlda || !reg.status_reg.gda)
                return false;

        /* Calculate dt */
        now_us = esp_timer_get_time();
        if (*last_time_us == 0) {
                /* First frame: initialize timestamp and clear queue */
                *last_time_us = now_us;
                float dummy[3];
                while (xQueueReceive(mouse_queue_handle, dummy, 0) == pdTRUE)
                        ;
                dt = 0.01f; // 首帧给一个约 10ms 的默认值
        } else {
                dt = (now_us - *last_time_us) / 1000000.0f;
        }
        *last_time_us = now_us;

        /* Read and convert accelerometer data (raw -> mg -> g) */
        lsm6ds3tr_c_acceleration_raw_get(&dev_ctx, raw_accel);
        ax = lsm6ds3tr_c_from_fs4g_to_mg(raw_accel[0]) / 1000.0f;
        ay = lsm6ds3tr_c_from_fs4g_to_mg(raw_accel[1]) / 1000.0f;
        az = lsm6ds3tr_c_from_fs4g_to_mg(raw_accel[2]) / 1000.0f;

        /* Read and convert gyroscope data (raw -> mdps -> rad/s) */
        lsm6ds3tr_c_angular_rate_raw_get(&dev_ctx, raw_gyro);
        gx = lsm6ds3tr_c_from_fs2000dps_to_mdps(raw_gyro[0]) / 1000.0f *
             (M_PI / 180.0f);
        gy = lsm6ds3tr_c_from_fs2000dps_to_mdps(raw_gyro[1]) / 1000.0f *
             (M_PI / 180.0f);
        gz = lsm6ds3tr_c_from_fs2000dps_to_mdps(raw_gyro[2]) / 1000.0f *
             (M_PI / 180.0f);

#if !CONFIG_COLLECT_DATA_EN
        /* Feed gyro data to air mouse (overwrite to avoid blocking) */
        float airmouse_data[3] = {gx, gy, dt};
        xQueueOverwrite(mouse_queue_handle, airmouse_data);
#endif

        /* Update quaternion using Madgwick filter */
        madgwick_ahrs_update_imu(gx, gy, gz, ax, ay, az, dt);

        /* Transform acceleration to world frame */
        float ax_world, ay_world, az_world;
        quaternion_rotate_vector(q0, q1, q2, q3, ax, ay, az,
                                &ax_world, &ay_world, &az_world);

        /* Remove gravity (world Z-axis = +1g) to get linear acceleration */
        float lin_ax = ax_world;
        float lin_ay = ay_world;
        float lin_az = az_world - 1.0f;

        /* Gesture trigger: lock reference yaw when acceleration > 1.2g */
        acc_norm = sqrtf(ax * ax + ay * ay + az * az);
        if (!ref_yaw_locked && acc_norm > 1.2f) {
                ref_yaw = quaternion_get_yaw(q0, q1, q2, q3);
                ref_yaw_locked = true;
        }

        /* Transform to user frame (rotate horizontal plane by -ref_yaw) */
        float user_ax, user_ay, user_az;
        if (ref_yaw_locked) {
                float cos_yaw = cosf(-ref_yaw);
                float sin_yaw = sinf(-ref_yaw);
                user_ax = lin_ax * cos_yaw - lin_ay * sin_yaw;
                user_ay = lin_ax * sin_yaw + lin_ay * cos_yaw;
                user_az = lin_az;
        } else {
                user_ax = lin_ax;
                user_ay = lin_ay;
                user_az = lin_az;
        }

        /* Update gesture sample counter */
        (*sample_count)++;
        if (*sample_count >= CONFIG_COLLECT_DATA_NUM) {
                ref_yaw_locked = false;
                *sample_count = 0;
        }

#if CONFIG_IMU_QUATERNION_PRINT_EN && CONFIG_COLLECT_DATA_EN
        printf("%.3f,%.3f,%.3f\n", user_ax, user_ay, user_az);
        if (*sample_count == 0)
                printf("\n");
#endif

#if CONFIG_AI_MODEL_EN && !CONFIG_COLLECT_DATA_EN
        /* Feed data to gesture recognition buffer */
        // if (xSemaphoreTake(IMUbuffer.bufMutex, 0) == pdPASS) {
                IMUbuffer_add(&IMUbuffer, float2int(user_ax));
                IMUbuffer_add(&IMUbuffer, float2int(user_ay));
                IMUbuffer_add(&IMUbuffer, float2int(user_az));
                // xSemaphoreGive(IMUbuffer.bufMutex);
        // }
#endif

        return true;
}

void lsm6ds3tr_QuaternionTest_task(void *pvParameters)
{
        printf("Initializing LSM6DS3TR for Quaternion Test (Task Started)...\n");

        lsm6ds3tr_quaternion_init();

#if CONFIG_COLLECT_DATA_EN
        lsm6ds3tr_set_state(IMU_ACT);
#else
        lsm6ds3tr_set_state(IMU_SLEEP);
#endif

        while (1) {
                /* Wait for 100Hz timer wakeup */
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                static uint16_t sample_count = 0;
#if CONFIG_COLLECT_DATA_EN
                static uint8_t collect_flag = 0;

                // sample_count %= CONFIG_COLLECT_DATA_NUM;
                if (!sample_count) {
                        xQueueReceive(collect_data_queue, &collect_flag, portMAX_DELAY);
                        last_time_us = 0;
                }
#endif

                /* Process IMU sample */
                process_imu_sample(&last_time_us, &sample_count);
        }
}

void app_lsm6ds3tr_Quaternion(void)
{
        ESP_LOGI("LSM6DS3TR_QUATERNION_TEST",
                 "Starting LSM6DS3TR Quaternion Test Task...");
        xTaskCreate(lsm6ds3tr_QuaternionTest_task, "lsm6ds3tr_QuaternionTest_task",
                    4096, NULL, 5, NULL);
}

#endif
