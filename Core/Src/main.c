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
  *  67–100% LCD DEBUG  — pot% + mm on LCD while Live/Hold
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
#include <string.h>
#include "vl53l1x_driver.h"
#include "lcd.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  ST_WAIT_INPUT = 0,
  ST_READY,
  ST_LIVE,
  ST_HOLD,
  ST_SLEEP
} app_state_t;

typedef enum
{
  MODE_UART_TEST = 0,   /*  0% - 33%: UART stream */
  MODE_STANDARD_LCD,    /* 34% - 66%: presentation UI */
  MODE_LCD_DEBUG        /* 67% - 100%: LCD debug layout */
} PotMode_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SW1_PIN        GPIO_PIN_3
#define SW1_PORT       GPIOA
#define DEBOUNCE_MS    20U
#define TIMEOUT_MS     30000U
#define CM_START       50
#define CM_MIN         1
#define CM_MAX         200
#define CM_STEP        1
#define CM_BLINK       40
#define CM_PANIC       10
#define BLINK_SLOW_MS  500U
#define BLINK_FAST_MS  80U
#define TOF_POLL_MS    500U
#define UART_PRINT_MS  100U
/*
 * Idle cadence, i.e. how long the loop may sleep before it must look again.
 * Every periodic job is covered by one of the two values:
 *   LOOP_FAST_MS  LED blink (half period 40..250 ms) and ST_LIVE ranging
 *   LOOP_IDLE_MS  UART_PRINT_MS 100, LCD refresh 500, TOF_POLL_MS 500,
 *                 countdown 1 s, pot mode change; SW1 never waits, EXTI3
 *                 cuts the sleep short.
 */
#define LOOP_FAST_MS   10U
#define LOOP_IDLE_MS   100U
#define RX_RING_SIZE   64U   /* power of two: index wrap is a mask */
#define LCD_COLS       16U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static int distance_cm = CM_START;
static uint8_t tof_online = 0;
static uint16_t raw_adc = 0;
static uint8_t pot_percent = 0;
static uint16_t distance_mm = 0;
static PotMode_t pot_mode = MODE_STANDARD_LCD;
static PotMode_t last_pot_mode = MODE_STANDARD_LCD;
static uint32_t last_print_time = 0;
static volatile uint8_t sw1_wake = 0;   /* EXTI3 seen: skip __WFI so no press is lost */

/*
 * USART2 receive ring. USART2 has no RX FIFO, so at 115200 baud a byte has to
 * be taken within ~87 us — far shorter than the up to 100 ms the loop sleeps in
 * HAL_TickSleep(). Reception is therefore interrupt driven, and the interrupt
 * doubles as a wake source so a keystroke returns from __WFI at once.
 */
static uint8_t rx_byte = 0;                     /* staging cell for HAL_UART_Receive_IT */
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head = 0;           /* written by the ISR */
static volatile uint32_t rx_tail = 0;           /* written by the main loop */

/* Text typed in the LCD DEBUG pot mode. */
static char uart_line[LCD_COLS + 1] = "";       /* being typed, echoed on the VCP only */
static uint8_t uart_line_len = 0;
static char uart_msg[LCD_COLS + 1] = "";        /* last line committed with Enter */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static uint16_t Read_Potentiometer(void);
static uint8_t  Calculate_Pot_Percent(uint16_t adc_val);
static PotMode_t Pot_Mode_From_Percent(uint8_t percent);
static int  distance_get_cm(void);
static uint8_t sw1_edge(void);
static void app_draw(app_state_t state, int cm, uint32_t left_s);
static void app_on_sw1(app_state_t *state, uint8_t *was_hold);
static void led_update(int cm, uint8_t sleep);
static uint32_t loop_idle_ms(app_state_t state, int cm);
static uint8_t tof_present(void);
static uint8_t tof_connect(void);
static void tof_read_live(void);
static void tof_ensure_ranging(void);
static void distance_on_key(uint8_t key);
static void uart_rx_start(void);
static uint8_t uart_rx_pop(uint8_t *byte);
static void lcd_uart_reset(void);
static uint8_t lcd_uart_key(uint8_t c);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

