/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : VL53L1X + LCD: SW1 measure/hold/sleep + pot debug modes
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  * Presentation (SW1): Ready → Live cm → Hold; 30 s idle → sleep; wake restores.
  * Debug (potentiometer PA7 / ADC1_IN12):
  *   0–33%  UART TEST  — stream pot + distance on VCP
  *  34–66%  STANDARD   — normal LCD UI (scenarios above)
  *  67–100% LCD DEBUG  — type on the VCP, Enter writes LCD row 2
  * SW1: PA3 pull-up, pressed = 0, EXTI3 falling (wake source)
 * Power: TIM7 is the HAL tick (not SysTick); the loop idles in __WFI(). While
 *        idling TIM7 becomes one coarse one-shot of up to 100 ms, so the CPU
 *        wakes once per wait instead of every millisecond. In sleep the tick
 *        is suspended entirely, leaving SW1 (EXTI3) as the only wake source.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "lcd.h"
#include "app_types.h"
#include "pot.h"
#include "distance_app.h"
#include "uart_console.h"
#include "app_ui.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  app_state_t state = ST_WAIT_INPUT;
  PotMode_t pot_mode = MODE_STANDARD_LCD;
  PotMode_t last_pot_mode = MODE_STANDARD_LCD;
  uint8_t was_hold = 0;
  uint8_t pot_percent = 0;
  uint16_t raw_adc = 0;
  uint32_t t_idle;
  uint32_t t_tof_poll = 0;
  uint32_t last_print_time = 0;
  uint32_t shown_s = 999U;
  int shown_cm = -1;
  app_state_t shown_state = ST_SLEEP;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* I2C1 (PB6 SCL / PB7 SDA, open-drain + pull-up in MSP) is shared by the
   * LCD backpack and the VL53L1X. MX_I2C1_Init() already ran; LCD_Init needs
   * that handle. Two slaves on one bus: never overlap LCD nibbles with ToF. */
  uart_console_start();

  printf("\r\n=========================================\r\n");
  printf("   VL53L1X + LCD (SW1 + pot debug)       \r\n");
  printf("=========================================\r\n");

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    printf("[WARN] ADC Calibration Failed!\r\n");
  }

  t_idle = HAL_GetTick();
  t_tof_poll = t_idle;
  shown_s = 0U;
  shown_cm = distance_cm_get();

  if (tof_try_attach() != 0U)
  {
    state = ST_READY;
    shown_state = ST_READY;
    printf("[OK] VL53L1X ready - System Ready\r\n");
  }
  else
  {
    state = ST_WAIT_INPUT;
    shown_state = ST_WAIT_INPUT;
    printf("[WAIT] No VL53 ACK yet - waiting / SW1 skip\r\n");
  }

  LCD_Init(&hi2c1);
  /* Probe is already done inside LCD_Init (0x27 then 0x3F); this is VCP only. */
  if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(0x27U << 1), 1, 50) == HAL_OK)
  {
    printf("[OK] LCD ACK at 0x27\r\n");
  }
  else if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(0x3FU << 1), 1, 50) == HAL_OK)
  {
    printf("[OK] LCD ACK at 0x3F\r\n");
  }
  else
  {
    printf("[WARN] LCD no ACK at 0x27/0x3F\r\n");
  }

  if (state == ST_READY)
  {
    LCD_WriteLine(0, "System Ready");
    LCD_WriteLine(1, "On: 30s");
  }
  else
  {
    LCD_WriteLine(0, "Waiting for");
    LCD_WriteLine(1, "input device");
  }
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    pot_sample(&raw_adc, &pot_percent, &pot_mode);
    if (pot_mode != last_pot_mode)
    {
      app_on_pot_mode_change(pot_mode, state, &shown_s, &shown_cm, &shown_state);
      last_pot_mode = pot_mode;
    }

    if (app_sw1_poll(&state, &was_hold) != 0U)
    {
      if (state != ST_WAIT_INPUT)
      {
        t_idle = HAL_GetTick();
      }
      shown_state = ST_SLEEP;
      shown_cm = -1;
    }

    uart_console_poll(state, pot_mode, &t_idle);

    if ((state == ST_WAIT_INPUT) && ((HAL_GetTick() - t_tof_poll) >= TOF_POLL_MS))
    {
      t_tof_poll = HAL_GetTick();
      if (tof_try_attach() != 0U)
      {
        state = ST_READY;
        t_idle = HAL_GetTick();
        shown_state = ST_SLEEP;
        printf("[OK] VL53L1X connected - System Ready\r\n");
      }
    }

    tof_service_live(state, pot_mode);

    if ((pot_mode == MODE_UART_TEST) && (state != ST_SLEEP) && (state != ST_WAIT_INPUT))
    {
      if ((HAL_GetTick() - last_print_time) >= UART_PRINT_MS)
      {
        printf("[TEST MODE] Pot: %3u%% (%4u) | Distance: %4u mm (%3d cm) st=%u\r\n",
               pot_percent, raw_adc, distance_mm_get(), distance_cm_get(),
               (unsigned)state);
        last_print_time = HAL_GetTick();
      }
    }

    if ((state != ST_SLEEP) && (state != ST_WAIT_INPUT)
        && ((HAL_GetTick() - t_idle) >= TIMEOUT_MS))
    {
      /* 30 s idle: stop ranging, remember Hold, LCD display/backlight off.
       * DDRAM and was_hold stay in RAM. Next loop uses app_wait_sleep() —
       * MCU Sleep + __WFI, not Stop/Standby, so wake can restore Hold. */
      was_hold = (state == ST_HOLD) ? 1U : 0U;
      tof_stop();
      state = ST_SLEEP;
      LCD_Sleep();
      printf("[SLEEP] idle 30 s\r\n");
    }

    app_paint(state, pot_mode, t_idle, &shown_s, &shown_cm, &shown_state);
    app_led(state);

    if (state == ST_SLEEP)
    {
      /* Tick suspended; only EXTI3 (SW1) wakes. Not Stop/Standby. */
      app_wait_sleep();
    }
    else
    {
      app_wake_clear();
      HAL_TickSleep(app_idle_ms(state));
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */
  /* Fast Mode, 7-bit. Pins (open-drain + pull-up) are in HAL_I2C_MspInit. */
  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00B07CB4;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_8, GPIO_PIN_SET);

  /*Configure GPIO pin : PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA4 PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* SW1 on PA3 (pull-up, pressed = 0) — not in .ioc; keep in USER CODE */
  GPIO_InitStruct.Pin = SW1_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SW1_PORT, &GPIO_InitStruct);

  /* SW1 is the only wake source while the LCD sleeps; ISR just clears the flag.
     Below EXTI0/tick priority so it cannot cut into an LCD I2C nibble write. */
  HAL_NVIC_SetPriority(EXTI3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  /*
   * Project_LCD has no PB0 EXTI. VL53 GPIO1/INT on the Click often lands on
   * PB0 — EXTI0 at priority 0 can preempt mid-LCD I2C nibble writes and leave
   * the HD44780 desynced (garbage glyphs). Driver polls INT via I2C registers.
   * Keep EXTI0 off for the whole run, not only "during" LCD writes.
   */
  HAL_NVIC_DisableIRQ(EXTI0_IRQn);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
