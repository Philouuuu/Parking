#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "pn532.h"

/* === CONFIG HARDWARE === */
#define SPI_BUS      DT_NODELABEL(spi2)
#define GPIO_CS_NODE DT_NODELABEL(gpioa)
#define CS_PIN       8
#define GPIO_RST_NODE DT_NODELABEL(gpioi)
#define RST_PIN       2 

/* === CONSTANTES === */
#define PN532_PREAMBLE      0x00
#define PN532_STARTCODE1    0x00
#define PN532_STARTCODE2    0xFF
#define PN532_POSTAMBLE     0x00
#define PN532_HOSTTOPN532   0xD4
#define PN532_PN532TOHOST   0xD5
#define PN532_SPI_STATREAD  0x02
#define PN532_SPI_DATAWRITE 0x01
#define PN532_SPI_DATAREAD  0x03
#define PN532_SPI_READY     0x01

#define PN532_COMMAND_GETFIRMWAREVERSION    0x02
#define PN532_COMMAND_SAMCONFIGURATION      0x14
#define PN532_COMMAND_INLISTPASSIVETARGET   0x4A

static const struct device *z_spi = NULL;
static const struct device *z_cs_gpio = NULL;
static const struct device *z_rst_gpio = NULL;

static struct spi_config z_spi_cfg;

