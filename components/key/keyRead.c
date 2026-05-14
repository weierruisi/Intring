/*
 * Key Reading Module - Trackball and touch key input handling
 *
 * Supports:
 * - Trackball directional input (4-way)
 * - Touch keys (A, B) with long press and double click detection
 * - Ball button
 * - Multi-key combinations
 */

#include "keyRead.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static key_enum_t key_num = KEY_NONE;
static key_enum_t key_down = KEY_NONE;
static key_enum_t key_old = KEY_NONE;

#define LPRESS_TIME 100  /* Long press time: 100*10ms = 1s */
#define DCLICK_TIME 10   /* Double click window: 10*10ms = 100ms */

/*
 * ball_init - Initialize trackball GPIO pins
 *
 * Configures 4 GPIO pins for trackball directional sensing
 */
void ball_init(void)
{
        gpio_config_t io_conf = {};

        io_conf.pin_bit_mask = (1ULL << CONFIG_GPIO_FORWARD) | (1ULL << CONFIG_GPIO_BACK) |
                               (1ULL << CONFIG_GPIO_RIGHT) | (1ULL << CONFIG_GPIO_LEFT);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = 1;
        io_conf.pull_down_en = 0;
        gpio_config(&io_conf);
}

/*
 * ball_read - Read current trackball state
 *
 * Returns: Bitmask of active directions (BALL_FORWARD, BALL_BACK, etc.)
 */
uint8_t ball_read(void)
{
        uint8_t ball_val = BALL_NONE;

        if (gpio_get_level(CONFIG_GPIO_FORWARD) == 0)
                ball_val |= BALL_FORWARD;
        if (gpio_get_level(CONFIG_GPIO_BACK) == 0)
                ball_val |= BALL_BACK;
        if (gpio_get_level(CONFIG_GPIO_RIGHT) == 0)
                ball_val |= BALL_RIGHT;
        if (gpio_get_level(CONFIG_GPIO_LEFT) == 0)
                ball_val |= BALL_LEFT;

        return ball_val;
}

/*
 * ball_get_direct - Get trackball direction changes (edge detection)
 *
 * Returns: Bitmask of direction changes since last call
 */
uint8_t ball_get_direct(void)
{
        static uint8_t ball_old;
        uint8_t ball_val, ball_direct;

        ball_val = ball_read();
        ball_direct = ball_val ^ ball_old;
        ball_old = ball_val;

        return ball_direct;
}

/*
 * key_init - Initialize key GPIO pins
 *
 * Configures GPIO pins for ball button and touch keys A/B
 */
void key_init(void)
{
        gpio_config_t io_conf = {};

        io_conf.pin_bit_mask = (1ULL << CONFIG_GPIO_BALLKEY) |
                               (1ULL << CONFIG_GPIO_TOUCHKEY_A) |
                               (1ULL << CONFIG_GPIO_TOUCHKEY_B);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = 1;
        io_conf.pull_down_en = 0;
        gpio_config(&io_conf);
}

/*
 * key_read - Read current key state
 *
 * Returns: Current key state, prioritizing multi-key combinations
 */
key_enum_t key_read(void)
{
        key_enum_t key_val = KEY_NONE;
        uint8_t ball_pressed = 0;
        uint8_t touch_a_pressed = 0;
        uint8_t touch_b_pressed = 0;

        /* Check individual key states */
        if (gpio_get_level(CONFIG_GPIO_BALLKEY) == 0)
                ball_pressed = 1;
        if (gpio_get_level(CONFIG_GPIO_TOUCHKEY_A) == 0)
                touch_a_pressed = 1;
        if (gpio_get_level(CONFIG_GPIO_TOUCHKEY_B) == 0)
                touch_b_pressed = 1;

        /* Prioritize multi-key combinations */
        if (touch_a_pressed && touch_b_pressed && ball_pressed) {
                printf("三键同时按下\n");
                key_val = KEY_TOUCH_A_B_BALL;
        } else if (touch_a_pressed && touch_b_pressed) {
                key_val = KEY_TOUCH_A_B;
        } else if (ball_pressed) {
                key_val = KEY_BALL;
        } else if (touch_a_pressed) {
                key_val = KEY_TOUCH_A;
        } else if (touch_b_pressed) {
                key_val = KEY_TOUCH_B;
        }

        return key_val;
}

/*
 * key_get_state - Get key state with long press and double click detection
 *
 * Must be called every 10ms for proper timing
 *
 * Returns: Detected key event (single click, long press, double click, etc.)
 */
