#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>

/* Adresse I2C du backpack PCF8574 (souvent 0x27, parfois 0x3F) */
#ifndef LCD_PCF8574_ADDR
#define LCD_PCF8574_ADDR 0x27
#endif

/* Mapping “classique” des pins PCF8574 → HD44780 (backpack le plus répandu)
 * P0=RS, P1=RW, P2=E, P3=BL, P4=D4, P5=D5, P6=D6, P7=D7
 */
#define LCD_RS_BIT  (1u << 0)
#define LCD_RW_BIT  (1u << 1)
#define LCD_E_BIT   (1u << 2)
#define LCD_BL_BIT  (1u << 3)
#define LCD_D4_BIT  (1u << 4)
#define LCD_D5_BIT  (1u << 5)
#define LCD_D6_BIT  (1u << 6)
#define LCD_D7_BIT  (1u << 7)

/* Commandes (reprend tes defines d’origine) */
#define LCD_CLEAR                 0x01
#define LCD_HOME                  0x02
#define LCD_ENTRY_MODE            0x04
#define LCD_DISPLAY_ONOFF         0x08
#define LCD_DISPLAY_CURSOR_SHIFT  0x10
#define LCD_FUNCTION_SET          0x20
#define LCD_CGRAM_SET             0x40
#define LCD_DDRAM_SET             0x80

/* Entry mode params */
#define LCD_ENTRY_SHIFT_CURSOR    0
#define LCD_ENTRY_SHIFT_DISPLAY   1
#define LCD_ENTRY_MOVE_LEFT       0
#define LCD_ENTRY_MOVE_RIGHT      2

/* Display ON/OFF params */
#define LCD_DISP_DISPLAY_OFF      0
#define LCD_DISP_DISPLAY_ON       4
#define LCD_DISP_CURSOR_OFF       0
#define LCD_DISP_CURSOR_ON        2
#define LCD_DISP_CURSOR_NOBLINK   0
#define LCD_DISP_CURSOR_BLINK     1

/* Shift params */
#define LCD_SHIFT_CURSOR          0
#define LCD_SHIFT_DISPLAY         8
#define LCD_SHIFT_LEFT            0
#define LCD_SHIFT_RIGHT           4

/* Function set params */
#define LCD_FUNCTION_FONT5x7      0
#define LCD_FUNCTION_FONT5x10     4
#define LCD_FUNCTION_ONE_LINE     0
#define LCD_FUNCTION_TWO_LINE     8
#define LCD_FUNCTION_4_BIT        0
#define LCD_FUNCTION_8_BIT        16

#ifdef __cplusplus
extern "C" {
#endif

/* API publique (mêmes noms qu’avant) */
int  LCD_Initalize(const struct device *i2c_dev);
void LCD_Clear(void);
void LCD_Home(void);
void LCD_GoTo(uint8_t line, uint8_t column);
void LCD_WriteText(const char *msg);
void LCD_WriteData(uint8_t data);

/* Optionnel : backlight ON/OFF */
void LCD_Backlight(bool on);

#ifdef __cplusplus
}
#endif

#endif