/* --- OUTILS --- */
static uint8_t reverse_byte(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static void debug_dump(const char* label, uint8_t* buf, int len) {
    printk("%s: ", label);
    for(int i=0; i<len; i++) printk("%02X ", buf[i]);
    printk("\n");
}

/* --- BAS NIVEAU --- */

static void cs_select(void) { 
    gpio_pin_set(z_cs_gpio, CS_PIN, 0); 
    k_busy_wait(500); 
}

static void cs_release(void) { 
    gpio_pin_set(z_cs_gpio, CS_PIN, 1); 
    k_busy_wait(500); 
}

static bool pn532_is_ready(void) {
    /* CORRECTION TX : On n'inverse PAS la commande ici, le driver le fera (SPI_TRANSFER_LSB) */
    uint8_t cmd[4] = {PN532_SPI_STATREAD, 0x00, 0x00, 0x00};
    uint8_t rx_buff[4] = {0x00, 0x00, 0x00, 0x00};
    
    struct spi_buf tx_buf = { .buf = cmd, .len = 4 };
    struct spi_buf rx_buf = { .buf = rx_buff, .len = 4 };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

    cs_select();
    spi_transceive(z_spi, &z_spi_cfg, &tx, &rx);
    cs_release();

    /* CORRECTION RX : On INVERSE la réponse car le driver ne le fait pas en lecture */
    for(int i=0; i<4; i++) rx_buff[i] = reverse_byte(rx_buff[i]);

    // printk("[DBG] Ready? %02X %02X\n", rx_buff[0], rx_buff[1]); 

    if (rx_buff[0] == 0x01 || rx_buff[1] == 0x01 || rx_buff[2] == 0x01 || rx_buff[3] == 0x01) {
        return true;
    }
    return false;
}

static bool pn532_wait_ready(int timeout_ms) {
    int loops = timeout_ms / 2;
    while (loops--) {
        if (pn532_is_ready()) return true;
        k_msleep(2);
    }
    return false;
}

static void pn532_write_command(uint8_t *cmd, uint8_t cmd_len) {
    uint8_t checksum = (uint8_t)(PN532_PREAMBLE + PN532_STARTCODE1 + PN532_STARTCODE2 + PN532_HOSTTOPN532);
    uint8_t packet[64];
    int idx = 0;

    packet[idx++] = PN532_SPI_DATAWRITE;
    packet[idx++] = PN532_PREAMBLE;
    packet[idx++] = PN532_STARTCODE1;
    packet[idx++] = PN532_STARTCODE2;
    packet[idx++] = cmd_len + 1;        
    packet[idx++] = (uint8_t)(~((uint8_t)(cmd_len + 1)) + 1); 
    packet[idx++] = PN532_HOSTTOPN532;  

    for (int i = 0; i < cmd_len; i++) {
        packet[idx++] = cmd[i];
        checksum += cmd[i];
    }

    packet[idx++] = (uint8_t)(~checksum + 1); 
    packet[idx++] = PN532_POSTAMBLE;

    /* CORRECTION TX : PAS D'INVERSION ICI ! */
    /* On laisse SPI_TRANSFER_LSB faire le travail pour l'envoi */
    // for(int i=0; i<idx; i++) packet[i] = reverse_byte(packet[i]); // <--- SUPPRIMÉ

    struct spi_buf tx_buf = { .buf = packet, .len = idx };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

    cs_select();
    spi_write(z_spi, &z_spi_cfg, &tx);
    cs_release();
}

static bool pn532_read_ack(void) {
    if (!pn532_wait_ready(100)) {
        printk("[DBG] Timeout ACK Ready.\n");
        return false;
    }

    /* TX : Pas d'inversement */
    uint8_t cmd = PN532_SPI_DATAREAD;
    uint8_t ack_buff[10];
    
    struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
    struct spi_buf rx_buf = { .buf = ack_buff, .len = 10 };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

    cs_select();
    spi_transceive(z_spi, &z_spi_cfg, &tx, &rx);
    cs_release();

    /* RX : On inverse */
    for(int i=0; i<10; i++) ack_buff[i] = reverse_byte(ack_buff[i]);

    debug_dump("[DBG] ACK Frame", ack_buff, 8);

    /* Check ACK : 00 00 FF 00 FF 00 */
    for (int i=0; i<5; i++) {
        if (ack_buff[i] == 0x00 && ack_buff[i+1] == 0x00 && ack_buff[i+2] == 0xFF && ack_buff[i+3] == 0x00) {
            return true;
        }
    }
    return false;
}

static int pn532_read_response(uint8_t *buf, uint8_t max_len) {
    if (!pn532_wait_ready(100)) return -1;

    uint8_t cmd = PN532_SPI_DATAREAD; // Pas d'inversement TX
    uint8_t raw_buff[32]; 
    
    struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
    struct spi_buf rx_buf = { .buf = raw_buff, .len = sizeof(raw_buff) };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

    cs_select();
    spi_transceive(z_spi, &z_spi_cfg, &tx, &rx);
    cs_release();

    /* RX : On inverse */
    for(int i=0; i<sizeof(raw_buff); i++) raw_buff[i] = reverse_byte(raw_buff[i]);

    /* Debug DATA */
    debug_dump("[DBG] Data Frame", raw_buff, 16);

    int offset = 0; 
    while (offset < 20) {
        if (raw_buff[offset] == 0x00 && raw_buff[offset+1] == 0x00 && raw_buff[offset+2] == 0xFF) {
            if (raw_buff[offset+3] != 0x00) { 
                offset += 2; 
                break;
            }
        }
        offset++;
    }
    
    if (offset >= 20) return -2; 

    uint8_t len = raw_buff[offset + 1];
    int data_len = len - 1; 
    if (data_len > max_len) data_len = max_len;

    for (int i=0; i<data_len; i++) {
        buf[i] = raw_buff[offset + 4 + i];
    }
    return data_len;
}

/* --- INIT --- */
int PN532_Init(void) {
    z_spi      = DEVICE_DT_GET(SPI_BUS);
    z_cs_gpio  = DEVICE_DT_GET(GPIO_CS_NODE);
    z_rst_gpio = DEVICE_DT_GET(GPIO_RST_NODE);

    if (!device_is_ready(z_spi) || !device_is_ready(z_cs_gpio) || !device_is_ready(z_rst_gpio)) return -1;

    z_spi_cfg.frequency = 250000;
    /* IMPORTANT : On garde LSB et MODE 3 */
    z_spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_LSB | SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA; 
    z_spi_cfg.slave = 0;
    
    gpio_pin_configure(z_cs_gpio, CS_PIN, GPIO_OUTPUT_ACTIVE);
    cs_release(); 
    gpio_pin_configure(z_rst_gpio, RST_PIN, GPIO_OUTPUT_ACTIVE);
    
    printk("PN532: Reset...\n");
    gpio_pin_set(z_rst_gpio, RST_PIN, 1); k_msleep(10);
    gpio_pin_set(z_rst_gpio, RST_PIN, 0); k_msleep(50);
    gpio_pin_set(z_rst_gpio, RST_PIN, 1); k_msleep(200); 

    printk("PN532: GetFirmware...\n");
    uint8_t cmd_ver[] = { PN532_COMMAND_GETFIRMWAREVERSION };
    pn532_write_command(cmd_ver, 1);
    
    if (!pn532_read_ack()) {
        printk("PN532: Erreur ACK.\n");
        return -1;
    }
    
    uint8_t resp[6];
    if (pn532_read_response(resp, 6) < 0) {
        printk("PN532: Erreur Data.\n");
        return -1;
    }
    printk("PN532: Init OK! Ver: %d.%d\n", resp[1], resp[2]);

    uint8_t cmd_sam[] = { PN532_COMMAND_SAMCONFIGURATION, 0x01, 0x14, 0x01 };
    pn532_write_command(cmd_sam, 4);
    pn532_read_ack(); 
    pn532_read_response(resp, 0); 

    return 0;
}

bool PN532_ReadPassiveTargetID(uint8_t *uid_out, uint8_t *uid_len) {
    uint8_t cmd[] = { PN532_COMMAND_INLISTPASSIVETARGET, 0x01, 0x00 };
    pn532_write_command(cmd, 3);

    if (!pn532_read_ack()) return false;
    
    if (!pn532_wait_ready(100)) return false; 

    uint8_t buffer[20];
    int len = pn532_read_response(buffer, 20);

    if (len > 0 && buffer[0] == 1) {
        *uid_len = buffer[5];
        for (uint8_t i = 0; i < buffer[5]; i++) {
            uid_out[i] = buffer[6 + i];
        }
        return true;
    }
    return false;
}