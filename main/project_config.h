#define LSM6DS3TR_C_READ_DATA_POLLING_EN 0
// #define LSM6DS3TR_AIRMOUSE_EN 1 // 使能空鼠
#define LSM6DS3TR_COLLECT_DATA_EN 0

// // 四元数
// // 仅使能 LSM6DS3TR_QUATERNION_TEST_EN 程序只编译四元数代码，串口持续输出世界坐标系结果
// // 同时使能 COLLECT_DATA_EN 保留戒指所有功能，通过触摸按键，控制串口输出世界坐标系下的加速度
// #define LSM6DS3TR_QUATERNION_TEST_EN 1
// #if LSM6DS3TR_QUATERNION_TEST_EN
// #define LSM6DS3TR_QUATERNION_EN_PRINT 1
// #endif

// #define DFS_EN 1

// // key
// #define GPIO_BALLKEY GPIO_NUM_8
// #define GPIO_TOUCHKEY_A GPIO_NUM_4
// #define GPIO_TOUCHKEY_B GPIO_NUM_2
// // key测试
// #define TOUCH_SWICH_TEST_EN 0
                
// // 轨迹球
// #define GPIO_FORWARD GPIO_NUM_5
// #define GPIO_BACK GPIO_NUM_6
// #define GPIO_RIGHT GPIO_NUM_3
// #define GPIO_LEFT GPIO_NUM_7
// // 轨迹球测试
// #define TRACK_BALL_TEST_EN 0

// #define MOUSE_STEP 3
// #define OP_COUNT_MAX 6
// #define OP_DELAY_MAX 30
// #define DELAY_COUNTMAX 20

// // 手势识别
// #define COLLECT_DATA_NUM    128
// #define COLLECT_DATA_EN     0       // 收集数据训练模型
// #define TEST_AI_MODEL_EN    0
// #define AI_MODEL_EN         1
// /*
//  *飞线版测试板:  GPIO_PWN_EN：       GPIO_NUM_1
//  *              GPIO_LED_NUM：      GPIO_NUM_0
//  */

// // #define GPIO_LED_NUM GPIO_NUM_10 // GPIO_NUM_1 
// // #define GPIO_PWN_EN GPIO_NUM_10
// #define GPIO_LED_PWN_NUM GPIO_NUM_10

// #define GPIO_I2C_SDA GPIO_NUM_18 // GPIO_NUM_18
// #define GPIO_I2C_SCK GPIO_NUM_19
