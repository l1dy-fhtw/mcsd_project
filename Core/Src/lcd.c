/**
 ******************************************************************************
 * @file    lcd.c
 * @brief   HD44780 16x2 via PCF8574 I2C backpack (4-bit mode)
 ******************************************************************************
 * Student: Akos Eross 84655
 *
 * HOW: one I2C byte sets the expander pins (P4..P7 = data nibble, P0=RS,
 *      P2=EN, P3=backlight). WHY: backpack has no register map — only GPIO.
 * Nibble mode is required: only 4 data pins are wired on the Funduino board.
 ******************************************************************************
 */
#include "lcd.h"
#include <stdio.h>  /* sprintf for Update_LCD_* */

/* 7-bit backpack address; HAL needs << 1 (R/W bit space) — same as Thermo 11 lab */
#define LCD_ADDR_27    (0x27 << 1)
#define LCD_ADDR_3F    (0x3F << 1)
#define LCD_RS         0x01         /* PCF8574 P0: 0=command, 1=data */
#define LCD_EN         0x04         /* PCF8574 P2: HD44780 enable strobe */
#define LCD_BL         0x08         /* PCF8574 P3 backlight */
#define LCD_I2C_TO     20           /* ms, one expander byte */

static I2C_HandleTypeDef *lcd_hi2c = 0;
static uint8_t lcd_addr = LCD_ADDR_27;  /* set in LCD_Init() from ACK */
static uint8_t lcd_bl = LCD_BL;

/* HOW: one byte to the PCF8574 expander. WHY: backpack has no register map. */
static void lcd_i2c(uint8_t data)
{
  if (lcd_hi2c == 0)
  {
    return;
  }
  HAL_I2C_Master_Transmit(lcd_hi2c, lcd_addr, &data, 1, LCD_I2C_TO);
}

/* HOW: P7..P4 = nibble, then EN high then low. WHY: HD44780 latches on EN fall. */
static void lcd_nibble(uint8_t nibble, uint8_t rs)
{
  uint8_t data = (uint8_t)((nibble & 0xF0) | lcd_bl | rs);
  lcd_i2c((uint8_t)(data | LCD_EN));
  lcd_i2c(data);
}

/* 4-bit LCD: high nibble first, then low nibble shifted into P7..P4. */
static void lcd_byte(uint8_t value, uint8_t rs)
{
  lcd_nibble(value, rs);
  lcd_nibble((uint8_t)(value << 4), rs);
}

static void lcd_cmd(uint8_t cmd)
{
  lcd_byte(cmd, 0);          /* RS=0: instruction (clear, DDRAM addr, ...) */
}

static void lcd_data(uint8_t value)
{
  lcd_byte(value, LCD_RS);   /* RS=1: character to display */
}

static void lcd_gotoxy(uint8_t col, uint8_t row)
{
  uint8_t ddram = (row == 0U) ? 0x80U : 0xC0U;  /* HD44780 line 0 / line 1 */
  lcd_cmd((uint8_t)(ddram + col));
}

/* Probe 0x27 then 0x3F. HAL_Delay only here (HD44780 boot timing), not in the loop. */
void LCD_Init(I2C_HandleTypeDef *hi2c)
{
  lcd_hi2c = hi2c;

  if (HAL_I2C_IsDeviceReady(lcd_hi2c, LCD_ADDR_27, 3, 100) == HAL_OK)
  {
    lcd_addr = LCD_ADDR_27;
  }
  else if (HAL_I2C_IsDeviceReady(lcd_hi2c, LCD_ADDR_3F, 3, 100) == HAL_OK)
  {
    lcd_addr = LCD_ADDR_3F;
  }

  lcd_bl = LCD_BL;
  HAL_Delay(50);
  lcd_nibble(0x30, 0);
  HAL_Delay(5);
  lcd_nibble(0x30, 0);
  HAL_Delay(1);
  lcd_nibble(0x30, 0);
  lcd_nibble(0x20, 0);   /* switch controller to 4-bit */
  lcd_cmd(0x28);         /* 2 lines, 5x8 */
  lcd_cmd(0x08);         /* display off */
  lcd_cmd(0x01);         /* clear */
  HAL_Delay(2);
  lcd_cmd(0x06);         /* cursor increment */
  lcd_cmd(0x0C);         /* display on, cursor off */
}

/* Write exactly 16 characters so the previous text is fully replaced. */
void LCD_WriteLine(uint8_t row, const char *s)
{
  uint8_t i;

  lcd_gotoxy(0, row);
  for (i = 0; i < 16U; i++)
  {
    if (*s != '\0')
    {
      lcd_data((uint8_t)*s);
      s++;
    }
    else
    {
      lcd_data(' ');
    }
  }
}

/* Display off + backlight pin 0. WHY: 30 s idle (not used on the wait screen). */
void LCD_Sleep(void)
{
  lcd_cmd(0x08);
  lcd_bl = 0;
  lcd_i2c(0);
}

void LCD_Wake(void)
{
  lcd_bl = LCD_BL;
  lcd_cmd(0x0C);             /* display on, cursor off */
}

/* Dani API (mm in). main uses app_draw(); these are for a later merge. */
void Update_LCD_Standard(uint16_t dist)
{
  char line[17];
  int cm = (int)(dist / 10U);

  (void)sprintf(line, "Dist: %3d cm", cm);
  LCD_WriteLine(0, line);
}

/* Pot % + raw mm — same Dani API, debug layout. */
void Update_LCD_Debug(uint8_t pot, uint16_t dist)
{
  char l1[17];
  char l2[17];

  (void)sprintf(l1, "DBG P:%3u%%", (unsigned)pot);
  (void)sprintf(l2, "D:%4u mm", (unsigned)dist);
  LCD_WriteLine(0, l1);
  LCD_WriteLine(1, l2);
}
