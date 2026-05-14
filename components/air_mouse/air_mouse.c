#include <math.h>
#include <stdint.h>
#include <stdio.h>

// ======================= 用户可调参数 =======================

// 空鼠基础灵敏度：值越大，同样角速度对应的光标位移越大。
#define VIRTUAL_DISTANCE_PX 2000.0f

// 基础死区（运动时）：越小越灵敏/丝滑，但更容易出现微漂。
#define GYRO_DEADBAND 0.22f
// 增强死区（长时间静止后）：越大越抗抖，但微小动作更难触发。
#define GYRO_DEADBAND_BOOSTED 20.34f

// 动态低通滤波参数
// 慢速最小 alpha：越小越稳，但会更“黏手”。
#define FILTER_ALPHA_MIN 0.05f
// 快速最大 alpha：越大越跟手，但平滑度会下降。
#define FILTER_ALPHA_MAX 0.60f
// alpha 插值的速度尺度（像素/帧估计）。
#define DYNAMIC_VEL_THRES 10.0f

// 自适应死区判定参数（目标：运动灵敏，停住稳）
// 两轴 raw 都小于该值，且光标估计速度较低时，认为进入“静止积累”。
#define STILL_GYRO_RAW_THRES 0.035f
#define STILL_CURSOR_SPEED_THRES 0.55f
// 超过该阈值则认为是“主动移动”，快速退出静止状态。
#define ACTIVE_GYRO_RAW_THRES 0.085f
#define ACTIVE_CURSOR_SPEED_THRES 1.80f

// still_score（0~1）变化速率
// 静止时每帧增加：越大表示更快进入强抑抖状态。
#define STILL_SCORE_INC 0.048f
// 主动移动时每帧快速减少：越大表示更快恢复灵敏。
#define STILL_SCORE_DEC_FAST 0.140f
// 过渡状态每帧慢速减少：控制迟滞保持时长。
#define STILL_SCORE_DEC_SLOW 0.030f

// 死区目标映射参数
// still_score 低于该拐点时，优先保持基础死区，保证运动丝滑。
#define STILL_SCORE_KNEE 0.60f

// 每帧死区爬升/回落速度（平滑体感）
#define DEADBAND_RAMP_UP_STEP 0.0048f
#define DEADBAND_RAMP_DOWN_STEP 0.004f

// ======================= 状态变量 =======================

static float residue_x;
static float residue_y;
static float filt_dx;
static float filt_dy;
static float still_score;
static float adaptive_deadband = GYRO_DEADBAND;

/*
 * 动态低通：速度越高，alpha 越大，保证快速动作时的响应。
 */
static float apply_dynamic_filter(float target, float *current)
{
        float delta = fabsf(target - *current);
        float alpha = FILTER_ALPHA_MIN +
                      ((FILTER_ALPHA_MAX - FILTER_ALPHA_MIN) *
                       (delta / DYNAMIC_VEL_THRES));

        if (alpha > FILTER_ALPHA_MAX)
                alpha = FILTER_ALPHA_MAX;

        *current = alpha * target + (1.0f - alpha) * (*current);
        return *current;
}

/*
 * 根据角速度与死区计算位移。
 */
static float calculate_trig_displacement(float angular_velocity, float dt,
                                         float deadband)
{
        float d_theta;

        if (fabsf(angular_velocity) < deadband)
                return 0.0f;

        d_theta = angular_velocity * dt;

        // 防止 tan() 在大角度下数值爆炸
        if (d_theta > 0.7f)
                d_theta = 0.7f;
        if (d_theta < -0.7f)
                d_theta = -0.7f;

        return VIRTUAL_DISTANCE_PX * tanf(d_theta);
}

/*
 * 将 still_score 映射为目标死区。
 * 1. 低分段保持基础死区，保证运动丝滑。
 * 2. 进入静止段后非线性快速抬升死区，抑制按压抖动。
 */
static float compute_target_deadband(float score)
{
        float span = GYRO_DEADBAND_BOOSTED - GYRO_DEADBAND;
        float t;

        if (score <= STILL_SCORE_KNEE)
                return GYRO_DEADBAND;

        t = (score - STILL_SCORE_KNEE) / (1.0f - STILL_SCORE_KNEE);
        if (t < 0.0f)
                t = 0.0f;
        else if (t > 1.0f)
                t = 1.0f;

        return GYRO_DEADBAND + span * sqrtf(t);
}

void trig_mouse_reset(void)
{
        residue_x = 0;
        residue_y = 0;
        filt_dx = 0;
        filt_dy = 0;
        still_score = 0;
        adaptive_deadband = GYRO_DEADBAND;
}

void trig_mouse_compute(float gyro_y, float gyro_x, float dt,
                        float offset_y, float offset_x,
                        int8_t *out_x, int8_t *out_y)
{
        float raw_dx, raw_dy;
        float filtered_dx, filtered_dy;
        float abs_gyro_y, abs_gyro_x, gyro_mag;
        float cursor_speed_est;
        float target_deadband;
        int32_t final_x, final_y;

        if (dt < 0.001f) {
                *out_x = 0;
                *out_y = 0;
                return;
        }

        gyro_y -= offset_y;
        gyro_x -= offset_x;

        abs_gyro_y = fabsf(gyro_y);
        abs_gyro_x = fabsf(gyro_x);
        gyro_mag = hypotf(gyro_y, gyro_x);
        cursor_speed_est = hypotf(filt_dx, filt_dy);

        // 带迟滞地构建 still_score：静止慢慢加，移动快速减。
        if (abs_gyro_y < STILL_GYRO_RAW_THRES &&
            abs_gyro_x < STILL_GYRO_RAW_THRES &&
            cursor_speed_est < STILL_CURSOR_SPEED_THRES) {
                still_score += STILL_SCORE_INC;
        } else if (gyro_mag > ACTIVE_GYRO_RAW_THRES ||
                   cursor_speed_est > ACTIVE_CURSOR_SPEED_THRES) {
                still_score -= STILL_SCORE_DEC_FAST;
        } else {
                still_score -= STILL_SCORE_DEC_SLOW;
        }

        if (still_score < 0.0f)
                still_score = 0.0f;
        else if (still_score > 1.0f)
                still_score = 1.0f;

        target_deadband = compute_target_deadband(still_score);

        if (adaptive_deadband < target_deadband) {
                adaptive_deadband += DEADBAND_RAMP_UP_STEP;
                if (adaptive_deadband > target_deadband)
                        adaptive_deadband = target_deadband;
        } else if (adaptive_deadband > target_deadband) {
                adaptive_deadband -= DEADBAND_RAMP_DOWN_STEP;
                if (adaptive_deadband < target_deadband)
                        adaptive_deadband = target_deadband;
        }

        raw_dx = calculate_trig_displacement(gyro_y, dt, adaptive_deadband);
        raw_dy = calculate_trig_displacement(gyro_x, dt, adaptive_deadband);

        filtered_dx = apply_dynamic_filter(raw_dx, &filt_dx);
        filtered_dy = apply_dynamic_filter(raw_dy, &filt_dy);

        residue_x += filtered_dx;
        residue_y += filtered_dy;

        final_x = (int32_t)residue_x;
        final_y = (int32_t)residue_y;

        residue_x -= final_x;
        residue_y -= final_y;

        if (final_x > 127)
                final_x = 127;
        else if (final_x < -127)
                final_x = -127;

        if (final_y > 127)
                final_y = 127;
        else if (final_y < -127)
                final_y = -127;

        *out_x = (int8_t)final_y;
        *out_y = (int8_t)final_x;
}