static void uart_rx_start(void)
{
  (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/* One byte arrived: park it and re-arm before the next one can overrun. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint32_t next;

  if (huart->Instance != USART2)
  {
    return;
  }

  next = (rx_head + 1U) & (RX_RING_SIZE - 1U);
  if (next != rx_tail)   /* full ring: drop rather than overwrite unread input */
  {
    rx_ring[rx_head] = rx_byte;
    rx_head = next;
  }
  uart_rx_start();
}

/* Without this a single overrun (typing while the CPU is busy) would stop RX. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART2)
  {
    return;
  }

  __HAL_UART_CLEAR_OREFLAG(huart);
  __HAL_UART_CLEAR_NEFLAG(huart);
  __HAL_UART_CLEAR_FEFLAG(huart);
  __HAL_UART_CLEAR_PEFLAG(huart);
  uart_rx_start();
}

static uint8_t uart_rx_pop(uint8_t *byte)
{
  if (rx_tail == rx_head)
  {
    return 0;
  }

  *byte = rx_ring[rx_tail];
  rx_tail = (rx_tail + 1U) & (RX_RING_SIZE - 1U);
  return 1;
}

static uint16_t Read_Potentiometer(void)
{
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);
  uint16_t val = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return val;
}

static uint8_t Calculate_Pot_Percent(uint16_t adc_val)
{
  if (adc_val > 4095U)
  {
    adc_val = 4095U;
  }
  return (uint8_t)((adc_val * 100UL) / 4095UL);
}

static PotMode_t Pot_Mode_From_Percent(uint8_t percent)
{
  if (percent <= 33U)
  {
    return MODE_UART_TEST;
  }
  if (percent <= 66U)
  {
    return MODE_STANDARD_LCD;
  }
  return MODE_LCD_DEBUG;
}

static int distance_get_cm(void)
{
  return distance_cm;
}

static uint8_t tof_present(void)
{
  return (HAL_I2C_IsDeviceReady(&hi2c1, VL53L1X_I2C_ADDR, 1, 10) == HAL_OK) ? 1U : 0U;
}

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

/* Ensure continuous ranging is running (idempotent start). */
static void tof_ensure_ranging(void)
{
  if (tof_online != 0U)
  {
    (void)VL53L1X_StartMeasurement(&hi2c1);
  }
}

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

/* Wake source only: debounce and the state machine stay in the main loop. */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == SW1_PIN)
  {
    sw1_wake = 1;
  }
}

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
  return (now == 0U) ? 1U : 0U;
}

