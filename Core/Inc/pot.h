#ifndef POT_H
#define POT_H

#include "app_types.h"

/**
 * One ADC conversion on PA7 / ADC1_IN12.
 * @param raw      12-bit ADC count (0..4095), or NULL
 * @param percent  0..100, or NULL
 * @param mode     pot debug band, or NULL
 */
void pot_sample(uint16_t *raw, uint8_t *percent, PotMode_t *mode);

#endif /* POT_H */
