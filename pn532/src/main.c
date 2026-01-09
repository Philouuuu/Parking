#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "pn532.h"
#include "rfid.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* === GLOBALS === */
static struct spi_config spi_cfg;
static struct spi_cs_control cs_ctrl;

/* Flag pour éviter le spam d'ouverture */
bool barrier_open = false;

/* --- CONFIGURATION DU TAG (Standard NDEF) --- */
const uint8_t capability_container[] = {
    0x00, 0x0F, 0x20, 0x00, 0x54, 0x00, 0xFF, 0x04, 0x06, 0xE1, 0x04, 0x00, 0xFF, 0x00, 0x00
};

const uint8_t ndef_aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };

/* Mémoire NDEF */
uint8_t ndef_file[256] = { 0x00 };

typedef enum { NONE, CC, NDEF_FILE } tag_file_t;
tag_file_t current_file = NONE;

/* --- LOGIQUE APDU (Gestion des commandes iPhone) --- */
int handle_apdu(uint8_t *rx, uint8_t rx_len, uint8_t *tx, uint8_t *tx_len) {
    uint8_t ins = rx[1];
    uint8_t p1 = rx[2];
    uint8_t p2 = rx[3];
    uint8_t lc = rx[4];
    
    uint8_t sw1 = 0x90, sw2 = 0x00;
    *tx_len = 0;

    switch (ins) {
        case 0xA4: // SELECT FILE
            if (p1 == 0x04) {
                if (lc >= sizeof(ndef_aid) && memcmp(&rx[5], ndef_aid, sizeof(ndef_aid)) == 0) current_file = NONE;
                else { sw1 = 0x6A; sw2 = 0x82; }
            }
            else if (p1 == 0x00) {
                if (rx[5] == 0xE1 && rx[6] == 0x03) current_file = CC;
                else if (rx[5] == 0xE1 && rx[6] == 0x04) current_file = NDEF_FILE;
                else { sw1 = 0x6A; sw2 = 0x82; }
            }
            break;

        case 0xB0: // READ BINARY
            {
                uint16_t offset = (p1 << 8) | p2;
                uint8_t len = rx[4];
                uint8_t *src = NULL;
                uint16_t max_size = 0;

                if (current_file == CC) { src = (uint8_t*)capability_container; max_size = sizeof(capability_container); }
                else if (current_file == NDEF_FILE) { src = ndef_file; max_size = sizeof(ndef_file); }

                if (src != NULL && offset < max_size) {
                    uint8_t copy_len = (offset + len > max_size) ? (max_size - offset) : len;
                    memcpy(tx, src + offset, copy_len);
                    *tx_len = copy_len;
                } else { sw1 = 0x6A; sw2 = 0x82; }
            }
            break;

        case 0xD6: // UPDATE BINARY (L'écriture magique)
            {
                uint16_t offset = (p1 << 8) | p2;
                uint8_t len = lc;

                if (current_file == NDEF_FILE) {
                    if (offset + len <= sizeof(ndef_file)) {
                        memcpy(&ndef_file[offset], &rx[5], len);
                        
                        // Si le paquet est assez gros, on cherche le mot de passe
                        if (len > 2 && !barrier_open) {
                            bool password_found = false;
                            for (int i = 0; i < len; i++) {
                                if ((i + 5 < len) &&
                                    rx[5+i] == 'S' && rx[5+i+1] == 'E' &&
                                    rx[5+i+2] == 'S' && rx[5+i+3] == 'A' &&
                                    rx[5+i+4] == 'M' && rx[5+i+5] == 'E') 
                                {
                                    password_found = true;
                                    break;
                                }
                            }
                            if (password_found) {
                                LOG_INF("\n#######################################");
                                LOG_INF("#   OUVERTURE BARRIERE (IPHONE)       #");
                                LOG_INF("#######################################\n");
                                barrier_open = true;
                                // gpio_pin_set(dev, pin, 1);
                            }
                        }
                    } else { sw1 = 0x6A; sw2 = 0x84; }
                } else { sw1 = 0x69; sw2 = 0x82; }
            }
            break;
        default:
            sw1 = 0x6D; sw2 = 0x00;
            break;
    }

    tx[(*tx_len)++] = sw1;
    tx[(*tx_len)++] = sw2;
    return 0;
}

