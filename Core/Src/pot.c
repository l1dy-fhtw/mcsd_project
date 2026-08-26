/**
 * @file    pot.c
 * @brief   Potentiometer (PA7 / ADC1_IN12) → percent → debug mode band
 */
#include "pot.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

void pot_sample(uint16_t *raw, uint8_t *percent, PotMode_t *mode)
{
  uint16_t adc;
  uint8_t pct;

  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);
  adc = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  if (adc > 4095U)
  {
    adc = 4095U;
  }
  pct = (uint8_t)((adc * 100UL) / 4095UL);

  if (raw != 0)
  {
    *raw = adc;
  }
  if (percent != 0)
  {
    *percent = pct;
  }
  if (mode != 0)
  {
    if (pct <= 33U)
    {
      *mode = MODE_UART_TEST;
    }
    else if (pct <= 66U)
    {
      *mode = MODE_STANDARD_LCD;
    }
    else
    {
      *mode = MODE_LCD_DEBUG;
    }
  }
}
