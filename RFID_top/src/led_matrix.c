/*
 * led_matrix.c - Driver Version Finale
 * Orientation: Standard (Ligne par Ligne)
 * Synchronisation: Module 0 = Gauche (Loin), Module 3 = Droite (Près)
 */

#include "led_matrix.h"
#include "fonts.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <string.h>

// --- CONFIGURATION HARDWARE ---
#define DIN_NODE DT_NODELABEL(gpiog)
#define DIN_PIN  6
#define CS_NODE  DT_NODELABEL(gpiob)
#define CS_PIN   4
#define CLK_NODE DT_NODELABEL(gpiog)
#define CLK_PIN  7

static const struct device *dev_din = DEVICE_DT_GET(DIN_NODE);
static const struct device *dev_cs  = DEVICE_DT_GET(CS_NODE);
static const struct device *dev_clk = DEVICE_DT_GET(CLK_NODE);

// Buffer vidéo global
uint8_t LED_Buffer[4][8] = {0};

// --- ICONES METEO (Format standard) ---
const uint8_t ICON_SUN[8] = { 0x24, 0x00, 0xbd, 0x3c, 0x3c, 0xbd, 0x00, 0x24 };
const uint8_t ICON_CLOUD[8] = { 0x00, 0x00, 0x06, 0x0d, 0x19, 0x32, 0x50, 0x00 };
const uint8_t ICON_RAIN[8] = { 0x00, 0x24, 0x4a, 0x1d, 0x39, 0x62, 0x50, 0x00 };

static void LED_WriteByte(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        gpio_pin_set(dev_clk, CLK_PIN, 0);
        gpio_pin_set(dev_din, DIN_PIN, (data & (1 << i)) ? 1 : 0);
        k_busy_wait(1); 
        gpio_pin_set(dev_clk, CLK_PIN, 1);
        k_busy_wait(1);
    }
}

static void LED_SendCmdAll(uint8_t address, uint8_t data) {
    gpio_pin_set(dev_cs, CS_PIN, 0);
    for(int i=0; i<4; i++) {
        LED_WriteByte(address); 
        LED_WriteByte(data);    
    }
    gpio_pin_set(dev_cs, CS_PIN, 1);
}

int LED_Matrix_Init(void) {
    if (!device_is_ready(dev_din) || !device_is_ready(dev_cs) || !device_is_ready(dev_clk)) return -1;
    
    gpio_pin_configure(dev_din, DIN_PIN, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(dev_cs,  CS_PIN,  GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure(dev_clk, CLK_PIN, GPIO_OUTPUT_INACTIVE);

    // Initialisation du MAX7219
    LED_SendCmdAll(MAX7219_REG_SHUTDOWN, 0x01);    // Sortie de veille
    LED_SendCmdAll(MAX7219_REG_DECODEMODE, 0x00);  // Pas de décodage
    LED_SendCmdAll(MAX7219_REG_SCANLIMIT, 0x07);   // Scan 8 digits
    LED_SendCmdAll(MAX7219_REG_INTENSITY, 0x01);   // Luminosité faible
    LED_SendCmdAll(MAX7219_REG_DISPLAYTEST, 0x00); // Mode normal
    
    LED_Matrix_Clear();
    return 0;
}

void LED_Matrix_Clear(void) {
    memset(LED_Buffer, 0, sizeof(LED_Buffer));
    LED_Matrix_Update();
}

void LED_Matrix_SetBrightness(uint8_t intensity) {
    if(intensity > 15) intensity = 15;
    LED_SendCmdAll(MAX7219_REG_INTENSITY, intensity);
}

// --- CORRECTION SYNCHRONISATION ---
void LED_Matrix_Update(void) {
    // On garde l'envoi Ligne par Ligne (qui mettait le texte dans le bon sens)
    for (int row = 0; row < 8; row++) {
        gpio_pin_set(dev_cs, CS_PIN, 0);
        
        // MAIS on inverse l'ordre des modules ici !
        // Avant : 3 -> 0 (Désynchronisé)
        // Maintenant : 0 -> 3 (Synchronisé si DIN est à droite)
        // Le premier octet envoyé (Module 0) ira au module le plus LOIN (Gauche).
        for (int module = 0; module < 4; module++) {
            LED_WriteByte(row + 1);          
            LED_WriteByte(LED_Buffer[module][row]); 
        }
        
        gpio_pin_set(dev_cs, CS_PIN, 1);
    }
}

// --- DESSIN STANDARD (Remis à l'endroit) ---
void LED_DrawPixel(int x, int y, int on) {
    if (x < 0 || x >= 32 || y < 0 || y >= 8) return;
    
    int module = x / 8;     
    int col = x % 8;    
    
    // Logique standard (Ligne/Colonne classiques)
    if (on) LED_Buffer[module][y] |= (1 << (7-col)); // 7-col pour MSB à gauche
    else    LED_Buffer[module][y] &= ~(1 << (7-col));
}

void LED_DrawIcon(int module, const uint8_t *icon) {
    // Copie directe car l'orientation est standard
    for(int i=0; i<8; i++) {
        LED_Buffer[module][i] = icon[i];
    }
}

void LED_DrawChar(int x, char c) {
    if (c >= 'a' && c <= 'z') c -= 32; 
    if (c < 32 || c > 90) c = 32; 
    
    int char_idx = c - 32;
    
    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[char_idx][i];
        for (int y = 0; y < 8; y++) {
             if (line & (1 << y)) {
                 LED_DrawPixel(x + i, y, 1); // Pas d'inversion Y ici
             }
        }
    }
}

void LED_Print(int x, const char *str) {
    while (*str) {
        LED_DrawChar(x, *str);
        x += 6; 
        str++;
    }
}