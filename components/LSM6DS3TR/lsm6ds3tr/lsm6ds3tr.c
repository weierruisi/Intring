#include "lsm6ds3tr.h"
#include "project_config.h"

/* Private variables ---------------------------------------------------------*/
static int16_t data_raw_acceleration[3];
static int16_t data_raw_angular_rate[3];
// static int16_t data_raw_temperature;
// static float acceleration_mg[3];
// static float angular_rate_mdps[3];
// static float temperature_degC;
static uint8_t whoamI, rst;
// static uint8_t tx_buffer[TX_BUF_DIM];
static stmdev_ctx_t dev_ctx;

void lsm6ds3tr_init(void) {
  /* Initialize mems driver interface */
  dev_ctx.write_reg = platform_write;
  dev_ctx.read_reg = platform_read;
  dev_ctx.mdelay = platform_delay;
  // dev_ctx.handle = &SENSOR_BUS;
  /* Init test platform */
  platform_init();
  /* Wait sensor boot time */
  platform_delay(BOOT_TIME);
  /* Check device ID */
  whoamI = 0;
  lsm6ds3tr_c_device_id_get(&dev_ctx, &whoamI);

  if ((whoamI != LSM6DS3TR_C_ID1) && (whoamI != LSM6DS3TR_C_ID2)) {
    ESP_LOGE("lsm6ds3tr", "Erro whoamI: %X", whoamI);
    while (1) /*manage here device not found */
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
  ESP_LOGI("lsm6ds3tr", "Right ID whoamI: %X", whoamI);

  /* Restore default configuration */
  lsm6ds3tr_c_reset_set(&dev_ctx, PROPERTY_ENABLE);

  do {
    lsm6ds3tr_c_reset_get(&dev_ctx, &rst);
  } while (rst);

  /* Enable Block Data Update */
  lsm6ds3tr_c_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
  /* Set Output Data Rate */
  lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_XL_ODR_6k66Hz);
  lsm6ds3tr_c_gy_data_rate_set(&dev_ctx, LSM6DS3TR_C_GY_ODR_6k66Hz);
  /* Set full scale */
  lsm6ds3tr_c_xl_full_scale_set(&dev_ctx, LSM6DS3TR_C_2g);
  lsm6ds3tr_c_gy_full_scale_set(&dev_ctx, LSM6DS3TR_C_2000dps);
  /* Configure filtering chain(No aux interface) */
  /* Accelerometer - analog filter */
  lsm6ds3tr_c_xl_filter_analog_set(&dev_ctx, LSM6DS3TR_C_XL_ANA_BW_400Hz);
  /* Accelerometer - LPF1 path ( LPF2 not used )*/
  // lsm6ds3tr_c_xl_lp1_bandwidth_set(&dev_ctx, LSM6DS3TR_C_XL_LP1_ODR_DIV_4);
  /* Accelerometer - LPF1 + LPF2 path */
  lsm6ds3tr_c_xl_lp2_bandwidth_set(&dev_ctx,
                                   LSM6DS3TR_C_XL_LOW_NOISE_LP_ODR_DIV_100);
  /* Accelerometer - High Pass / Slope path */
  // lsm6ds3tr_c_xl_reference_mode_set(&dev_ctx, PROPERTY_DISABLE);
  // lsm6ds3tr_c_xl_hp_bandwidth_set(&dev_ctx, LSM6DS3TR_C_XL_HP_ODR_DIV_100);
  /* Gyroscope - filtering chain */
  lsm6ds3tr_c_gy_band_pass_set(&dev_ctx, LSM6DS3TR_C_HP_DISABLE_LP1_NORMAL);
}

void lsm6ds3tr_GetAcc(float *buf) {
  lsm6ds3tr_c_reg_t reg;
  lsm6ds3tr_c_status_reg_get(&dev_ctx, &reg.status_reg);

  if (reg.status_reg.xlda) {
    /* Read magnetic field data */
    memset(data_raw_acceleration, 0x00, 3 * sizeof(int16_t));
    lsm6ds3tr_c_acceleration_raw_get(&dev_ctx, data_raw_acceleration);
    buf[0] = lsm6ds3tr_c_from_fs2g_to_mg(data_raw_acceleration[0]);
    buf[1] = lsm6ds3tr_c_from_fs2g_to_mg(data_raw_acceleration[1]);
    buf[2] = lsm6ds3tr_c_from_fs2g_to_mg(data_raw_acceleration[2]);
  }
}

void lsm6ds3tr_GetAgl(float *buf) {
  lsm6ds3tr_c_reg_t reg;
  lsm6ds3tr_c_status_reg_get(&dev_ctx, &reg.status_reg);

  if (reg.status_reg.gda) {
    /* Read magnetic field data */
    memset(data_raw_angular_rate, 0x00, 3 * sizeof(int16_t));
    lsm6ds3tr_c_angular_rate_raw_get(&dev_ctx, data_raw_angular_rate);
    buf[0] = lsm6ds3tr_c_from_fs2000dps_to_mdps(data_raw_angular_rate[0]);
    buf[1] = lsm6ds3tr_c_from_fs2000dps_to_mdps(data_raw_angular_rate[1]);
    buf[2] = lsm6ds3tr_c_from_fs2000dps_to_mdps(data_raw_angular_rate[2]);
  }
}

#if LSM6DS3TR_COLLECT_DATA_EN
void lsm6ds3tr_CollectData(void) {
  float LSM_DataBuf[3];
  uint16_t count = 0;

  gpio_set_direction(GPIO_NUM_9, GPIO_MODE_INPUT);
  lsm6ds3tr_init();
  while (1) {
    uint8_t level = gpio_get_level(GPIO_NUM_9);
    while (gpio_get_level(GPIO_NUM_9) == 0)
      ;
    while (level == 0 && count < 200) {
      count++;

      lsm6ds3tr_GetAcc(LSM_DataBuf);
      if (count == 1)
        printf("x,y,z\n");
      printf("%f,%f,%f\n", LSM_DataBuf[0], LSM_DataBuf[1], LSM_DataBuf[2]);
      // lsm6ds3tr_GetAgl(LSM_DataBuf);
      // printf("%f, %f, %f\r\n", LSM_DataBuf[0], LSM_DataBuf[1],
      // LSM_DataBuf[2]); vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    count = 0;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
#endif
