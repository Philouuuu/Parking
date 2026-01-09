#include "lcd_i2c.h"

/* --- Défauts si ton header actuel ne les a pas encore --- */
#ifndef LCD_COLS
#define LCD_COLS 16
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif
#ifndef LCD_LINE1_ADDR
#define LCD_LINE1_ADDR 0x00
#endif
#ifndef LCD_LINE2_ADDR
#define LCD_LINE2_ADDR 0x40
#endif
#ifndef LCD_LINE3_ADDR
#define LCD_LINE3_ADDR 0x14   /* 20x4 */
#endif
#ifndef LCD_LINE4_ADDR
#define LCD_LINE4_ADDR 0x54   /* 20x4 */
#endif

/* ====== Interne ====== */
static const struct device *s_i2c = NULL;
static uint8_t s_bl_mask = 0;     /* masque backlight selon ACTIVE_LOW */

static inline int pcf_write(uint8_t v)
{
    return i2c_write(s_i2c, &v, 1, LCD_PCF8574_ADDR);
}

static inline void pulse_enable(uint8_t bus)
{
    pcf_write(bus | LCD_E_BIT);
    k_busy_wait(1);                /* >450 ns */
    pcf_write(bus & ~LCD_E_BIT);
    k_busy_wait(50);               /* ~37 µs min */
}

static void send_nibble(uint8_t nibble, bool rs)
{
    uint8_t bus = s_bl_mask;
    if (rs) bus |= LCD_RS_BIT;     /* data/commande */

    if (nibble & 0x01) bus |= LCD_D4_BIT;
    if (nibble & 0x02) bus |= LCD_D5_BIT;
    if (nibble & 0x04) bus |= LCD_D6_BIT;
    if (nibble & 0x08) bus |= LCD_D7_BIT;

    pcf_write(bus);
    pulse_enable(bus);
}

static void send_byte(uint8_t byte, bool rs)
{
    send_nibble((byte >> 4) & 0x0F, rs);
    send_nibble(byte & 0x0F, rs);
}

static void cmd(uint8_t c)
{
    send_byte(c, false);
    if (c == LCD_CLEAR || c == LCD_HOME) {
        k_msleep(3);               /* >1.53 ms */
    } else {
        k_busy_wait(50);
    }
}

static uint8_t ddram_addr_from_xy(uint8_t line, uint8_t col)
{
    uint8_t addr;
#if (LCD_ROWS == 4)
    switch (line) {
    default:
    case 1: addr = LCD_LINE1_ADDR; break;
    case 2: addr = LCD_LINE2_ADDR; break;
    case 3: addr = LCD_LINE3_ADDR; break;
    case 4: addr = LCD_LINE4_ADDR; break;
    }
#else
    addr = (line <= 1) ? LCD_LINE1_ADDR : LCD_LINE2_ADDR;
#endif
    if (col > 0) col--;
    if (col >= LCD_COLS) col = LCD_COLS - 1;
    return (uint8_t)(addr + col);
}

/* ====== API ====== */
void LCD_Backlight(bool on)
{
#if LCD_BACKLIGHT_ACTIVE_LOW
    s_bl_mask = on ? 0 : LCD_BL_BIT;
#else
    s_bl_mask = on ? LCD_BL_BIT : 0;
#endif
    pcf_write(s_bl_mask);
}

int LCD_Initalize(const struct device *i2c_dev)
{
    if (!i2c_dev || !device_is_ready(i2c_dev)) return -EINVAL;
    s_i2c = i2c_dev;

    LCD_Backlight(true);

    k_msleep(40);                  /* power-up wait */
    send_nibble(0x03, false); k_msleep(5);
    send_nibble(0x03, false); k_busy_wait(150);
    send_nibble(0x03, false); k_busy_wait(150);
    send_nibble(0x02, false); k_busy_wait(150); /* 4-bit */

    cmd(LCD_FUNCTION_SET | LCD_FUNCTION_TWO_LINE | LCD_FUNCTION_4_BIT | LCD_FUNCTION_FONT5x7);
    cmd(LCD_DISPLAY_ONOFF | LCD_DISP_DISPLAY_OFF | LCD_DISP_CURSOR_OFF | LCD_DISP_CURSOR_NOBLINK);
    LCD_Clear();
    cmd(LCD_ENTRY_MODE | LCD_ENTRY_MOVE_RIGHT | LCD_ENTRY_SHIFT_CURSOR);
    cmd(LCD_DISPLAY_ONOFF | LCD_DISP_DISPLAY_ON | LCD_DISP_CURSOR_OFF | LCD_DISP_CURSOR_NOBLINK);
    return 0;
}

void LCD_Clear(void)
{
    cmd(LCD_CLEAR);
    LCD_Home();
}

void LCD_Home(void)
{
    cmd(LCD_HOME);
}

void LCD_GoTo(uint8_t line, uint8_t column)
{
    if (line < 1) line = 1;
    if (line > LCD_ROWS) line = LCD_ROWS;
    uint8_t addr = ddram_addr_from_xy(line, column);
    cmd(LCD_DDRAM_SET | addr);
}

void LCD_WriteData(uint8_t data)
{
    send_byte(data, true);
}

/* Dans lcd_i2c.c */

void LCD_WriteText(const char *msg)
{
    // 1. Sécurité : Si le message est vide ou NULL, on ne fait rien
    if (msg == NULL) return;

    // 2. Sécurité : On limite la boucle pour ne pas écrire hors de l'écran
    // (Un écran standard fait 16 ou 20 caractères de large)
    int i = 0;
    while (msg[i] != '\0' && i < 20) { 
        LCD_WriteData(msg[i]);
        i++;
    }
}
