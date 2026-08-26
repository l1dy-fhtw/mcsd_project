#ifndef LCD_H
#define LCD_H

#include "main.h"

/**
 * @file lcd.h
 * @brief Public API for a 16x2 HD44780 LCD on a PCF8574 I2C backpack.
 *
 * Hardware: HD44780 in 4-bit mode, reached only through the backpack GPIO
 * (no MCU RS/EN/D4–D7 pins). Funduino map: P0=RS, P1=RW (held 0), P2=EN,
 * P3=backlight, P4–P7=data nibble.
 *
 * Wiring: SDA = PB7 (D4), SCL = PB6 (D5), I2C1 shared with the VL53L1X.
 * Probe order is 7-bit 0x27 then 0x3F (HAL uses the 8-bit address << 1).
 * CubeMX: I2C1 Fast Mode, PB6/PB7 open-drain with GPIO_PULLUP. Call
 * MX_I2C1_Init() before LCD_Init() so the handle and pins exist.
 *
 * I2C DMA is a poor fit: each nibble is three 1-byte expander writes with a
 * timed EN edge, and the ToF sits on the same bus.
 */

/**
 * @brief Probe the backpack and run the HD44780 4-bit boot sequence.
 * @param hi2c  I2C1 handle already initialised by MX_I2C1_Init().
 * @retval None  Failures are silent; later writes time out inside HAL.
 *
 * Probe 0x27 then 0x3F (first ACK wins). Boot nibbles 0x30 x3 then 0x20,
 * then commands 0x28 / 0x08 / 0x01 / 0x06 / 0x0C. No ACK does not abort:
 * HAL_I2C_Master_Transmit later just times out. HAL_Delay is used only for
 * datasheet boot / clear waits.
 */
void LCD_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief Display off, backlight pin 0. Used for the 30 s idle sleep.
 * @retval None
 *
 * DDRAM is not cleared. After LCD_Wake() the application must redraw
 * (app_paint / SW1 restore), because this driver does not store the last text.
 */
void LCD_Sleep(void);

/**
 * @brief Backlight on, display on, cursor off. Used on SW1 wake.
 * @retval None
 *
 * Turns the panel back on; it does not rewrite the two lines.
 */
void LCD_Wake(void);

/**
 * @brief Write exactly 16 characters to one row (pads/truncates with spaces).
 * @param row  0 = top line (DDRAM 0x80), 1 = bottom line (DDRAM 0xC0).
 * @param s    NUL-terminated string; NULL is treated as empty.
 * @retval None
 *
 * Always 16 glyphs so a shorter string cannot leave leftover characters
 * from the previous line.
 */
void LCD_WriteLine(uint8_t row, const char *s);

/**
 * @brief Presentation line 0: millimetres in, "Dist: xxx cm" out.
 * @param dist  Range in millimetres (clamped to 0..999 cm on the LCD).
 * @retval None
 *
 * The LCD format %3d can show 0..999. A Hold of 200 cm at ~4.5 m was the
 * application CM_MAX ceiling, not this format.
 */
void Update_LCD_Standard(uint16_t dist);

/**
 * @brief Two-line debug layout (kept as Dani's published API; unused by main).
 * @param pot   Potentiometer percent 0..100.
 * @param dist  Range in millimetres (clamped to 9999 on the LCD).
 * @retval None
 *
 * Current main / app_paint never call this. LCD DEBUG mode writes UART text
 * with LCD_WriteLine instead.
 */
void Update_LCD_Debug(uint8_t pot, uint16_t dist);

#endif /* LCD_H */
