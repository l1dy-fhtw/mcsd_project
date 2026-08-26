#ifndef LCD_H
#define LCD_H

#include "main.h"

/**
 * @file lcd.h
 * @brief Public API for a 16x2 HD44780 LCD on a PCF8574 I2C backpack.
 *
 * Wiring: SDA = PB7 (D4), SCL = PB6 (D5), I2C1 shared with the VL53L1X.
 * Backpack ACK at 7-bit 0x27 or 0x3F (HAL uses the 8-bit address << 1).
 * CubeMX: I2C1 Fast Mode, PB6/PB7 open-drain with GPIO_PULLUP.
 */

/**
 * @brief Probe the backpack and run the HD44780 4-bit boot sequence.
 * @param hi2c  I2C1 handle already initialised by MX_I2C1_Init().
 * @retval None  Failures are silent; later writes time out inside HAL.
 *
 * HAL: HAL_I2C_IsDeviceReady (0x27 then 0x3F), then HAL_I2C_Master_Transmit
 * for each nibble. Uses HAL_Delay only for datasheet boot waits.
 */
void LCD_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief Display off, backlight pin 0. Used for the 30 s idle sleep.
 * @retval None
 */
void LCD_Sleep(void);

/**
 * @brief Backlight on, display on, cursor off. Used on SW1 wake.
 * @retval None
 */
void LCD_Wake(void);

/**
 * @brief Write exactly 16 characters to one row (pads/truncates with spaces).
 * @param row  0 = top line (DDRAM 0x80), 1 = bottom line (DDRAM 0xC0).
 * @param s    NUL-terminated string; NULL is treated as empty.
 * @retval None
 */
void LCD_WriteLine(uint8_t row, const char *s);

/**
 * @brief Presentation line 0: millimetres in, "Dist: xxx cm" out.
 * @param dist  Range in millimetres (clamped to 0..999 cm on the LCD).
 * @retval None
 */
void Update_LCD_Standard(uint16_t dist);

/**
 * @brief Two-line debug layout (kept as Dani's published API; unused by main).
 * @param pot   Potentiometer percent 0..100.
 * @param dist  Range in millimetres (clamped to 9999 on the LCD).
 * @retval None
 */
void Update_LCD_Debug(uint8_t pot, uint16_t dist);

#endif /* LCD_H */
