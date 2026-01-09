/*
 * led_matrix.h
 * Driver pour 4x MAX7219 en cascade - Version Zephyr OS
 */

#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include <zephyr/kernel.h>
#include <stdint.h>

// --- Constantes MAX7219 ---
#define MAX7219_REG_NOOP        0x00
#define MAX7219_REG_DIGIT0      0x01
#define MAX7219_REG_DECODEMODE  0x09
#define MAX7219_REG_INTENSITY   0x0A
#define MAX7219_REG_SCANLIMIT   0x0B
#define MAX7219_REG_SHUTDOWN    0x0C
#define MAX7219_REG_DISPLAYTEST 0x0F
// ----------------------------------------------

// --- Prototypes ---
int LED_Matrix_Init(void);
void LED_Matrix_Clear(void);
void LED_Matrix_SetBrightness(uint8_t intensity); // 0 à 15
void LED_Matrix_Update(void);

// Nouvelles fonctions graphiques
void LED_DrawPixel(int x, int y, int on);
void LED_Print(int x, const char *str);
void LED_DrawIcon(int module, const uint8_t *icon);

// Icônes disponibles
extern const uint8_t ICON_SUN[8];
extern const uint8_t ICON_CLOUD[8];
extern const uint8_t ICON_RAIN[8];

// Buffer global
extern uint8_t LED_Buffer[4][8]; 

#endif