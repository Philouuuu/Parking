#ifndef PN532_H
#define PN532_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

/* Codes de Commande PN532 */
#define PN532_COMMAND_RFCONFIGURATION    0x32
#define PN532_COMMAND_TGINITASTARGET     0x8C
#define PN532_COMMAND_TGGETDATA          0x86
#define PN532_COMMAND_TGSETDATA          0x8E

typedef struct {
    const struct device *spi_dev;
    struct spi_config spi_cfg;
    struct gpio_dt_spec cs_gpio;
    const struct device *rst_port;
    gpio_pin_t rst_pin;
} pn532_dev_t;

/* Prototypes exacts de votre code fonctionnel */
int pn532_init(pn532_dev_t *dev);
int pn532_sam_config(pn532_dev_t *dev);
int pn532_set_rf_retries(pn532_dev_t *dev, uint8_t max_retries);
int pn532_init_as_target(pn532_dev_t *dev, const uint8_t *nfcid1);
int pn532_tg_get_data(pn532_dev_t *dev, uint8_t *buf, uint8_t *len, int timeout_ms);
int pn532_tg_set_data(pn532_dev_t *dev, const uint8_t *data, uint8_t len);

#endif