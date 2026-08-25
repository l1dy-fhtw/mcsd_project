#ifndef LCD_H
#define LCD_H

#include "main.h"

/**
 * @file lcd.h
 * @brief Public API for 16x2 HD44780 LCD via PCF8574 I2C backpack.
 *
 * HOW: one module (lcd.h + lcd.c), same pattern as vl53l1x_driver.
 * WHY: Dani #include "lcd.h" and calls Update_LCD_*; main keeps the state machine.
 *
 * Wiring: SDA = PB7 (D4), SCL = PB6 (D5), same I2C1 as VL53L1X.
 * Backpack ACK at 7-bit 0x27 or 0x3F (HAL uses << 1).
 */

/* Probe backpack, 4-bit HD44780 init. HAL_Delay only here (boot timing). */
void LCD_Init(I2C_HandleTypeDef *hi2c);

/* Display off + backlight pin 0. Used for 30 s idle sleep. */
void LCD_Sleep(void);

/* Backlight on, display on, cursor off. */
void LCD_Wake(void);

/* Write exactly 16 characters so the previous text is fully replaced. */
void LCD_WriteLine(uint8_t row, const char *s);

/* Dani API: millimetres in → "Dist: xxx cm" on line 0. */
void Update_LCD_Standard(uint16_t dist);

/* Dani API: pot percent + raw mm (debug layout, two lines). */
void Update_LCD_Debug(uint8_t pot, uint16_t dist);

#endif /* LCD_H */
