/*
 * main.c - Système Parking Complet + METEO + BME680
 * Hardware: STM32F756G-DISCO
 * Mise à jour : Remplacement RC522 par PN532 (SPI)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <stdio.h>
#include <string.h>

// --- INCLUDES LOCAUX ---
#include "lcd_i2c.h"
#include "lvgl.h"
#include "network.h"
#include "pn532.h"    // <--- Changement ici (plus de rfid.h)
#include "admin.h" 
#include "ui.h"
#include "led_matrix.h"
#include "bme680_sensor.h"

// --- CONFIGURATION THREAD LED ---
#define LED_STACK_SIZE 1024
#define LED_PRIORITY   2 

// --- DONNÉES PARTAGÉES (Thread Safe) ---
K_MUTEX_DEFINE(weather_mtx);
char shared_scrolling_msg[512] = "INITIALISATION..."; 
char text_mqtt[256]   = "CONNEXION...";
char text_sensor[128] = "INIT CAPTEUR...";

// --- FONCTION D'ASSEMBLAGE ---
void update_full_message(void) {
    k_mutex_lock(&weather_mtx, K_FOREVER);
    snprintf(shared_scrolling_msg, sizeof(shared_scrolling_msg), "%s | %s", text_mqtt, text_sensor);
    k_mutex_unlock(&weather_mtx);
}

// --- FONCTIONS UTILITAIRES LCD ---
void lcd_center_text(uint8_t line, const char *text) {
    int len = strlen(text);
    int pad = (16 - len) / 2;
    if(pad < 0) pad = 0;
    LCD_GoTo(line, 1 + pad);
    LCD_WriteText(text);
}

void lcd_show_welcome(void) {
    LCD_Clear();
    lcd_center_text(1, "   BIENVENUE   ");
    lcd_center_text(2, "Presentez Badge");
}

void refresh_lcd_logic(void) {
    if (ui_is_admin_auth_mode()) {
        LCD_Clear(); 
        lcd_center_text(1, "AUTH ADMIN");
        lcd_center_text(2, "Badge Requis");
    } else if (ui_is_scan_mode_active()) {
        LCD_Clear();
        lcd_center_text(1, "MODE GESTION");
        if (ui_is_delete_mode()) lcd_center_text(2, "Passer Badge -");
        else lcd_center_text(2, "Passer Badge +");
    } else {
        lcd_show_welcome();
    }
}

void uid_to_hex(uint8_t *uid, char *str_out) {
    // Convertit les 4 premiers octets (Standard Mifare Classic)
    // Si tu utilises des cartes Ultralight (7 octets), il faudra adapter ici.
    sprintf(str_out, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
}

// --- THREAD DÉDIÉ A LA MATRICE LED ---
void led_matrix_thread(void)
{
    int pos_x = 32;
    char local_msg[512]; 
    int msg_total_width = 0;

    k_msleep(100);

    while (1) {
        k_mutex_lock(&weather_mtx, K_FOREVER);
        strncpy(local_msg, shared_scrolling_msg, sizeof(local_msg)-1);
        local_msg[sizeof(local_msg)-1] = '\0';
        k_mutex_unlock(&weather_mtx);

        msg_total_width = strlen(local_msg) * 6;

        LED_Matrix_Clear();
        LED_Print(pos_x, local_msg);
        LED_Matrix_Update();
        
        pos_x--;
        if (pos_x < -msg_total_width) {
            pos_x = 32; 
        }

        k_msleep(30); 
    }
}

K_THREAD_DEFINE(led_tid, LED_STACK_SIZE, led_matrix_thread, NULL, NULL, NULL, LED_PRIORITY, 0, 0);


// --- MAIN LOOP ---
int main(void)
{
    const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    k_thread_priority_set(k_current_get(), 5);

    // --- 1. INITIALISATIONS ---
    if (device_is_ready(i2c)) {
        LCD_Initalize(i2c);
        LCD_Backlight(true);
        LCD_Clear();
        lcd_center_text(1, "DEMARRAGE...");
    }
    
    if (device_is_ready(display)) ui_init(display);
    
    ui_set_state_change_cb(refresh_lcd_logic);

    LCD_GoTo(2, 1); LCD_WriteText("Connexion MQTT..");
    network_init(); 
    
    LCD_GoTo(2, 1); LCD_WriteText("SD Card...");
    if (admin_init_sd() != 0) {
        printk("WARN: SD Card echec init\n");
    }

    // --- INITIALISATION PN532 ---
    LCD_GoTo(2, 1); LCD_WriteText("NFC PN532...");
    if (PN532_Init() != 0) {
         LCD_Clear(); lcd_center_text(1, "ERREUR NFC");
         lcd_center_text(2, "Check Cables!");
         // On continue quand même, mais le NFC ne marchera pas
         k_msleep(2000);
    } else {
         printk("PN532 Initialise avec succes !\n");
    }

    LCD_GoTo(2, 1); LCD_WriteText("LED Matrix...");
    LED_Matrix_Init(); 

    LCD_GoTo(2, 1); LCD_WriteText("Capteur BME...");
    bme680_init(); 
    
    k_msleep(1000);
    lcd_show_welcome();

    // Variables locales
    uint8_t uid_raw[8]; // Agrandissement du buffer pour supporter plus de types de cartes
    uint8_t uid_len = 0;
    char uid_str[32];
    char rx_buf[128]; 
    bool card_was_present = false;
    
    // Timers
    int64_t last_rfid_check = 0;
    int64_t last_sensor_read = 0;

    char temp_net_buf[256];

    while (1) {
        ui_tick(); 
        network_poll(); 

        // --- 2. GESTION METEO ---
        if (network_get_weather(temp_net_buf, sizeof(temp_net_buf))) {
            printk("Recu Meteo MQTT: %s\n", temp_net_buf);
            strncpy(text_mqtt, temp_net_buf, sizeof(text_mqtt)-1);
            text_mqtt[sizeof(text_mqtt)-1] = '\0';
            update_full_message();
        }

        // --- 3. GESTION CAPTEUR BME680 ---
        if (k_uptime_get() - last_sensor_read > 5000) {
            last_sensor_read = k_uptime_get();
            int32_t t, h, p;
            bme680_get_values(&t, &h, &p); 
            snprintf(text_sensor, sizeof(text_sensor), "SALLE: %dC %d%% %dhPa", t, h, p);
            update_full_message();
        }

        // --- 4. GESTION RFID (VERSION PN532) ---
        if (k_uptime_get() - last_rfid_check > 200) {
            last_rfid_check = k_uptime_get();
            bool detected = false;
            
            // Le PN532 gère la détection et l'anticollision en interne
            if (PN532_ReadPassiveTargetID(uid_raw, &uid_len)) {
                detected = true;
                uid_to_hex(uid_raw, uid_str);
            }

            if (detected && !card_was_present) {
                printk(">> Badge detecte: %s (Len: %d)\n", uid_str, uid_len);

                // --- LOGIQUE METIER ---
                
                // A. Mode Admin
                if (ui_is_admin_auth_mode()) {
                    LCD_Clear(); lcd_center_text(1, "VERIF ADMIN...");
                    if (admin_check_uid(uid_str)) {
                        LCD_Clear(); lcd_center_text(1, "ADMIN VALIDE");
                        ui_open_settings(); 
                    } else {
                        LCD_Clear(); lcd_center_text(1, "ACCES REFUSE");
                        lcd_center_text(2, "Non autorise");
                        k_msleep(2000); 
                        ui_show_prompt(); 
                    }
                }
                // B. Mode Gestion (Ajout/Suppression)
                else if (ui_is_scan_mode_active()) {
                    LCD_Clear(); lcd_center_text(1, "TRAITEMENT...");
                    if (ui_is_delete_mode()) {
                        if (network_send_del(uid_str)) {
                            LCD_Clear(); lcd_center_text(1, "SUPPRESSION OK");
                            ui_go_back_to_settings();
                        } else {
                            LCD_Clear(); lcd_center_text(1, "ECHEC SUPPR");
                            lcd_center_text(2, "(Inconnu?)");
                        }
                    } else {
                        const char *name = ui_get_scanned_name();
                        int res = 0;
                        if (ui_is_edit_mode()) res = network_send_edit(uid_str, name);
                        else                   res = network_send_add(uid_str, name);
                        
                        if (res) {
                            LCD_Clear(); lcd_center_text(1, "ENREGISTRE !");
                            lcd_center_text(2, name);
                            ui_go_back_to_settings();
                        } else {
                            LCD_Clear(); lcd_center_text(1, "ERREUR ENREG");
                            lcd_center_text(2, "(Existant?)");
                        }
                    }
                }
                // C. Mode Normal (Contrôle d'accès)
                else {
                    LCD_Clear(); lcd_center_text(1, "VERIFICATION...");
                    
                    if (network_send_check(uid_str, rx_buf, sizeof(rx_buf))) {
                        LCD_Clear(); lcd_center_text(1, "ACCES AUTORISE");
                        lcd_center_text(2, rx_buf);
                    } else {
                        LCD_Clear(); lcd_center_text(1, "ACCES REFUSE");
                        lcd_center_text(2, "Badge Inconnu");
                    }
                }
                
                // Pause pour lecture LCD et éviter les rebonds rapides
                k_msleep(1500); 
                refresh_lcd_logic();
            }
            card_was_present = detected;
        }
        
        k_msleep(20); 
    }
    return 0;
}