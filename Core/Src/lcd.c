/**
 ******************************************************************************
 * @file    lcd.c
 * @brief   HD44780 16x2 via PCF8574 I2C backpack (4-bit mode)
 ******************************************************************************
 * Student: Akos Eross 84655
 *
 * Funduino backpack map: P0=RS, P1=RW(held 0), P2=EN, P3=BL, P4..P7=data.
 * Probe 7-bit 0x27 then 0x3F; HAL stores them as 8-bit (addr << 1).
 *
 * EN pulse matches LiquidCrystal_I2C: data (EN=0) -> EN high -> EN low.
 * Each nibble is three I2C bytes. I2C DMA would not help: the EN edge is a
 * timed GPIO pattern on the expander, and VL53L1X shares I2C1.
 * HAL_Delay is used only in Init / clear; EN uses a ~us busy-wait.
 ******************************************************************************
 */
#include "lcd.h"
#include <stdio.h>

#define LCD_ADDR_27    (0x27U << 1)   /* HAL 8-bit write address 0x4E */
#define LCD_ADDR_3F    (0x3FU << 1)   /* HAL 8-bit write address 0x7E */
#define LCD_RS         0x01U          /* P0: 0 = command, 1 = character */
#define LCD_EN         0x04U          /* P2: strobe; HD44780 latches on fall */
#define LCD_BL         0x08U          /* P3: backlight (P1/RW is never set) */
#define LCD_I2C_TO     100U

static I2C_HandleTypeDef *lcd_hi2c = 0;
static uint8_t lcd_addr = LCD_ADDR_27;
static uint8_t lcd_bl = LCD_BL;

/* Busy-wait of a few microseconds at SYSCLK. HAL_Delay(1) is a whole
 * millisecond and would stretch every EN pulse; keep HAL_Delay for init /
 * clear only. */
static void lcd_delay_us(uint32_t us)
{
  uint32_t cycles = us * (SystemCoreClock / 1000000U);
  while (cycles > 0U)
  {
    __NOP();
    cycles--;
  }
}

/* One expander byte (blocking). DMA is unused: each nibble is three 1-byte
 * writes with a gap between them, and the ToF uses the same I2C1. */
static HAL_StatusTypeDef lcd_i2c(uint8_t data)
{
  if (lcd_hi2c == 0)
  {
    return HAL_ERROR;
  }
  return HAL_I2C_Master_Transmit(lcd_hi2c, lcd_addr, &data, 1, LCD_I2C_TO);
}

/* lcd_nibble — send 4 bits to HD44780 via PCF8574 (EN latch workflow).
 *
 * 1) Build expander byte: P7–P4 = (nibble & 0xF0), P3 = backlight, P0 = rs
 *    (0 = command, LCD_RS = character). P1/RW stays 0.
 * 2) I2C write data with EN=0  — pins set, not latched yet
 * 3) I2C write data|LCD_EN    — EN high
 * 4) I2C write data again     — EN falls; HD44780 latches the nibble here
 * 5) ~50 us settle (>37 us datasheet)
 *
 * Why 3 writes: backpack is I2C GPIO; we must pulse EN ourselves.
 * Why us busy-wait: HAL_Delay(1) is too coarse for EN. */
static void lcd_nibble(uint8_t nibble, uint8_t rs)
{
  uint8_t data = (uint8_t)((nibble & 0xF0U) | lcd_bl | rs);

  (void)lcd_i2c(data);                         /* EN=0: present nibble */
  lcd_delay_us(1);
  (void)lcd_i2c((uint8_t)(data | LCD_EN));     /* EN=1 */
  lcd_delay_us(1);
  (void)lcd_i2c(data);                         /* EN=0: latch */
  lcd_delay_us(50);                            /* >37 us after nibble */
}

/* High nibble first, then low nibble shifted into P7..P4. */
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
    HAL_Delay(2);                      /* clear / home need extra time */
  }
}

static void lcd_data(uint8_t value)
{
  lcd_byte(value, LCD_RS);
}

/* lcd_gotoxy — HD44780 DDRAM: row 0 → 0x80 (top), row 1 → 0xC0 (bottom); + col. */
static void lcd_gotoxy(uint8_t col, uint8_t row)
{
  uint8_t ddram = (row == 0U) ? 0x80U : 0xC0U;
  lcd_cmd((uint8_t)(ddram + col));
}

/* hi2c must already be live (MX_I2C1_Init). Return is void: a missing
 * backpack does not abort; later transmits just NACK / time out. */
void LCD_Init(I2C_HandleTypeDef *hi2c)
{
  lcd_hi2c = hi2c;

  /* First ACK wins: 0x27 (common Funduino), then 0x3F. */
  if (HAL_I2C_IsDeviceReady(lcd_hi2c, LCD_ADDR_27, 3, 100) == HAL_OK)
  {
    lcd_addr = LCD_ADDR_27;
  }
  else if (HAL_I2C_IsDeviceReady(lcd_hi2c, LCD_ADDR_3F, 3, 100) == HAL_OK)
  {
    lcd_addr = LCD_ADDR_3F;
  }

  lcd_bl = LCD_BL;   /* Turn the backlight bit on for every later I2C byte (so the display lights up). */
  HAL_Delay(50);

  /* HD44780 4-bit boot sequence (datasheet, still in 8-bit until 0x20). */
  lcd_nibble(0x30, 0);
  HAL_Delay(5);
  lcd_nibble(0x30, 0); /* This is the datasheet reset sequence */
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

/* LCD_WriteLine(row, s) — always exactly 16 characters.
 * Why 16: visible width of one HD44780 line; pad with spaces / truncate
 * so the previous text is fully overwritten (no leftover glyphs).
 * Row: lcd_gotoxy(0, row) → DDRAM 0x80 or 0xC0, then 16× lcd_data. */
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
      lcd_data((uint8_t)' ');   /* pad rest of row */
    }
  }
}

/* Display off + backlight 0. DDRAM is kept; app_paint redraws after wake. */
void LCD_Sleep(void)
{
  lcd_cmd(0x08);
  lcd_bl = 0;
  (void)lcd_i2c(0);
}

/* Display on, cursor off, backlight on. Does not rewrite the two lines. */
void LCD_Wake(void)
{
  lcd_bl = LCD_BL;
  lcd_cmd(0x0C);
}

/* dist is millimetres; line 0 shows centimetres. %3d allows 999 cm; the
 * 200 cm Hold trap was CM_MAX in the application, not this format. */
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

/* Published API; current main never calls this (LCD DEBUG uses LCD_WriteLine). */
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
