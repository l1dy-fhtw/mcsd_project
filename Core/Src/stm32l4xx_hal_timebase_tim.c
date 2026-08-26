/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32l4xx_hal_timebase_tim.c
  * @brief   HAL 1 ms time base on TIM7 instead of SysTick.
  ******************************************************************************
  * @attention
  *
  * ## Why
  * The main loop idles with __WFI(). In ST_SLEEP the app calls
  * HAL_SuspendTick() so only EXTI3 (SW1) can wake the CPU. SysTick cannot be
  * masked that way without losing the HAL time base, so TIM7 owns the tick and
  * every HAL_GetTick / HAL_Delay user follows it automatically: main.c
  * (debounce, 500 ms probe, 30 s idle, LED, UART throttle), lcd.c,
  * vl53l1x_driver.c and the HAL ADC / I2C / UART timeouts.
  *
  * ## How
  * Plain TIM7 registers (CMSIS). The HAL TIM module is not part of this
  * project, and a basic timer needs PSC / ARR / DIER / CR1 only.
  *
  * TIM7 runs in two modes, so the tick is fine when it has to be and coarse
  * when nobody is looking:
  *
  *   fine   - 1 MHz  / ARR 1000  -> update every 1 ms, uwTick += 1.
  *            Active whenever the loop is doing work, so HAL_Delay and the
  *            HAL I2C / ADC / UART timeouts keep their millisecond meaning.
  *   coarse - 10 kHz / ARR n*10  -> one-shot update after n ms, uwTick += n.
  *            Active only inside HAL_TickSleep() while the loop idles, so a
  *            100 ms wait costs one wake-up instead of a hundred.
  *
  * uwTick is credited with the time actually spent asleep (the full period, or
  * CNT/10 when SW1 cuts the sleep short), so HAL_GetTick keeps counting real
  * milliseconds across both modes and no deadline in main.c drifts.
  *
  * ## Notes
  * - SysTick stays disabled; stm32l4xx_it.c no longer increments the tick.
  * - HAL re-calls HAL_InitTick() after SystemClock_Config(), so the prescalers
  *   are recomputed for the final PLL clock.
  * - Coarse ARR is 16 bit at 10 kHz, hence the TICK_SLEEP_MAX_MS ceiling.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

#define TICK_SLEEP_MAX_MS   1000U   /* ARR = ms * 10 - 1 must stay 16 bit */

static uint32_t psc_fine   = 0U;    /* 1 MHz:  1 us per count */
static uint32_t psc_coarse = 0U;    /* 10 kHz: 100 us per count */

/* 1 while the fine tick runs, else the length of the coarse one-shot in ms.
   The handler resets it to 1 once that one-shot has been credited. */
static volatile uint32_t tick_step_ms = 1U;

static void tick_mode_fine(void)
{
  TIM7->PSC = psc_fine;
  TIM7->ARR = 1000U - 1U;
  TIM7->EGR = TIM_EGR_UG;   /* load PSC / ARR now; URS keeps UIF clear */
  TIM7->SR  = 0U;
  TIM7->CR1 |= TIM_CR1_CEN;
}

/**
  * @brief  Start TIM7 as the 1 ms HAL time base.
  * @param  TickPriority  Tick interrupt priority.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  RCC_ClkInitTypeDef clkconfig = {0};
  uint32_t timclock;
  uint32_t flatency;

  if (TickPriority >= (1UL << __NVIC_PRIO_BITS))
  {
    return HAL_ERROR;
  }

  /* No HAL_SYSTICK_Config here: SysTick must never wake the CPU from __WFI. */
  SysTick->CTRL &= ~(SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);

  __HAL_RCC_TIM7_CLK_ENABLE();

  HAL_RCC_GetClockConfig(&clkconfig, &flatency);

  /* An APB1 prescaler other than 1 doubles the timer clock (see RM0394). */
  timclock = HAL_RCC_GetPCLK1Freq();
  if (clkconfig.APB1CLKDivider != RCC_HCLK_DIV1)
  {
    timclock *= 2U;
  }

  psc_fine   = (timclock / 1000000U) - 1U;
  psc_coarse = (timclock / 10000U) - 1U;

  TIM7->CR1 = 0U;             /* stop before re-configuring */
  TIM7->CR1 |= TIM_CR1_URS;   /* only overflow raises an update */
  TIM7->DIER |= TIM_DIER_UIE;
  tick_step_ms = 1U;
  tick_mode_fine();

  HAL_NVIC_SetPriority(TIM7_IRQn, TickPriority, 0U);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);
  uwTickPrio = TickPriority;

  return HAL_OK;
}

/* Used by ST_SLEEP: no 1 ms wake-up until SW1 hits EXTI3. */
void HAL_SuspendTick(void)
{
  TIM7->DIER &= ~TIM_DIER_UIE;
}

void HAL_ResumeTick(void)
{
  TIM7->SR = ~TIM_SR_UIF;   /* drop overflows collected while suspended */
  TIM7->DIER |= TIM_DIER_UIE;
}

/**
  * @brief  Idle until `ms` have passed or an interrupt (SW1 on EXTI3) arrives.
  * @param  ms  Sleep length, clamped to TICK_SLEEP_MAX_MS.
  *
  * Replaces the 1 ms __WFI() spin at the bottom of the main loop: TIM7 becomes
  * a single coarse one-shot for the whole wait, so the CPU wakes once. Returns
  * as soon as anything fires, and uwTick is advanced by the time really slept.
  */
void HAL_TickSleep(uint32_t ms)
{
  uint32_t primask;

  if (ms == 0U)
  {
    __WFI();
    return;
  }
  if (ms > TICK_SLEEP_MAX_MS)
  {
    ms = TICK_SLEEP_MAX_MS;
  }

  TIM7->CR1 &= ~TIM_CR1_CEN;
  TIM7->PSC = psc_coarse;
  TIM7->ARR = (ms * 10U) - 1U;
  TIM7->EGR = TIM_EGR_UG;
  TIM7->SR = 0U;
  tick_step_ms = ms;
  TIM7->CR1 |= TIM_CR1_OPM | TIM_CR1_CEN;   /* one-shot: stops itself on overflow */

  __WFI();

  /* Accounting and the mode switch must not race the TIM7 handler, or a wake
     and an overflow could both be credited. */
  primask = __get_PRIMASK();
  __disable_irq();
  TIM7->CR1 &= ~(TIM_CR1_CEN | TIM_CR1_OPM);
  if (tick_step_ms != 1U)
  {
    /* Woken early, so the handler has not credited this period yet. */
    uwTick += ((TIM7->SR & TIM_SR_UIF) != 0U) ? tick_step_ms : (TIM7->CNT / 10U);
    tick_step_ms = 1U;
  }
  tick_mode_fine();
  __set_PRIMASK(primask);
}

/* TIM7 overflow feeds uwTick: every 1 ms when fine, once per sleep when coarse. */
void TIM7_TickHandler(void)
{
  if ((TIM7->SR & TIM_SR_UIF) != 0U)
  {
    TIM7->SR = ~TIM_SR_UIF;

    if (tick_step_ms == 1U)
    {
      HAL_IncTick();
    }
    else
    {
      uwTick += tick_step_ms;
      tick_step_ms = 1U;   /* marks the coarse sleep as already credited */
    }
  }
}
