/**
 * @file    uart_console.c
 * @brief   USART2 RX ring + LCD DEBUG line editor + VCP printf glue
 *
 * USART2 has no RX FIFO. At 115200 baud a byte must be taken within ~87 us,
 * which is far shorter than HAL_TickSleep() (up to 100 ms). Reception is
 * interrupt-driven; the ISR also wakes __WFI so a keystroke is not delayed.
 */
#include "uart_console.h"
#include "distance_app.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

#define RX_RING_SIZE  64U   /* power of two: wrap is a mask */

static uint8_t rx_byte = 0;
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;

static char uart_line[LCD_COLS + 1] = "";
static uint8_t uart_line_len = 0;
static char uart_msg[LCD_COLS + 1] = "";

int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

static void uart_rx_arm(void)
{
  (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

void uart_console_start(void)
{
  uart_rx_arm();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint32_t next;

  if (huart->Instance != USART2)
  {
    return;
  }

  next = (rx_head + 1U) & (RX_RING_SIZE - 1U);
  if (next != rx_tail)
  {
    rx_ring[rx_head] = rx_byte;
    rx_head = next;
  }
  uart_rx_arm();
}

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
  uart_rx_arm();
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

void uart_console_reset_line(void)
{
  uart_line[0] = '\0';
  uart_line_len = 0;
}

const char *uart_console_msg(void)
{
  return uart_msg;
}

/* 1 = Enter committed uart_msg (LCD should be written). Echo is VCP-only. */
static uint8_t lcd_uart_key(uint8_t c)
{
  if ((c == '\r') || (c == '\n'))
  {
    (void)strncpy(uart_msg, uart_line, sizeof(uart_msg));
    uart_msg[sizeof(uart_msg) - 1U] = '\0';
    uart_console_reset_line();
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
  return 0;
}

void uart_console_poll(app_state_t state, PotMode_t mode, uint32_t *t_idle)
{
  uint8_t key;

  while (uart_rx_pop(&key) != 0U)
  {
    if ((state != ST_SLEEP) && (t_idle != 0))
    {
      *t_idle = HAL_GetTick();
    }

    if (mode == MODE_LCD_DEBUG)
    {
      if ((lcd_uart_key(key) != 0U) && (state != ST_SLEEP))
      {
        LCD_WriteLine(1, uart_msg);
      }
    }
    else if ((tof_is_online() == 0U) && ((state == ST_LIVE) || (state == ST_READY)))
    {
      distance_key(key);
    }
  }
}
