#include "vl5310x_minimal.h"

/**
 * @brief  Write single 8-bit register
 */
static HAL_StatusTypeDef VL53L0X_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value) {
    return HAL_I2C_Mem_Write(hi2c, VL53L0X_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
}

/**
 * @brief  Read single 8-bit register
 */
static HAL_StatusTypeDef VL53L0X_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *value) {
    return HAL_I2C_Mem_Read(hi2c, VL53L0X_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, value, 1, 100);
}

/**
 * @brief  Initializes the VL53L0X sensor for 2.8V I2C pads and verifies Device ID
 */
HAL_StatusTypeDef VL53L0X_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t model_id = 0;

    /* 1. Verify Model ID (Default for VL53L0X is 0xEE) */
    if (VL53L0X_ReadReg(hi2c, VL53L0X_REG_IDENTIFICATION_MODEL_ID, &model_id) != HAL_OK) {
        return HAL_ERROR;
    }
    if (model_id != 0xEE) {
        return HAL_ERROR; /* Device not recognized */
    }

    /* 2. Configure I/O Voltage (Set 2V8 mode on SCL/SDA pads) */
    uint8_t vhv_config = 0;
    VL53L0X_ReadReg(hi2c, VL53L0X_REG_VHV_CONFIG_PAD_SCL_SDA, &vhv_config);
    vhv_config |= 0x01;
    VL53L0X_WriteReg(hi2c, VL53L0X_REG_VHV_CONFIG_PAD_SCL_SDA, vhv_config);

    /* 3. Set standard I2C Mode */
    VL53L0X_WriteReg(hi2c, 0x88, 0x00);
    VL53L0X_WriteReg(hi2c, 0x80, 0x01);
    VL53L0X_WriteReg(hi2c, 0xFF, 0x01);
    VL53L0X_WriteReg(hi2c, 0x00, 0x00);
    VL53L0X_WriteReg(hi2c, 0xFF, 0x00);
    VL53L0X_WriteReg(hi2c, 0x80, 0x00);

    return HAL_OK;
}

/**
 * @brief  Triggers a single-shot range measurement and returns distance in mm
 */
HAL_StatusTypeDef VL53L0X_ReadDistance(I2C_HandleTypeDef *hi2c, uint16_t *distance_mm) {
    uint8_t status = 0;
    uint8_t data[2] = {0};
    uint32_t timeout_tick = HAL_GetTick();

    /* 1. Trigger single-shot ranging */
    if (VL53L0X_WriteReg(hi2c, VL53L0X_REG_SYSRANGE_START, VL53L0X_START_SINGLE_SHOT) != HAL_OK) {
        return HAL_ERROR;
    }

    /* 2. Poll interrupt status register until data is ready (bit 0 or 2 set) */
    do {
        if (VL53L0X_ReadReg(hi2c, VL53L0X_REG_RESULT_INTERRUPT_STATUS, &status) != HAL_OK) {
            return HAL_ERROR;
        }
        if ((HAL_GetTick() - timeout_tick) > 200) {
            return HAL_TIMEOUT; /* Measurement timed out */
        }
    } while ((status & 0x07) == 0);

    /* 3. Read 2-byte distance measurement result (Register 0x14 + 10 = 0x1E) */
    if (HAL_I2C_Mem_Read(hi2c, VL53L0X_I2C_ADDR, VL53L0X_REG_RESULT_RANGE_STATUS + 10,
                         I2C_MEMADD_SIZE_8BIT, data, 2, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    /* 4. Combine high and low byte */
    *distance_mm = ((uint16_t)data[0] << 8) | (uint16_t)data[1];

    /* 5. Clear interrupt to prepare for next range cycle */
    VL53L0X_WriteReg(hi2c, 0x0B, VL53L0X_CLEAR_INTERRUPT);

    return HAL_OK;
}
