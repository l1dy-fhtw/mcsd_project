#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include "app_types.h"

void        uart_console_start(void);          /* arm USART2 RX interrupt */
void        uart_console_poll(app_state_t state, PotMode_t mode, uint32_t *t_idle);
void        uart_console_reset_line(void);     /* drop a half-typed LCD DEBUG line */
const char *uart_console_msg(void);            /* last line committed with Enter */

#endif /* UART_CONSOLE_H */