static void app_draw(app_state_t state, int cm, uint32_t left_s)
{
  char line1[17];
  char line2[17];

  if (cm < 0)
  {
    cm = 0;
  }
  if (cm > 999)
  {
    cm = 999;
  }
  if (left_s > 99U)
  {
    left_s = 99U;
  }

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

static void app_on_sw1(app_state_t *state, uint8_t *was_hold)
{
  switch (*state)
  {
    case ST_WAIT_INPUT:
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

/* Drop a half-typed line, e.g. when the pot re-enters LCD DEBUG. */
static void lcd_uart_reset(void)
{
  uart_line[0] = '\0';
  uart_line_len = 0;
}

/**
  * @brief  Feed one received character to the LCD DEBUG line editor.
  * @param  c  Byte from the UART ring.
  * @retval 1 when Enter committed a line into uart_msg, 0 otherwise.
  *
  * The line being typed is echoed on the VCP only; the LCD is written once per
  * Enter so a blocking I2C update does not run on every keystroke.
  */
static uint8_t lcd_uart_key(uint8_t c)
{
  if ((c == '\r') || (c == '\n'))
  {
    (void)strncpy(uart_msg, uart_line, sizeof(uart_msg));
    uart_msg[sizeof(uart_msg) - 1U] = '\0';
    lcd_uart_reset();
    printf("\r\n[LCD] %s\r\n> ", uart_msg);
    return 1;
  }

  if ((c == '\b') || (c == 0x7FU))
  {
    if (uart_line_len > 0U)
    {
      uart_line_len--;
      uart_line[uart_line_len] = '\0';
      printf("\b \b");
    }
    return 0;
  }

  if ((c >= 0x20U) && (c < 0x7FU) && (uart_line_len < LCD_COLS))
  {
    uart_line[uart_line_len] = (char)c;
    uart_line_len++;
    uart_line[uart_line_len] = '\0';
    printf("%c", (char)c);
  }

  return 0;   /* over 16 chars or a control code: ignore, the LCD row is full */
}

/* How long the loop may sleep: only a blinking LED or live ranging need speed. */
static uint32_t loop_idle_ms(app_state_t state, int cm)
{
  if (state == ST_LIVE)
  {
    return LOOP_FAST_MS;
  }
  if ((state != ST_SLEEP) && (state != ST_WAIT_INPUT) && (cm <= CM_BLINK))
  {
    return LOOP_FAST_MS;   /* led_update() is toggling */
  }
  return LOOP_IDLE_MS;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  app_state_t state = ST_WAIT_INPUT;
  uint8_t was_hold = 0;
  uint8_t key = 0;
  uint32_t t_idle;
  uint32_t t_tof_poll = 0;
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
  uart_rx_start();   /* lab 1/2 keys and the LCD DEBUG text entry both read this ring */

  printf("\r\n=========================================\r\n");
  printf("   VL53L1X + LCD (SW1 + pot debug)       \r\n");
  printf("=========================================\r\n");

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    printf("[WARN] ADC Calibration Failed!\r\n");
  }

  /* ToF first, then LCD once — avoid double LCD_Init after bus traffic. */
  t_idle = HAL_GetTick();
  t_tof_poll = t_idle;
  shown_s = 0U;
  shown_cm = distance_get_cm();

  if ((tof_present() != 0U) && (tof_connect() != 0U))
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
    /* Potentiometer debug layer (always sampled). */
    raw_adc = Read_Potentiometer();
    pot_percent = Calculate_Pot_Percent(raw_adc);
    pot_mode = Pot_Mode_From_Percent(pot_percent);

    if (pot_mode != last_pot_mode)
    {
      switch (pot_mode)
      {
        case MODE_UART_TEST:
          printf("\r\n>>> Pot debug: [1] UART TEST (0-33%%) <<<\r\n");
          /* Old working pot path: banner once, then leave LCD alone (bus for VL53). */
          if (state != ST_SLEEP)
          {
            LCD_WriteLine(0, "Mode: UART Test");
            LCD_WriteLine(1, "Check Terminal");
          }
          if ((tof_online != 0U) && (state != ST_SLEEP) && (state != ST_WAIT_INPUT))
          {
            tof_ensure_ranging();
          }
          break;
        case MODE_STANDARD_LCD:
          printf("\r\n>>> Pot debug: [2] STANDARD UI (34-66%%) <<<\r\n");
          shown_state = ST_SLEEP; /* force one presentation redraw */
          shown_cm = -1;
          shown_s = 999U;
          break;
        case MODE_LCD_DEBUG:
          printf("\r\n>>> Pot debug: [3] LCD DEBUG (67-100%%) <<<\r\n");
          printf("Type text at 115200 8N1 and press Enter.\r\n");
          printf("The line then appears on LCD row 2 (max %u chars).\r\n> ",
                 (unsigned)LCD_COLS);
          lcd_uart_reset();   /* stale half-typed line from a previous visit */
          if (state != ST_SLEEP)
          {
            LCD_WriteLine(0, "Type UART 115200");
            LCD_WriteLine(1, uart_msg);
            shown_state = state;   /* already painted: no repaint below */
          }
          break;
        default:
          break;
      }
      last_pot_mode = pot_mode;
    }

    /* Scenario 1: SW1 Ready → Live → Hold (wake from sleep restores hold). */
    if (sw1_edge() != 0U)
    {
      app_state_t before = state;

      if (state != ST_WAIT_INPUT)
      {
        t_idle = HAL_GetTick();
      }
      app_on_sw1(&state, &was_hold);
      shown_state = ST_SLEEP;
      shown_cm = -1;

      if ((tof_online != 0U) && (before == ST_SLEEP))
      {
        /* Wake: restart ranging for Live or UART debug from Ready. */
        if ((state == ST_LIVE) || (pot_mode == MODE_UART_TEST))
        {
          tof_ensure_ranging();
        }
      }
      else if ((state == ST_LIVE) && (tof_online != 0U) && (before != ST_LIVE))
      {
        tof_ensure_ranging();
      }
    }

    /*
     * Drain everything the RX interrupt collected. LCD DEBUG owns the keyboard
     * when selected; otherwise the lab 1/2 keys work while the ToF is offline.
     */
    while (uart_rx_pop(&key) != 0U)
    {
      if (state != ST_SLEEP)
      {
        t_idle = HAL_GetTick();   /* typing is user activity: do not sleep mid-line */
      }

      if (pot_mode == MODE_LCD_DEBUG)
      {
        if ((lcd_uart_key(key) != 0U) && (state != ST_SLEEP))
        {
          LCD_WriteLine(1, uart_msg);   /* Enter: commit the line */
        }
      }
      else if ((tof_online == 0U) && ((state == ST_LIVE) || (state == ST_READY)))
      {
        distance_on_key(key);
      }
    }

    /* Attach VL53 only while waiting — do not probe during ranging (bus glitches). */
    if ((state == ST_WAIT_INPUT) && ((HAL_GetTick() - t_tof_poll) >= TOF_POLL_MS))
    {
      t_tof_poll = HAL_GetTick();
      if ((tof_present() != 0U) && (tof_connect() != 0U))
      {
        state = ST_READY;
        t_idle = HAL_GetTick();
        shown_state = ST_SLEEP;
        printf("[OK] VL53L1X connected - System Ready\r\n");
      }
    }

    /*
     * Range updates:
     * - LIVE: always (scenario 2)
     * - UART TEST pot mode: also while READY so VCP shows live mm without SW1 Live
     */
    if (tof_online != 0U)
    {
      if (state == ST_LIVE)
      {
        tof_read_live();
      }
      else if ((pot_mode == MODE_UART_TEST)
               && (state != ST_SLEEP) && (state != ST_WAIT_INPUT) && (state != ST_HOLD))
      {
        tof_read_live();
      }
    }

    /* Pot UART TEST: print when a fresh sample arrived (distance_mm updated). */
    if ((pot_mode == MODE_UART_TEST) && (state != ST_SLEEP) && (state != ST_WAIT_INPUT))
    {
      if ((HAL_GetTick() - last_print_time) >= UART_PRINT_MS)
      {
        printf("[TEST MODE] Pot: %3u%% (%4u) | Distance: %4u mm (%3d cm) st=%u\r\n",
               pot_percent, raw_adc, distance_mm, distance_cm, (unsigned)state);
        last_print_time = HAL_GetTick();
      }
    }

    /* Scenario 3: 30 s idle -> sleep (LCD off, stop ranging). */
    if ((state != ST_SLEEP) && (state != ST_WAIT_INPUT))
    {
      if ((HAL_GetTick() - t_idle) >= TIMEOUT_MS)
      {
        was_hold = (state == ST_HOLD) ? 1U : 0U;
        if (tof_online != 0U)
        {
          (void)VL53L1X_StopMeasurement(&hi2c1);
        }
        state = ST_SLEEP;
        LCD_Sleep();
        printf("[SLEEP] idle 30 s\r\n");
      }
    }

    /*
     * LCD DEBUG owns both rows: row 0 is the fixed hint, row 1 is whatever was
     * last committed with Enter. Nothing periodic here — the drain loop writes
     * row 1 — so this only repaints after a state change, e.g. waking from sleep.
     */
    if ((pot_mode == MODE_LCD_DEBUG) && (state != ST_SLEEP) && (shown_state != state))
    {
      LCD_WriteLine(0, "Type UART 115200");
      LCD_WriteLine(1, uart_msg);
      shown_state = state;
    }

    /*
     * LCD: same idea as Project_LCD + old pot path.
     * - UART TEST: banner only (written on mode entry)
     * - LCD DEBUG: handled above
     * - STANDARD: presentation app_draw on state / second / cm change
     */
    if ((state != ST_SLEEP) && (pot_mode == MODE_STANDARD_LCD))
    {
      uint32_t left_s;
      uint32_t elapsed;
      int cm;
      static uint32_t t_lcd = 0;

      elapsed = HAL_GetTick() - t_idle;
      if ((state == ST_WAIT_INPUT) || (elapsed >= TIMEOUT_MS))
      {
        left_s = 0U;
      }
      else
      {
        left_s = (TIMEOUT_MS - elapsed) / 1000U;
      }
      cm = distance_get_cm();

      if ((HAL_GetTick() - t_lcd) >= 500U)
      {
        t_lcd = HAL_GetTick();

        if (state == ST_LIVE)
        {
          /* Line0 distance like old pot Standard; line1 countdown when second ticks. */
          if (cm != shown_cm)
          {
            Update_LCD_Standard(distance_mm);
            shown_cm = cm;
          }
          if ((left_s != shown_s) || (state != shown_state))
          {
            char line2[17];
            (void)sprintf(line2, "On: %lus", (unsigned long)((left_s > 99U) ? 99U : left_s));
            LCD_WriteLine(1, line2);
            shown_s = left_s;
            shown_state = state;
          }
        }
        else if ((left_s != shown_s) || (cm != shown_cm) || (state != shown_state))
        {
          app_draw(state, cm, left_s);
          shown_s = left_s;
          shown_cm = cm;
          shown_state = state;
        }
      }
    }

    led_update(distance_get_cm(),
               ((state == ST_SLEEP) || (state == ST_WAIT_INPUT)) ? 1U : 0U);

    /* Idle instead of spinning in HAL_Delay. LCD off: SW1 only, so the TIM7
       tick stops too (it would just wake the CPU for nothing). */
    if (state == ST_SLEEP)
    {
      HAL_SuspendTick();
      __disable_irq();
      if (sw1_wake == 0U)
      {
        __WFI();      /* a pending EXTI3 still wakes us with PRIMASK set */
      }
      sw1_wake = 0;
      __enable_irq(); /* let the EXTI3 handler run */
      HAL_ResumeTick(); /* tick back before the next HAL_GetTick user */
    }
    else
    {
      /* One coarse TIM7 one-shot for the whole wait, so the CPU wakes once
         instead of every millisecond. EXTI3 still returns immediately. */
      sw1_wake = 0;
      HAL_TickSleep(loop_idle_ms(state, distance_get_cm()));
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
