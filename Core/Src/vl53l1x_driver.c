/**
 * @file    vl53l1x_driver.c
 * @brief   VL53L1X I2C register driver (identify, config, range)
 *
 * All public calls take the same I2C1 handle. 16-bit register addresses use
 * HAL_I2C_Mem_Read/Write with I2C_MEMADD_SIZE_16BIT and a 100 ms timeout.
 */
#include "vl53l1x_driver.h"
#include <stdio.h>

/* Official ST default configuration (91 bytes starting at 0x002D). */
static const uint8_t VL51L1X_DEFAULT_CONFIGURATION[91] = {
    0x00, /* 0x2d : I2C mode */
    0x00, /* 0x2e : I2C 1.8V / 2.8V pad */
    0x00, /* 0x2f : GPIO 1.8V / 2.8V pad */
    0x01, /* 0x30 : Interrupt Polarity */
    0x02, /* 0x31 : Interrupt Status */
    0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b,
    0x00, 0x00, 0x02, 0x0a, 0x21, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0xc8, 0x00, 0x00, 0x38, 0xff, 0x01, 0x00, 0x08, 0x00, 0x00,
    0x01, 0xdb, 0x0f, 0x01, 0xf1, 0x0d, 0x01, 0x68, 0x00, 0x80, 0x08,
    0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x89, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x0f, 0x0d, 0x0e, 0x0e, 0x00, 0x00, 0x02,
    0xc7, 0xff, 0x9B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};

static HAL_StatusTypeDef I2C_WriteReg8(I2C_HandleTypeDef *hi2c, uint16_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(hi2c, VL53L1X_I2C_ADDR, reg, I2C_MEMADD_SIZE_16BIT, &val, 1, 100);
}

static HAL_StatusTypeDef I2C_ReadReg8(I2C_HandleTypeDef *hi2c, uint16_t reg, uint8_t *pVal)
{
    return HAL_I2C_Mem_Read(hi2c, VL53L1X_I2C_ADDR, reg, I2C_MEMADD_SIZE_16BIT, pVal, 1, 100);
}

static HAL_StatusTypeDef I2C_ReadReg16(I2C_HandleTypeDef *hi2c, uint16_t reg, uint16_t *pVal)
{
    uint8_t buf[2] = {0};
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(hi2c, VL53L1X_I2C_ADDR, reg, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);
    if (status == HAL_OK)
    {
        *pVal = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return status;
}

VL53L1X_Status_t VL53L1X_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t modelId = 0, moduleType = 0, maskRev = 0;
    uint8_t bootState = 0;
    uint32_t startTick = HAL_GetTick();

    if (I2C_ReadReg8(hi2c, VL53L1X_REG_MODEL_ID, &modelId) != HAL_OK ||
        I2C_ReadReg8(hi2c, VL53L1X_REG_MODULE_TYPE, &moduleType) != HAL_OK ||
        I2C_ReadReg8(hi2c, VL53L1X_REG_MASK_REVISION, &maskRev) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }

    if (modelId != VL53L1X_VAL_MODEL_ID ||
        moduleType != VL53L1X_VAL_MODULE_TYPE ||
        maskRev != VL53L1X_VAL_MASK_REVISION)
    {
        return VL53L1X_INVALID;
    }

    while (bootState == 0)
    {
        if (I2C_ReadReg8(hi2c, VL53L1X_REG_SYS_STATUS, &bootState) != HAL_OK)
        {
            return VL53L1X_ERROR;
        }
        if ((HAL_GetTick() - startTick) > 100)
        {
            return VL53L1X_TIMEOUT;
        }
        HAL_Delay(1);
    }

    if (HAL_I2C_Mem_Write(hi2c, VL53L1X_I2C_ADDR, 0x002D, I2C_MEMADD_SIZE_16BIT,
                          (uint8_t *)VL51L1X_DEFAULT_CONFIGURATION, 91, 100) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }

    HAL_Delay(5);
    return VL53L1X_OK;
}

VL53L1X_Status_t VL53L1X_StartMeasurement(I2C_HandleTypeDef *hi2c)
{
    I2C_WriteReg8(hi2c, VL53L1X_REG_INT_CLEAR, 0x01);
    if (I2C_WriteReg8(hi2c, VL53L1X_REG_SYS_START, 0x40) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }
    return VL53L1X_OK;
}

VL53L1X_Status_t VL53L1X_StopMeasurement(I2C_HandleTypeDef *hi2c)
{
    if (I2C_WriteReg8(hi2c, VL53L1X_REG_SYS_START, 0x00) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }
    return VL53L1X_OK;
}

uint8_t VL53L1X_IsDataReady(I2C_HandleTypeDef *hi2c)
{
    uint8_t status = 0;
    if (I2C_ReadReg8(hi2c, VL53L1X_REG_INT_STATUS, &status) == HAL_OK)
    {
        if ((status & 0x01) != 0)
        {
            return 1;
        }
    }
    return 0;
}

/* Official ST VL53L1X status decoding table (index = raw 5-bit status). */
static const uint8_t STATUS_RTN_TABLE[24] = {
    255, 255, 255, 5, 2, 4, 1, 7, 3, 0,
    255, 255, 9, 13, 255, 255, 255, 255, 10, 6,
    255, 255, 11, 12
};

VL53L1X_Status_t VL53L1X_GetDistance(I2C_HandleTypeDef *hi2c, uint16_t *pDistanceMm)
{
    uint8_t rawStatus = 0;
    uint8_t decodedStatus = 255;
    uint16_t rawDistance = 0;

    if (I2C_ReadReg8(hi2c, VL53L1X_REG_RANGE_STATUS, &rawStatus) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }

    if (I2C_ReadReg16(hi2c, VL53L1X_REG_RANGE_MM, &rawDistance) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }

    rawStatus &= 0x1F;
    if (rawStatus < 24)
    {
        decodedStatus = STATUS_RTN_TABLE[rawStatus];
    }

    *pDistanceMm = rawDistance;

    /* 0 = valid, 1 = sigma warning, 2 = signal warning, 7 = wrap warning. */
    if (decodedStatus == 0 || decodedStatus == 1 || decodedStatus == 2 || decodedStatus == 7)
    {
        return VL53L1X_OK;
    }

    return VL53L1X_INVALID;
}

VL53L1X_Status_t VL53L1X_ClearInterrupt(I2C_HandleTypeDef *hi2c)
{
    if (I2C_WriteReg8(hi2c, VL53L1X_REG_INT_CLEAR, 0x01) != HAL_OK)
    {
        return VL53L1X_ERROR;
    }
    return VL53L1X_OK;
}
