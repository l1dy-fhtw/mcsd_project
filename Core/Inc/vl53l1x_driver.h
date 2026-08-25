#ifndef VL53L1X_DRIVER_H
#define VL53L1X_DRIVER_H

#include "main.h"

/* Default 8-bit I2C Address (0x29 shifted left by 1) */
#define VL53L1X_I2C_ADDR             0x52

/* Core Registers */
#define VL53L1X_REG_SOFT_RESET       0x0000
#define VL53L1X_REG_SYS_STATUS       0x0022
#define VL53L1X_REG_INT_STATUS       0x0031
#define VL53L1X_REG_INT_CLEAR        0x0086
#define VL53L1X_REG_SYS_START        0x0087
#define VL53L1X_REG_RANGE_STATUS     0x0089
#define VL53L1X_REG_RANGE_MM         0x0096

/* Verification Registers */
#define VL53L1X_REG_MODEL_ID         0x010F
#define VL53L1X_REG_MODULE_TYPE      0x0110
#define VL53L1X_REG_MASK_REVISION    0x0111

#define VL53L1X_VAL_MODEL_ID         0xEA
#define VL53L1X_VAL_MODULE_TYPE      0xCC
#define VL53L1X_VAL_MASK_REVISION    0x10

/* Driver Status Codes */
typedef enum {
    VL53L1X_OK      = 0,
    VL53L1X_ERROR   = -1,
    VL53L1X_TIMEOUT = -2,
    VL53L1X_INVALID = -3
} VL53L1X_Status_t;

/* Public API Functions */
VL53L1X_Status_t VL53L1X_Init(I2C_HandleTypeDef *hi2c);
VL53L1X_Status_t VL53L1X_StartMeasurement(I2C_HandleTypeDef *hi2c);
VL53L1X_Status_t VL53L1X_StopMeasurement(I2C_HandleTypeDef *hi2c);
uint8_t          VL53L1X_IsDataReady(I2C_HandleTypeDef *hi2c);
VL53L1X_Status_t VL53L1X_GetDistance(I2C_HandleTypeDef *hi2c, uint16_t *pDistanceMm);
VL53L1X_Status_t VL53L1X_ClearInterrupt(I2C_HandleTypeDef *hi2c);

#endif /* VL53L1X_DRIVER_H */
