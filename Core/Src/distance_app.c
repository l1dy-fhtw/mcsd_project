/**
 * @file    distance_app.c
 * @brief   VL53L1X attach / live ranging / lab-key fallback
 */
#include "distance_app.h"
#include "vl53l1x_driver.h"

extern I2C_HandleTypeDef hi2c1;

static int distance_cm = CM_START;
static uint16_t distance_mm = 0;
static uint8_t tof_online = 0;
static uint8_t ranging = 0;

static void tof_read_once(void)
{
  uint16_t mm = 0;
  int cm;

  if (VL53L1X_IsDataReady(&hi2c1) == 0U)
  {
    return;
  }
  if (VL53L1X_GetDistance(&hi2c1, &mm) == VL53L1X_OK)
  {
    distance_mm = mm;
    cm = (int)(mm / 10U);
    if (cm < CM_MIN)
    {
      cm = CM_MIN;
    }
    if (cm > CM_MAX)
    {
      cm = CM_MAX;
    }
    distance_cm = cm;
  }
  (void)VL53L1X_ClearInterrupt(&hi2c1);
}

uint8_t tof_try_attach(void)
{
  if (HAL_I2C_IsDeviceReady(&hi2c1, VL53L1X_I2C_ADDR, 1, 10) != HAL_OK)
  {
    return 0;
  }
  if (VL53L1X_Init(&hi2c1) != VL53L1X_OK)
  {
    return 0;
  }
  if (VL53L1X_StartMeasurement(&hi2c1) != VL53L1X_OK)
  {
    return 0;
  }
  tof_online = 1;
  ranging = 1;
  return 1;
}

void tof_service_live(app_state_t state, PotMode_t mode)
{
  uint8_t need;

  if (tof_online == 0U)
  {
    return;
  }

  need = (state == ST_LIVE) ? 1U : 0U;
  if ((mode == MODE_UART_TEST)
      && (state != ST_SLEEP) && (state != ST_WAIT_INPUT) && (state != ST_HOLD))
  {
    need = 1U;
  }
  if (need == 0U)
  {
    return;
  }

  if (ranging == 0U)
  {
    (void)VL53L1X_StartMeasurement(&hi2c1);
    ranging = 1;
  }
  tof_read_once();
}

void tof_stop(void)
{
  if (tof_online != 0U)
  {
    (void)VL53L1X_StopMeasurement(&hi2c1);
  }
  ranging = 0;
}

uint8_t tof_is_online(void)
{
  return tof_online;
}

int distance_cm_get(void)
{
  return distance_cm;
}

uint16_t distance_mm_get(void)
{
  return distance_mm;
}

void distance_key(uint8_t key)
{
  if (key == '1')
  {
    if (distance_cm > CM_MIN)
    {
      distance_cm -= CM_STEP;
    }
  }
  else if (key == '2')
  {
    if (distance_cm < CM_MAX)
    {
      distance_cm += CM_STEP;
    }
  }
}
