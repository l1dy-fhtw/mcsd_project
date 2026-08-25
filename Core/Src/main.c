/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : MCSD project - I2C LCD + VL53 wait / range demo
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
  * Student: Akos Eross  84655
  *
  * ## Overview
  * 16x2 HD44780 LCD (PCF8574 I2C). Until the VL53L1X ACKs on the same
  * bus, the LCD shows "Waiting for / input device". After that, SW1
  * steps ready → live cm → freeze. 30 s idle sleeps the display.
  *
  * ## Key Components
  * - I2C1: PB6 SCL / PB7 SDA (Nucleo D5 / D4), 100 kHz
  * - lcd.h / lcd.c: PCF8574 HD44780; VL53L1X 0x29 (8-bit 0x52)
  * - SW1: PA3, pull-up, pressed = 0
  * - USART2 VCP: 115200 8N1; keys '1'/'2' only while no ToF (lab)
  * - LD3 green (PB3): far = solid, closer than 40 cm = faster blink
  *
  * ## How It Works
  * Boot waits for Dani's sensor. On ACK: init, System Ready, then SW1.
  * Unplug returns to the waiting screen. HAL_GetTick drives poll (500 ms),
  * debounce, blink and the 30 s timeout (timeout off while waiting).
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "lcd.h"
#include "vl53l1x_driver.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  ST_WAIT_INPUT = 0, // no VL53 ACK yet
  ST_READY,          // LCD on, "System Ready"
  ST_LIVE,           // refresh cm while it changes
  ST_HOLD,           // frozen cm, LCD stays on
  ST_SLEEP           // backlight off, measurement stopped
} app_state_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SW1_PIN        GPIO_PIN_3   // A2 / PA3
#define SW1_PORT       GPIOA
#define DEBOUNCE_MS    20U          // ignore bounce, still one event per press
#define TIMEOUT_MS     30000U       // scenario 3: idle → sleep
#define CM_START       50           // first fake reading
#define CM_MIN         1            // VL53-ish lower end
#define CM_MAX         200          // VL53L0X ~2 m
#define CM_STEP        1
#define UART_BAUD      115200
#define CM_SAFE        50           // above this: far, LED solid (same as start)
#define CM_BLINK       40           // at/below: start blinking
#define CM_PANIC       10           // fastest blink
#define BLINK_SLOW_MS  500U         // full period at 40 cm (250 on / 250 off)
#define BLINK_FAST_MS  80U          // full period at 10 cm
#define TOF_POLL_MS    500U         // I2C probe for Dani's VL53
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static int distance_cm = CM_START;      // UART '1'/'2', or VL53 mm/10
static uint8_t tof_online = 0;          // 1 after VL53 init + start
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN PFP */
static void distance_on_key(uint8_t key);
static int  distance_get_cm(void);
static uint8_t sw1_edge(void);
static void app_draw(app_state_t state, int cm, uint32_t left_s);
static void app_on_sw1(app_state_t *state, uint8_t *was_hold);
static void led_update(int cm, uint8_t sleep);
static uint8_t tof_present(void);
static uint8_t tof_connect(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Retarget printf to USART2 for Virtual COM Port */
int __io_putchar(int ch) {
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* Shared cm: UART lab until ToF is up; VL53 writes this only in LIVE. */
static int distance_get_cm(void)
{
  return distance_cm;
}

/* HOW: ACK on 0x52? WHY: same I2C1 as LCD; no Device ID needed just to detect plug. */
static uint8_t tof_present(void)
{
  return (HAL_I2C_IsDeviceReady(&hi2c1, VL53L1X_I2C_ADDR, 1, 10) == HAL_OK) ? 1U : 0U;
}

/* HOW: Dani driver init + start. WHY: ACK alone is not ranging yet. */
static uint8_t tof_connect(void)
{
  if (VL53L1X_Init(&hi2c1) != VL53L1X_OK)
  {
    return 0;
  }
  if (VL53L1X_StartMeasurement(&hi2c1) != VL53L1X_OK)
  {
    return 0;
  }
  tof_online = 1;
  return 1;
}

/* HOW: mm/10 → cm, clamp 1..200. WHY: skip if not ready — no HAL_Delay in the loop. */
static void tof_read_live(void)
{
  uint16_t mm = 0;
  int cm;

  if (VL53L1X_IsDataReady(&hi2c1) == 0U)
  {
    return;
  }
  if (VL53L1X_GetDistance(&hi2c1, &mm) == VL53L1X_OK)
  {
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
  (void)VL53L1X_ClearInterrupt(&hi2c1);  // allow the next sample
}

/* UART '1' closer / '2' farther. Ignored once tof_online (see main loop). */
static void distance_on_key(uint8_t key)
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

/* Falling edge on SW1 after DEBOUNCE_MS. Released pin reads 1 (pull-up). */
static uint8_t sw1_edge(void)
{
  static uint8_t last = 1;
  static uint32_t t_change = 0;
  uint8_t now;

  now = (HAL_GPIO_ReadPin(SW1_PORT, SW1_PIN) == GPIO_PIN_SET) ? 1U : 0U;
  if (now == last)
  {
    return 0;
  }
  if ((HAL_GetTick() - t_change) < DEBOUNCE_MS)
  {
    return 0;
  }

  t_change = HAL_GetTick();
  last = now;
  return (now == 0U) ? 1U : 0U; // 1 = pressed
}

/* 16-char lines. WAIT has no countdown; other modes show leftover sleep seconds. */
static void app_draw(app_state_t state, int cm, uint32_t left_s)
{
  char line1[17];
  char line2[17];

  (void)sprintf(line2, "On: %lus", (unsigned long)left_s);

  switch (state)
  {
    case ST_WAIT_INPUT:
      LCD_WriteLine(0, "Waiting for");
      LCD_WriteLine(1, "input device");
      return;
    case ST_READY:
      LCD_WriteLine(0, "System Ready");
      break;
    case ST_LIVE:
      (void)sprintf(line1, "Dist: %3d cm", cm);
      LCD_WriteLine(0, line1);
      break;
    case ST_HOLD:
      (void)sprintf(line1, "HOLD %3d cm", cm);
      LCD_WriteLine(0, line1);
      break;
    default:
      return;
  }
  LCD_WriteLine(1, line2);
}

/* SW1: wait→ready (lab skip), ready→live⇄hold, sleep→previous. */
static void app_on_sw1(app_state_t *state, uint8_t *was_hold)
{
  switch (*state)
  {
    case ST_WAIT_INPUT:
      /* Lab without ToF: SW1 continues with UART 1/2. */
      *state = ST_READY;
      break;
    case ST_SLEEP:
      LCD_Wake();
      *state = (*was_hold != 0U) ? ST_HOLD : ST_READY;
      break;
    case ST_READY:
      *state = ST_LIVE;
      break;
    case ST_LIVE:
      *state = ST_HOLD;
      break;
    case ST_HOLD:
      *state = ST_LIVE;
      break;
    default:
      break;
  }
}

/* LD3: solid when far, faster blink as cm drops from 40 to 10. No HAL_Delay. */
static void led_update(int cm, uint8_t sleep)
{
  static uint32_t t0 = 0;
  static uint8_t on = 1;
  uint32_t half_ms;

  if (sleep != 0U)
  {
    BSP_LED_Off(LED_GREEN);
    on = 0;
    return;
  }

  /* CM_SAFE (50) and the 40..50 band: still solid, shield red is on I2C. */
  if (cm > CM_BLINK)
  {
    BSP_LED_On(LED_GREEN);
    on = 1;
    return;
  }

  if (cm <= CM_PANIC)
  {
    half_ms = BLINK_FAST_MS / 2U;
  }
  else
  {
    uint32_t period_ms;

    period_ms = BLINK_FAST_MS
                + (uint32_t)(cm - CM_PANIC)
                  * (BLINK_SLOW_MS - BLINK_FAST_MS)
                  / (uint32_t)(CM_BLINK - CM_PANIC);
    half_ms = period_ms / 2U;
  }

  if ((HAL_GetTick() - t0) >= half_ms)
  {
    t0 = HAL_GetTick();
    on = (on != 0U) ? 0U : 1U;
    if (on != 0U)
    {
      BSP_LED_On(LED_GREEN);
    }
    else
    {
      BSP_LED_Off(LED_GREEN);
    }
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  app_state_t state = ST_WAIT_INPUT;  // boot: wait for VL53 ACK
  uint8_t was_hold = 0;               // sleep → HOLD if we froze before idle
  uint8_t key = 0;
  uint32_t t_idle;                    // last SW1 (sleep timer)
  uint32_t t_tof_poll = 0;            // 500 ms I2C probe
  uint32_t shown_s = 999U;            // last LCD seconds — skip redraw if same
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
  LCD_Init(&hi2c1);
  LCD_Wake();
  LCD_WriteLine(0, "Waiting for");
  LCD_WriteLine(1, "input device");
  t_idle = HAL_GetTick();
  t_tof_poll = t_idle;
  shown_s = 0U;
  shown_cm = distance_get_cm();
  shown_state = ST_WAIT_INPUT;

  printf("\r\n=========================================\r\n");
  printf("   VL53L1X Multi-Mode System Starting   \r\n");
  printf("=========================================\r\n");

  /* Calibrate ADC */
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
      printf("[WARN] ADC Calibration Failed!\r\n");
  }

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* SW1: ignored for sleep-timer while waiting for the ToF. */
    if (sw1_edge() != 0U)
    {
      if (state != ST_WAIT_INPUT)
      {
        t_idle = HAL_GetTick();
      }
      app_on_sw1(&state, &was_hold);
    }

    /* Lab keys only while Dani's sensor is not on the bus. */
    if ((tof_online == 0U) && (HAL_UART_Receive(&huart2, &key, 1, 0) == HAL_OK))
    {
      if ((state == ST_LIVE) || (state == ST_READY))
      {
        distance_on_key(key);
      }
    }

    /* 500 ms: attach on ACK, drop back to waiting if unplugged. */
    if ((HAL_GetTick() - t_tof_poll) >= TOF_POLL_MS)
    {
      t_tof_poll = HAL_GetTick();
      if (state == ST_WAIT_INPUT)
      {
        if ((tof_present() != 0U) && (tof_connect() != 0U))
        {
          state = ST_READY;
          t_idle = HAL_GetTick();
          shown_state = ST_SLEEP;
        }
      }
      else if (tof_online != 0U)
      {
        if (tof_present() == 0U)
        {
          (void)VL53L1X_StopMeasurement(&hi2c1);
          tof_online = 0;
          if (state != ST_SLEEP)
          {
            LCD_Wake();
          }
          state = ST_WAIT_INPUT;
          shown_state = ST_SLEEP;
        }
      }
      else if (tof_present() != 0U)
      {
        /* Plugged in after lab SW1 skip: take over from UART 1/2. */
        (void)tof_connect();
      }
    }

    if ((tof_online != 0U) && (state == ST_LIVE))
    {
      tof_read_live();  // HOLD keeps the last cm; do not poll ToF there
    }

    /* No SW1 for 30 s → LCD off. Waiting screen stays on. */
    if ((state != ST_SLEEP) && (state != ST_WAIT_INPUT))
    {
      if ((HAL_GetTick() - t_idle) >= TIMEOUT_MS)
      {
        was_hold = (state == ST_HOLD) ? 1U : 0U;
        state = ST_SLEEP;
        LCD_Sleep();
      }
    }

    /* Redraw only when the second, the cm, or the mode changed. */
    if (state != ST_SLEEP)
    {
      uint32_t left_s;
      int cm;

      left_s = (state == ST_WAIT_INPUT)
                   ? 0U
                   : (TIMEOUT_MS - (HAL_GetTick() - t_idle)) / 1000U;
      cm = distance_get_cm();
      if ((left_s != shown_s) || (cm != shown_cm) || (state != shown_state))
      {
        app_draw(state, cm, left_s);
        shown_s = left_s;
        shown_cm = cm;
        shown_state = state;
      }
    }

    /* Green LD3 follows cm; off while waiting or sleeping. */
    led_update(distance_get_cm(),
               ((state == ST_SLEEP) || (state == ST_WAIT_INPUT)) ? 1U : 0U);

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
  ADC_ChannelConfTypeDef sConfig = {0};

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
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{
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
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
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
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_8, GPIO_PIN_SET);

  /*Configure GPIO pin : PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : VCP_TX_Pin */
  GPIO_InitStruct.Pin = VCP_TX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(VCP_TX_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA3 */
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : VCP_RX_Pin */
  GPIO_InitStruct.Pin = VCP_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF3_USART2;
  HAL_GPIO_Init(VCP_RX_GPIO_Port, &GPIO_InitStruct);

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
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
