#ifndef DISTANCE_APP_H
#define DISTANCE_APP_H

#include "app_types.h"

uint8_t  tof_try_attach(void);                 /* ACK + init + start; 1 if online */
void     tof_service_live(app_state_t state, PotMode_t mode);
void     tof_stop(void);                       /* stop ranging (idle sleep) */
uint8_t  tof_is_online(void);
int      distance_cm_get(void);
uint16_t distance_mm_get(void);
void     distance_key(uint8_t key);            /* lab '1' / '2' while ToF is off */

#endif /* DISTANCE_APP_H */
