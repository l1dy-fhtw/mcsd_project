#ifndef VL53L1X_DRIVER_H
#define VL53L1X_DRIVER_H

#include "main.h"

/**
 * @file vl53l1x_driver.h
 * @brief Minimal VL53L1X driver (I2C register access, no ST ULD library).
 *
 * Wiring: SDA = PB7, SCL = PB6, I2C1 shared with the LCD backpack.
 * 7-bit address 0x29; HAL uses the 8-bit write address 0x52.
 * CubeMX: I2C1 Fast Mode (~400 kHz), PB6/PB7 open-drain GPIO_PULLUP.
 */

#define VL53L1X_I2C_ADDR             0x52

#define VL53L1X_REG_SOFT_RESET       0x0000
#define VL53L1X_REG_SYS_STATUS       0x0022
#define VL53L1X_REG_INT_STATUS       0x0031
#define VL53L1X_REG_INT_CLEAR        0x0086
#define VL53L1X_REG_SYS_START        0x0087
#define VL53L1X_REG_RANGE_STATUS     0x0089
#define VL53L1X_REG_RANGE_MM         0x0096

#define VL53L1X_REG_MODEL_ID         0x010F
#define VL53L1X_REG_MODULE_TYPE      0x0110
#define VL53L1X_REG_MASK_REVISION    0x0111

#define VL53L1X_VAL_MODEL_ID         0xEA
#define VL53L1X_VAL_MODULE_TYPE      0xCC
#define VL53L1X_VAL_MASK_REVISION    0x10

typedef enum {
    VL53L1X_OK      = 0,   /* valid (or warning) range */
    VL53L1X_ERROR   = -1,  /* I2C NACK / bus error */
    VL53L1X_TIMEOUT = -2,  /* boot did not complete in 100 ms */
    VL53L1X_INVALID = -3   /* wrong ID, or range status not usable */
} VL53L1X_Status_t;

/**
 * @brief Identify the sensor, wait for boot, load the 91-byte default config.
 * @param hi2c  I2C1 handle from MX_I2C1_Init().
 * @retval VL53L1X_OK, ERROR, TIMEOUT, or INVALID (ID mismatch).
 */
VL53L1X_Status_t VL53L1X_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief Clear the data-ready flag and start continuous ranging (0x40).
 * @param hi2c  I2C1 handle.
 * @retval VL53L1X_OK or ERROR.
 */
VL53L1X_Status_t VL53L1X_StartMeasurement(I2C_HandleTypeDef *hi2c);

/**
 * @brief Stop ranging (write 0x00 to SYS_START). Used before app sleep.
 * @param hi2c  I2C1 handle.
 * @retval VL53L1X_OK or ERROR.
 */
VL53L1X_Status_t VL53L1X_StopMeasurement(I2C_HandleTypeDef *hi2c);

/**
 * @brief Poll INT_STATUS bit 0 (data ready). Does not use the GPIO1 pin.
 * @param hi2c  I2C1 handle.
 * @retval 1 if a new sample is ready, 0 otherwise (or on I2C error).
 */
uint8_t          VL53L1X_IsDataReady(I2C_HandleTypeDef *hi2c);

/**
 * @brief Read millimetres and the ST range-status nibble.
 * @param hi2c         I2C1 handle.
 * @param pDistanceMm  Out: 16-bit distance, always written on a successful I2C read.
 * @retval OK if decoded status is 0, 1, 2 or 7 (valid / sigma / signal / wrap).
 *         INVALID for other statuses; ERROR on I2C failure.
 */
VL53L1X_Status_t VL53L1X_GetDistance(I2C_HandleTypeDef *hi2c, uint16_t *pDistanceMm);

/**
 * @brief Write 0x01 to INT_CLEAR so the next sample can raise data-ready.
 * @param hi2c  I2C1 handle.
 * @retval VL53L1X_OK or ERROR.
 */
VL53L1X_Status_t VL53L1X_ClearInterrupt(I2C_HandleTypeDef *hi2c);

#endif /* VL53L1X_DRIVER_H */
