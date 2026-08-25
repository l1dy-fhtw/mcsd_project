/**
 ******************************************************************************
 * @file    lcd.c
 * @brief   HD44780 16x2 via PCF8574 I2C backpack (4-bit mode)
 ******************************************************************************
 * Student: Akos Eross 84655
 *
 * Funduino backpack map: P0=RS, P1=RW(0), P2=EN, P3=BL, P4..P7=data.
 * EN pulse matches LiquidCrystal_I2C: data (EN=0) -> EN high -> EN low.
 ******************************************************************************
 */
#include "lcd.h"
#include <stdio.h>

#define LCD_ADDR_27    (0x27U << 1)
#define LCD_ADDR_3F    (0x3FU << 1)
#define LCD_RS         0x01U
#define LCD_EN         0x04U
#define LCD_BL         0x08U
#define LCD_I2C_TO     100U

static I2C_HandleTypeDef *lcd_hi2c = 0;
static uint8_t lcd_addr = LCD_ADDR_27;
static uint8_t lcd_bl = LCD_BL;

/* ~1 us busy-wait @ 32 MHz (SYSCLK); enough for EN pulse / settle. */
static void lcd_delay_us(uint32_t us)
{
  uint32_t cycles = us * (SystemCoreClock / 1000000U);
  while (cycles > 0U)
  {
    __NOP();
    cycles--;
  }
}

static HAL_StatusTypeDef lcd_i2c(uint8_t data)
{
  if (lcd_hi2c == 0)
  {
    return HAL_ERROR;
  }
  return HAL_I2C_Master_Transmit(lcd_hi2c, lcd_addr, &data, 1, LCD_I2C_TO);
}

/* LiquidCrystal_I2C-compatible nibble: set data, pulse EN high then low. */
static void lcd_nibble(uint8_t nibble, uint8_t rs)
{
  uint8_t data = (uint8_t)((nibble & 0xF0U) | lcd_bl | rs);

  (void)lcd_i2c(data);                 /* EN=0, data valid */
  lcd_delay_us(1);
  (void)lcd_i2c((uint8_t)(data | LCD_EN));
  lcd_delay_us(1);                     /* EN high > 450 ns */
  (void)lcd_i2c(data);                 /* EN falling edge latches */
  lcd_delay_us(50);                    /* HD44780 needs >37 us */
}

static void lcd_byte(uint8_t value, uint8_t rs)
{
  lcd_nibble(value, rs);
  lcd_nibble((uint8_t)(value << 4), rs);
}

static void lcd_cmd(uint8_t cmd)
{
  lcd_byte(cmd, 0);
  if ((cmd == 0x01U) || (cmd == 0x02U))
  {
    HAL_Delay(2);
  }
}

static void lcd_data(uint8_t value)
{
  lcd_byte(value, LCD_RS);
}

static void lcd_gotoxy(uint8_t col, uint8_t row)
{
  uint8_t ddram = (row == 0U) ? 0x80U : 0xC0U;
  lcd_cmd((uint8_t)(ddram + col));
}

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

  /* HD44780 4-bit boot sequence (datasheet). */
  lcd_nibble(0x30, 0);
  HAL_Delay(5);
  lcd_nibble(0x30, 0);
  HAL_Delay(1);
  lcd_nibble(0x30, 0);
  HAL_Delay(1);
  lcd_nibble(0x20, 0);
  HAL_Delay(1);

  lcd_cmd(0x28); /* 2 lines, 5x8 */
  lcd_cmd(0x08); /* display off */
  lcd_cmd(0x01); /* clear */
  lcd_cmd(0x06); /* entry mode */
  lcd_cmd(0x0C); /* display on */
}

void LCD_WriteLine(uint8_t row, const char *s)
{
  uint8_t i;

  lcd_gotoxy(0, row);
  for (i = 0; i < 16U; i++)
  {
    if ((s != 0) && (*s != '\0'))
    {
      lcd_data((uint8_t)*s);
      s++;
    }
    else
    {
      lcd_data((uint8_t)' ');
    }
  }
}

void LCD_Sleep(void)
{
  lcd_cmd(0x08);
  lcd_bl = 0;
  (void)lcd_i2c(0);
}

void LCD_Wake(void)
{
  lcd_bl = LCD_BL;
  lcd_cmd(0x0C);
}

void Update_LCD_Standard(uint16_t dist)
{
  char line[17];
  int cm = (int)(dist / 10U);

  if (cm < 0)
  {
    cm = 0;
  }
  if (cm > 999)
  {
    cm = 999;
  }
  (void)sprintf(line, "Dist: %3d cm", cm);
  LCD_WriteLine(0, line);
}

void Update_LCD_Debug(uint8_t pot, uint16_t dist)
{
  char l1[17];
  char l2[17];
  uint16_t mm = (dist > 9999U) ? 9999U : dist;

  (void)sprintf(l1, "DBG P:%3u%%", (unsigned)pot);
  (void)sprintf(l2, "D:%4u mm", (unsigned)mm);
  LCD_WriteLine(0, l1);
  LCD_WriteLine(1, l2);
}
