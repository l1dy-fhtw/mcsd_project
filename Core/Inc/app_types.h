#ifndef APP_TYPES_H
#define APP_TYPES_H

#include "main.h"

/**
 * Shared application types and limits.
 *
 * CM_MAX is the display / Hold / lab-key ceiling, not a VL53L1X hardware limit.
 * It used to be 200, which made Hold freeze 200 cm whenever Live was farther
 * (e.g. 450 cm). 500 cm covers typical long-range ToF with headroom; the LCD
 * format already allows up to 999 cm.
 */
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
  MODE_LCD_DEBUG        /* 67% - 100%: UART text on the LCD */
} PotMode_t;

#define SW1_PIN        GPIO_PIN_3
#define SW1_PORT       GPIOA
#define TIMEOUT_MS     30000U
#define CM_START       50
#define CM_MIN         1
#define CM_MAX         500
#define CM_STEP        1
#define CM_BLINK       40
#define CM_PANIC       10
#define LCD_COLS       16U
#define TOF_POLL_MS    500U
#define UART_PRINT_MS  100U
#define LOOP_FAST_MS   10U
#define LOOP_IDLE_MS   100U

#endif /* APP_TYPES_H */
