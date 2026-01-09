#include "pn532.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pn532, LOG_LEVEL_INF);

static uint8_t pn532_packetbuffer[64];

static uint8_t reverse_bit_order(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static int pn532_spi_write_frame(pn532_dev_t *dev, uint8_t *data, uint8_t len) {
    gpio_pin_set_dt(&dev->cs_gpio, 1); 
    k_busy_wait(500); 

    uint8_t cmd = reverse_bit_order(0x01);
    struct spi_buf tx_buf_cmd = { .buf = &cmd, .len = 1 };
    struct spi_buf_set tx_cmd = { .buffers = &tx_buf_cmd, .count = 1 };
    
    if (spi_write(dev->spi_dev, &dev->spi_cfg, &tx_cmd) < 0) {
        gpio_pin_set_dt(&dev->cs_gpio, 0);
        return -EIO;
    }

    for (int i = 0; i < len; i++) {
        uint8_t b = reverse_bit_order(data[i]);
        struct spi_buf tx_buf = { .buf = &b, .len = 1 };
        struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
        spi_write(dev->spi_dev, &dev->spi_cfg, &tx);
    }

    k_busy_wait(500);
    gpio_pin_set_dt(&dev->cs_gpio, 0);
    return 0;
}

static int pn532_wait_ready(pn532_dev_t *dev, int timeout_ms) {
    uint8_t cmd = reverse_bit_order(0x02);
    uint8_t response;
    int slept = 0;

    while (slept < timeout_ms) {
        gpio_pin_set_dt(&dev->cs_gpio, 1);
        k_busy_wait(200);

        struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
        struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
        spi_write(dev->spi_dev, &dev->spi_cfg, &tx);

        struct spi_buf rx_buf = { .buf = &response, .len = 1 };
        struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
        spi_read(dev->spi_dev, &dev->spi_cfg, &rx);

        gpio_pin_set_dt(&dev->cs_gpio, 0);

        if (reverse_bit_order(response) & 0x01) return 0;

        k_msleep(10); 
        slept += 10;
    }
    return -ETIMEDOUT;
}

static int pn532_read_frame(pn532_dev_t *dev, uint8_t *buff, uint8_t n) {
    uint8_t cmd = reverse_bit_order(0x03);
    uint8_t byte;
    
    gpio_pin_set_dt(&dev->cs_gpio, 1);
    k_busy_wait(500);

    struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
    struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
    spi_write(dev->spi_dev, &dev->spi_cfg, &tx);

    int timeout = 50; 
    bool synced = false;
    while (timeout > 0) {
        struct spi_buf rx_buf = { .buf = &byte, .len = 1 };
        struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
        spi_read(dev->spi_dev, &dev->spi_cfg, &rx);
        
        if (reverse_bit_order(byte) == 0xFF) {
            synced = true;
            break;
        }
        timeout--;
    }

    if (!synced) {
        gpio_pin_set_dt(&dev->cs_gpio, 0);
        return -EIO; 
    }

    buff[0] = 0x00; buff[1] = 0x00; buff[2] = 0xFF; 

    for(int i=3; i<n; i++) {
        struct spi_buf rx_buf = { .buf = &byte, .len = 1 };
        struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
        spi_read(dev->spi_dev, &dev->spi_cfg, &rx);
        buff[i] = reverse_bit_order(byte);
    }

    gpio_pin_set_dt(&dev->cs_gpio, 0);
    return 0;
}

static int pn532_read_ack(pn532_dev_t *dev) {
    uint8_t ack_buff[6];
    if (pn532_wait_ready(dev, 200) < 0) return -ETIMEDOUT;
    if (pn532_read_frame(dev, ack_buff, 6) < 0) return -EIO;
    if (ack_buff[1] == 0x00 && ack_buff[2] == 0xFF) return 0;
    return -EIO;
}

static int pn532_write_command(pn532_dev_t *dev, uint8_t *header, uint8_t hlen, uint8_t *body, uint8_t blen) {
    uint8_t cmd = 0xD4; 
    pn532_packetbuffer[0] = 0x00; pn532_packetbuffer[1] = 0x00; pn532_packetbuffer[2] = 0xFF;
    uint8_t length = hlen + blen + 1;
    pn532_packetbuffer[3] = length; pn532_packetbuffer[4] = (~length + 1);
    pn532_packetbuffer[5] = cmd;
    uint8_t sum = cmd;
    int idx = 6;
    for (int i = 0; i < hlen; i++) { pn532_packetbuffer[idx++] = header[i]; sum += header[i]; }
    for (int i = 0; i < blen; i++) { pn532_packetbuffer[idx++] = body[i]; sum += body[i]; }
    pn532_packetbuffer[idx++] = (~sum + 1); pn532_packetbuffer[idx++] = 0x00;
    return pn532_spi_write_frame(dev, pn532_packetbuffer, idx);
}

int pn532_init(pn532_dev_t *dev) {
    if (!device_is_ready(dev->spi_dev)) return -ENODEV;
    gpio_pin_configure_dt(&dev->cs_gpio, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(dev->rst_port, dev->rst_pin, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(dev->rst_port, dev->rst_pin, 0); 
    k_msleep(100);
    gpio_pin_set(dev->rst_port, dev->rst_pin, 1);
    k_msleep(500); 
    return 0;
}

int pn532_sam_config(pn532_dev_t *dev) {
    uint8_t cmd[] = {0x14, 0x01, 0x14, 0x01};
    if (pn532_write_command(dev, cmd, sizeof(cmd), NULL, 0) < 0) return -1;
    return pn532_read_ack(dev);
}

int pn532_set_rf_retries(pn532_dev_t *dev, uint8_t max_retries) {
    uint8_t cmd[] = {PN532_COMMAND_RFCONFIGURATION, 0x05, 0xFF, 0x01, max_retries};
    if (pn532_write_command(dev, cmd, sizeof(cmd), NULL, 0) < 0) return -1;
    if (pn532_read_ack(dev) < 0) return -2;
    if (pn532_wait_ready(dev, 100) < 0) return 0;
    uint8_t resp[16];
    pn532_read_frame(dev, resp, 10);
    return 0;
}

int pn532_init_as_target(pn532_dev_t *dev, const uint8_t *nfcid1)
{
    /* Données Historiques (ATS) */
    uint8_t tk[] = { 0x75, 0x77, 0x81, 0x02, 0x80 };

    uint8_t header[64];
    int idx = 0;

    header[idx++] = PN532_COMMAND_TGINITASTARGET;
    
    /* MODE 0x05 pour satisfaire ANDROID et IPHONE (Anti-FeliCa) */
    header[idx++] = 0x05; 

    /* MIFARE Params */
    header[idx++] = 0x04; header[idx++] = 0x00; 
    header[idx++] = nfcid1[0]; header[idx++] = nfcid1[1]; header[idx++] = nfcid1[2]; 
    header[idx++] = 0x20; 

    /* FeliCa Params (Zero) */
    for(int i=0; i<18; i++) header[idx++] = 0x00;

    /* NFCID3t (Zero) */
    for(int i=0; i<10; i++) header[idx++] = 0x00;

    /* General Bytes */
    header[idx++] = 0x00;
    header[idx++] = sizeof(tk);
    for(int i=0; i<sizeof(tk); i++) header[idx++] = tk[i];

    if (pn532_write_command(dev, header, idx, NULL, 0) < 0) return -1;
    if (pn532_read_ack(dev) < 0) return -2;

    /* Attente iPhone/Android */
    if (pn532_wait_ready(dev, 1000) < 0) return -ETIMEDOUT;

    /* Lecture réponse */
    uint8_t resp[64];
    if (pn532_read_frame(dev, resp, sizeof(resp)) < 0) return -3;

    /* Vérification */
    if (resp[6] == 0x7F) return -4; // Erreur syntaxe
    if (resp[6] != 0x8D) return -5; // Mauvaise réponse

    return 0; 
}

int pn532_tg_get_data(pn532_dev_t *dev, uint8_t *buf, uint8_t *len, int timeout_ms)
{
    uint8_t cmd = PN532_COMMAND_TGGETDATA;

    if (pn532_write_command(dev, &cmd, 1, NULL, 0) < 0) return -1;
    if (pn532_read_ack(dev) < 0) return -2;

    if (pn532_wait_ready(dev, timeout_ms) < 0) return 0; 

    uint8_t resp[250]; 
    if (pn532_read_frame(dev, resp, sizeof(resp)) < 0) return -3;

    if (resp[7] != 0x00) return -10 - resp[7]; 
    if (resp[3] < 3) return -5; 
    
    uint8_t data_len = resp[3] - 3;
    if (data_len > *len) data_len = *len;

    for (int i = 0; i < data_len; i++) buf[i] = resp[8 + i];
    *len = data_len;
    return (int)data_len;
}

int pn532_tg_set_data(pn532_dev_t *dev, const uint8_t *data, uint8_t len)
{
    static uint8_t cmd[260]; 
    cmd[0] = 0x8E; // TgSetData
    for(int i=0; i<len; i++) cmd[1+i] = data[i];

    if (pn532_write_command(dev, cmd, len+1, NULL, 0) < 0) return -1;
    if (pn532_read_ack(dev) < 0) return -2;
    
    pn532_wait_ready(dev, 100);
    
    uint8_t dummy[16];
    pn532_read_frame(dev, dummy, 10);
    return 0;
}