key_enum_t key_get_state(void)
{
        static key_enum_t lp_key_temp = KEY_NONE;
        static key_enum_t dc_key_temp = KEY_NONE;
        static uint16_t lpress_count;
        static uint16_t dclick_count;
        static uint8_t dclick_count_flag;
        key_enum_t key_down_out = KEY_NONE;

        key_num = key_read();
        key_down = key_num & (key_num ^ key_old);
        key_old = key_num;

        /* Handle simultaneous key presses immediately */
        if (key_num == KEY_TOUCH_A_B_BALL)// || key_num == KEY_TOUCH_A_B)
                return key_num;

        if (key_num == KEY_BALL)
                return key_num;

        /* Long press detection */
        if (key_num == lp_key_temp) {
                lpress_count++;
                if (lpress_count >= LPRESS_TIME) {
                        if (key_num == KEY_TOUCH_A)
                                key_down_out = KEY_TOUCH_A_LPRESS;
                        else if (key_num == KEY_TOUCH_B)
                                key_down_out = KEY_TOUCH_B_LPRESS;
                        else if (key_num == KEY_TOUCH_A_B)
                                key_down_out = KEY_TOUCH_A_B_LPRESS;
                        lpress_count = LPRESS_TIME;

                        /* Clear double click state to prevent single click
                         * after long press */
                        dclick_count_flag = 0;
                        dc_key_temp = KEY_NONE;
                }
        } else {
                lpress_count = 0;
        }

        if (key_down != KEY_NONE)
                lp_key_temp = key_down;

        /* Single click / double click detection */
        if (dc_key_temp != KEY_NONE && dclick_count_flag == 1) {
                if (key_num == KEY_NONE)
                        dclick_count++;

                if (dclick_count < DCLICK_TIME && key_down == dc_key_temp) {
                        /* Double click detected */
                        if (dc_key_temp == KEY_TOUCH_A)
                                key_down_out = KEY_TOUCH_A_DOUBLE_CLICK;
                        else if (dc_key_temp == KEY_TOUCH_B)
                                key_down_out = KEY_TOUCH_B_DOUBLE_CLICK;
                        dclick_count_flag = 0;
                        dclick_count = 0;
                } else if (dclick_count >= DCLICK_TIME) {
                        /* Single click confirmed */
                        key_down_out = dc_key_temp;
                        dclick_count = 0;
                        dclick_count_flag = 0;
                }
        }

        if (key_down != KEY_NONE && key_down_out == KEY_NONE) {
                dc_key_temp = key_down;
                dclick_count_flag = 1;
        }

        return key_down_out;
}

/* Key testing functions */

#if CONFIG_TOUCH_SWITCH_TEST_EN

#define KEY_TEST_MODE 0

/*
 * touch_key_test - Test touch key functionality
 *
 * Continuously reads and prints key states for debugging
 */
void touch_key_test(void)
{
        uint8_t led_state = 0;
        key_enum_t level;

        key_init();

        while (1) {
#if KEY_TEST_MODE == 0
                level = key_get_state();
                if (level != KEY_NONE)
                        printf("%d\r\n", level);
#else
                if (gpio_get_level(CONFIG_GPIO_BALLKEY) == 0)
                        level = KEY_BALL;
                else if (gpio_get_level(CONFIG_GPIO_TOUCHKEY_A) == 0)
                        level = KEY_TOUCH_A;
                else if (gpio_get_level(CONFIG_GPIO_TOUCHKEY_B) == 0)
                        level = KEY_TOUCH_B;
                else
                        level = KEY_NONE;

                printf("%d\r\n", level);
#endif
                if (level == 2) {
                        led_state++;
                        led_state %= 2;
                        gpio_set_level(GPIO_LED_PWN_NUM, led_state);
                }
                vTaskDelay(10 / portTICK_PERIOD_MS);
        }
}
#endif

#if CONFIG_TRACK_BALL_TEST_EN
/*
 * track_ball_test - Test trackball functionality
 *
 * Continuously reads and prints trackball direction changes
 */
void track_ball_test(void)
{
        uint8_t value;

        ball_init();

        while (1) {
                value = ball_get_direct();
                if (value != 0)
                        printf("%d %d %d %d\r\n",
                               value & BALL_FORWARD, value & BALL_BACK,
                               value & BALL_RIGHT, value & BALL_LEFT);
                vTaskDelay(10 / portTICK_PERIOD_MS);
        }
}
#endif
