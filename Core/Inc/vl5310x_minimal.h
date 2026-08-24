#ifndef VL53L0X_MINIMAL_H
#define VL53L0X_MINIMAL_H

#include "main.h"

/* 8-bit I2C Address (0x29 << 1) */
#define VL53L0X_I2C_ADDR                     0x52

/* Register Addresses */
#define VL53L0X_REG_IDENTIFICATION_MODEL_ID  0xC0
#define VL53L0X_REG_VHV_CONFIG_PAD_SCL_SDA   0x89
#define VL53L0X_REG_SYSRANGE_START           0x00
#define VL53L0X_REG_RESULT_INTERRUPT_STATUS  0x13
#define VL53L0X_REG_RESULT_RANGE_STATUS      0x14

/* Status & Control Defines */
#define VL53L0X_START_SINGLE_SHOT            0x01
#define VL53L0X_CLEAR_INTERRUPT              0x01

/* Function Prototypes */
HAL_StatusTypeDef VL53L0X_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef VL53L0X_ReadDistance(I2C_HandleTypeDef *hi2c, uint16_t *distance_mm);

#endif /* VL53L0X_MINIMAL_H */
