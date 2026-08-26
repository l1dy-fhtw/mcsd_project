/**
 * @file    app_ui.c
 * @brief   SW1 debounce/state, LCD paint, LED blink, sleep WFI
 */
#include "app_ui.h"
#include "distance_app.h"
#include "uart_console.h"
#include "lcd.h"
#include <stdio.h>

#define DEBOUNCE_MS    20U
#define BLINK_SLOW_MS  500U
#define BLINK_FAST_MS  80U

static volatile uint8_t sw1_wake = 0;

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == SW1_PIN)
  {
    sw1_wake = 1;
  }
}

static uint8_t sw1_falling_edge(void)
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

uint8_t app_sw1_poll(app_state_t *state, uint8_t *was_hold)
{
  if (sw1_falling_edge() == 0U)
  {
    return 0;
  }

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
  return 1;
}

void app_on_pot_mode_change(PotMode_t mode, app_state_t state,
                            uint32_t *shown_s, int *shown_cm,
                            app_state_t *shown_state)
{
  switch (mode)
  {
    case MODE_UART_TEST:
      printf("\r\n>>> Pot debug: [1] UART TEST (0-33%%) <<<\r\n");
      if (state != ST_SLEEP)
      {
        LCD_WriteLine(0, "Mode: UART Test");
        LCD_WriteLine(1, "Check Terminal");
      }
      break;
    case MODE_STANDARD_LCD:
      printf("\r\n>>> Pot debug: [2] STANDARD UI (34-66%%) <<<\r\n");
      *shown_state = ST_SLEEP;
      *shown_cm = -1;
      *shown_s = 999U;
      break;
    case MODE_LCD_DEBUG:
      printf("\r\n>>> Pot debug: [3] LCD DEBUG (67-100%%) <<<\r\n");
      printf("Type text at 115200 8N1 and press Enter.\r\n");
      printf("The line then appears on LCD row 2 (max %u chars).\r\n> ",
             (unsigned)LCD_COLS);
      uart_console_reset_line();
      if (state != ST_SLEEP)
      {
        LCD_WriteLine(0, "Type UART 115200");
        LCD_WriteLine(1, uart_console_msg());
        *shown_state = state;
      }
      break;
    default:
      break;
  }
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

void app_paint(app_state_t state, PotMode_t mode, uint32_t t_idle,
               uint32_t *shown_s, int *shown_cm, app_state_t *shown_state)
{
  static uint32_t t_lcd = 0;
  uint32_t left_s;
  uint32_t elapsed;
  int cm;

  if (state == ST_SLEEP)
  {
    return;
  }

  if (mode == MODE_LCD_DEBUG)
  {
    if (*shown_state != state)
    {
      LCD_WriteLine(0, "Type UART 115200");
      LCD_WriteLine(1, uart_console_msg());
      *shown_state = state;
    }
    return;
  }

  if (mode != MODE_STANDARD_LCD)
  {
    return;
  }

  elapsed = HAL_GetTick() - t_idle;
  if ((state == ST_WAIT_INPUT) || (elapsed >= TIMEOUT_MS))
  {
    left_s = 0U;
  }
  else
  {
    left_s = (TIMEOUT_MS - elapsed) / 1000U;
  }
  cm = distance_cm_get();

  if ((HAL_GetTick() - t_lcd) < 500U)
  {
    return;
  }
  t_lcd = HAL_GetTick();

  if (state == ST_LIVE)
  {
    if (cm != *shown_cm)
    {
      Update_LCD_Standard(distance_mm_get());
      *shown_cm = cm;
    }
    if ((left_s != *shown_s) || (state != *shown_state))
    {
      char line2[17];
      (void)sprintf(line2, "On: %lus", (unsigned long)((left_s > 99U) ? 99U : left_s));
      LCD_WriteLine(1, line2);
      *shown_s = left_s;
      *shown_state = state;
    }
  }
  else if ((left_s != *shown_s) || (cm != *shown_cm) || (state != *shown_state))
  {
    app_draw(state, cm, left_s);
    *shown_s = left_s;
    *shown_cm = cm;
    *shown_state = state;
  }
}

void app_led(app_state_t state)
{
  static uint32_t t0 = 0;
  static uint8_t on = 1;
  int cm = distance_cm_get();
  uint8_t sleep = ((state == ST_SLEEP) || (state == ST_WAIT_INPUT)) ? 1U : 0U;
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

uint32_t app_idle_ms(app_state_t state)
{
  int cm = distance_cm_get();

  if (state == ST_LIVE)
  {
    return LOOP_FAST_MS;
  }
  if ((state != ST_SLEEP) && (state != ST_WAIT_INPUT) && (cm <= CM_BLINK))
  {
    return LOOP_FAST_MS;
  }
  return LOOP_IDLE_MS;
}

void app_wait_sleep(void)
{
  HAL_SuspendTick();
  __disable_irq();
  if (sw1_wake == 0U)
  {
    __WFI();
  }
  sw1_wake = 0;
  __enable_irq();
  HAL_ResumeTick();
}

void app_wake_clear(void)
{
  sw1_wake = 0;
}