int main(void)
{
    k_msleep(1000);
    LOG_INF("=== PARKING ACCESS SYSTEM (HIGH RELIABILITY) ===");

    /* --- INITIALISATION HARDWARE --- */
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
    const struct device *gpio_a = DEVICE_DT_GET(DT_NODELABEL(gpioa));
    const struct device *gpio_i = DEVICE_DT_GET(DT_NODELABEL(gpioi));

    if (!device_is_ready(spi_dev) || !device_is_ready(gpio_a) || !device_is_ready(gpio_i)) return 0;

    // Config SPI PN532
    cs_ctrl.gpio.port = gpio_a; cs_ctrl.gpio.pin = 8; cs_ctrl.gpio.dt_flags = GPIO_ACTIVE_LOW; cs_ctrl.delay = 0;
    spi_cfg.frequency = 500000; spi_cfg.operation = SPI_WORD_SET(8) | SPI_OP_MODE_MASTER;
    spi_cfg.slave = 0;
    pn532_dev_t nfc = { .spi_dev = spi_dev, .spi_cfg = spi_cfg, .cs_gpio = cs_ctrl.gpio, .rst_port = gpio_i, .rst_pin = 2 };

    /* Reset PN532 */
    gpio_pin_configure_dt(&nfc.cs_gpio, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(nfc.rst_port, nfc.rst_pin, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(nfc.rst_port, nfc.rst_pin, 0); k_msleep(100);
    gpio_pin_set(nfc.rst_port, nfc.rst_pin, 1); k_msleep(500);

    if (pn532_init(&nfc) < 0) { LOG_ERR("PN532 Init Failed"); }
    else { pn532_sam_config(&nfc); LOG_INF("PN532 Ready (Slot 2)"); }

    /* Init RC522 */
    MFRC522_Init();
    if (MFRC522_Check_Hardware()) { LOG_INF("RC522 Ready (Slot 1)"); } 
    else { LOG_ERR("RC522 Error"); }

    const uint8_t virtual_uid[3] = { 0x05, 0x05, 0x05 };
    uint8_t rx_buf[255];
    uint8_t tx_buf[255];
    
    // Variables RC522
    uint8_t rc522_uid[10];
    uint8_t rc522_buf[18];
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    while (1) {
        
        /* ---------------------------------------------------------
         * SLOT 1 : VERIFICATION BADGE PHYSIQUE (RC522)
         * C'est très rapide (< 50ms si pas de badge).
         * --------------------------------------------------------- */
        if (MFRC522_Request(PICC_REQIDL, rc522_buf) == MI_OK) {
            if (MFRC522_Anticoll(rc522_uid) == MI_OK) {
                LOG_INF("[RC522] Badge Physique Detecte");
                MFRC522_SelectTag(rc522_uid);
                
                // Auth et Lecture Bloc 4
                if (MFRC522_Auth(PICC_AUTHENT1A, 4, default_key, rc522_uid) == MI_OK) {
                    if (MFRC522_Read(4, rc522_buf) == MI_OK) {
                        bool match = true;
                        char *pwd = "SESAME";
                        for(int i=0; i<6; i++) if(rc522_buf[i] != pwd[i]) match = false;
                        
                        if(match && !barrier_open) {
                             LOG_INF("\n### OUVERTURE BADGE ###\n");
                             barrier_open = true;
                             // gpio_pin_set...
                        }
                    }
                    MFRC522_StopCrypto1();
                }
                MFRC522_Halt();
                barrier_open = false; 
                
                // Anti-rebond après un succès badge
                k_msleep(1000); 
                continue; // On recommence la boucle
            }
        }

        /* ---------------------------------------------------------
         * SLOT 2 : VERIFICATION IPHONE (PN532)
         * Si un téléphone est détecté, on s'y consacre à 100%
         * --------------------------------------------------------- */
        int ret = pn532_init_as_target(&nfc, virtual_uid);

        if (ret == 0) {
            LOG_INF("[PN532] Smartphone detecte -> Mode Verrouillage");
            
            bool active = true;
            current_file = NONE;
            barrier_open = false;

            while (active) {
                uint8_t rx_len = sizeof(rx_buf);
                
                // === LE SECRET EST ICI ===
                // Timeout de 2500ms (2.5 secondes). 
                // Tant que l'iPhone est là, on attend patiemment sa prochaine commande.
                // Cela couvre largement les animations Apple Pay ou les validations FaceID.
                int r = pn532_tg_get_data(&nfc, rx_buf, &rx_len, 2500); 
                
                if (r > 0) {
                    // Données reçues, on traite
                    uint8_t tx_len = 0;
                    handle_apdu(rx_buf, rx_len, tx_buf, &tx_len);
                    
                    // On répond. Si erreur d'envoi, l'iPhone est parti.
                    if (pn532_tg_set_data(&nfc, tx_buf, tx_len) < 0) active = false;
                }
                else if (r == -ETIMEDOUT) {
                    // Timeout de 2.5s écoulé SANS rien recevoir.
                    // Là, on peut considérer que l'utilisateur est parti ou a abandonné.
                    LOG_INF("[PN532] Timeout long (Session terminee)");
                    active = false;
                }
                else {
                    // Erreur technique
                    active = false;
                }
            }
            // Petite pause pour laisser le champ magnétique retomber
            k_msleep(100);
        }
        else {
            // Pas de téléphone. Reset rapide pour être propre.
            if (ret != -ETIMEDOUT) {
                gpio_pin_set(nfc.rst_port, nfc.rst_pin, 0); k_msleep(10);
                gpio_pin_set(nfc.rst_port, nfc.rst_pin, 1); k_msleep(50);
                pn532_sam_config(&nfc);
            }
        }
        
        // Pause minimale entre les cycles de scan global
        k_msleep(20);
    }
    return 0;
}