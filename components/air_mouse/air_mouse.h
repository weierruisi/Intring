/*
 * Air Mouse Interface
 *
 * Provides gyroscope-based cursor control functionality
 */

#ifndef AIR_MOUSE_H
#define AIR_MOUSE_H

#include <stdint.h>

/*
 * trig_mouse_reset - Reset air mouse filter state
 *
 * Call this when enabling air mouse mode to clear residual values
 */
void trig_mouse_reset(void);

/*
 * trig_mouse_compute - Compute air mouse displacement
 * @gyro_y: Y-axis angular velocity (rad/s)
 * @gyro_x: X-axis angular velocity (rad/s)
 * @dt: Time interval (s)
 * @offset_y: Y-axis zero offset
 * @offset_x: X-axis zero offset
 * @out_x: Output X-axis displacement
 * @out_y: Output Y-axis displacement
 */
void trig_mouse_compute(float gyro_y, float gyro_x, float dt,
                        float offset_y, float offset_x,
                        int8_t *out_x, int8_t *out_y);

#endif /* AIR_MOUSE_H */
