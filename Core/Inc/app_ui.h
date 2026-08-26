#ifndef APP_UI_H
#define APP_UI_H

#include "app_types.h"

uint8_t  app_sw1_poll(app_state_t *state, uint8_t *was_hold);
void     app_on_pot_mode_change(PotMode_t mode, app_state_t state,
                                uint32_t *shown_s, int *shown_cm,
                                app_state_t *shown_state);
void     app_paint(app_state_t state, PotMode_t mode, uint32_t t_idle,
                   uint32_t *shown_s, int *shown_cm, app_state_t *shown_state);
void     app_led(app_state_t state);
uint32_t app_idle_ms(app_state_t state);
void     app_wait_sleep(void);                 /* ST_SLEEP: tick off, WFI until SW1 */
void     app_wake_clear(void);

#endif /* APP_UI_H */
