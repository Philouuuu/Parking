/* src/rfid.c - Driver RC522 Robuste (Soft Reset + Diagnostic) */

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdbool.h>
#include "rfid.h"

/* On donne un nom spécifique aux logs de ce fichier */
LOG_MODULE_REGISTER(rfid, LOG_LEVEL_INF);

/* === CONFIGURATION MATERIELLE === */
#define SPI_BUS      DT_NODELABEL(spi2)
#define GPIOA_NODE   DT_NODELABEL(gpioa)
#define CS_PIN       15  /* PA15 pour le Chip Select du RC522 */

static const struct device *z_spi  = NULL;
static const struct device *z_gpio = NULL;

/* Config SPI : 4MHz, MSB First, Master */
static struct spi_config z_spi_cfg = {
    .frequency = 4000000, 
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
    .slave     = 0,
};

static void cs_select(void)  { gpio_pin_set(z_gpio, CS_PIN, 0); }
static void cs_release(void) { gpio_pin_set(z_gpio, CS_PIN, 1); }

/* --- Primitives SPI --- */
static void Write_MFRC522(uint8_t adr, uint8_t val) {
    uint8_t tx[2] = { (adr << 1) & 0x7E, val };
    struct spi_buf buf = { .buf = tx, .len = 2 };
    struct spi_buf_set set = { .buffers = &buf, .count = 1 };
    
    cs_select();
    spi_write(z_spi, &z_spi_cfg, &set);
    cs_release();
}

static uint8_t Read_MFRC522(uint8_t adr) {
    uint8_t tx[2] = { ((adr << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx[2] = { 0, 0 };
    
    struct spi_buf txb = { .buf = tx, .len = 2 };
    struct spi_buf rxb = { .buf = rx, .len = 2 };
    struct spi_buf_set ts = { .buffers = &txb, .count = 1 };
    struct spi_buf_set rs = { .buffers = &rxb, .count = 1 };
    
    cs_select();
    spi_transceive(z_spi, &z_spi_cfg, &ts, &rs);
    cs_release();
    
    return rx[1];
}

static void SetBitMask(uint8_t reg, uint8_t mask) {
    Write_MFRC522(reg, Read_MFRC522(reg) | mask);
}

static void ClearBitMask(uint8_t reg, uint8_t mask) {
    Write_MFRC522(reg, Read_MFRC522(reg) & (~mask));
}

/* --- Initialisation Hardware --- */
static int rc522_hw_init_once(void) {
    if (z_spi) return 0; // Déjà init
    
    z_spi  = DEVICE_DT_GET(SPI_BUS);
    z_gpio = DEVICE_DT_GET(GPIOA_NODE);
    
    if (!device_is_ready(z_spi) || !device_is_ready(z_gpio)) {
        LOG_ERR("RC522: SPI ou GPIO non pret !");
        return -1;
    }
    
    // Config CS
    gpio_pin_configure(z_gpio, CS_PIN, GPIO_OUTPUT_ACTIVE);
    cs_release(); // CS à 1 par défaut
    
    return 0;
}

/* --- DIAGNOSTIC --- */
bool MFRC522_Check_Hardware(void) {
    /* Lecture du registre de version pour voir si la puce répond */
    uint8_t v = Read_MFRC522(VersionReg);
    LOG_INF("RC522 Version Reg: 0x%02X", v);
    
    // 0x92 = Version 2.0, 0x91 = Version 1.0, 0x88 = Clone
    if (v == 0x92 || v == 0x91 || v == 0x88) return true;
    
    return false;
}

/* --- Fonctions Logique --- */
void MFRC522_Reset(void) {
    /* Soft Reset via commande SPI */
    Write_MFRC522(CommandReg, PCD_RESETPHASE);
    k_msleep(50); 
}

void AntennaOn(void) {
    uint8_t temp = Read_MFRC522(TxControlReg);
    if (!(temp & 0x03)) SetBitMask(TxControlReg, 0x03);
}

void CalulateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData) {
    ClearBitMask(DivIrqReg, 0x04);
    SetBitMask(FIFOLevelReg, 0x80);
    for (int i = 0; i < len; i++) Write_MFRC522(FIFODataReg, pIndata[i]);
    Write_MFRC522(CommandReg, PCD_CALCCRC);
    
    int i = 0xFF;
    while(i--) { if (Read_MFRC522(DivIrqReg) & 0x04) break; }
    
    pOutData[0] = Read_MFRC522(CRCResultRegL);
    pOutData[1] = Read_MFRC522(CRCResultRegM);
}

uint8_t MFRC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, unsigned short *backLen) {
    uint8_t irqEn = 0x00, waitIRq = 0x00;

    if (command == PCD_AUTHENT) { irqEn = 0x12; waitIRq = 0x10; }
    else if (command == PCD_TRANSCEIVE) { irqEn = 0x77; waitIRq = 0x30; }

    Write_MFRC522(CommIEnReg, irqEn | 0x80);
    ClearBitMask(CommIrqReg, 0x80);
    SetBitMask(FIFOLevelReg, 0x80); 
    Write_MFRC522(CommandReg, PCD_IDLE);

    for (int i = 0; i < sendLen; i++) Write_MFRC522(FIFODataReg, sendData[i]);

    Write_MFRC522(CommandReg, command);
    if (command == PCD_TRANSCEIVE) SetBitMask(BitFramingReg, 0x80);

    int i = 2000;
    while (i--) {
        uint8_t n = Read_MFRC522(CommIrqReg);
        if (n & waitIRq) break; 
        if (n & 0x01) return MI_ERR; 
        k_busy_wait(20); 
    }
    ClearBitMask(BitFramingReg, 0x80);

    if (i == 0) return MI_ERR;
    if (Read_MFRC522(ErrorReg) & 0x1B) return MI_ERR;

    if (backData && backLen) {
        uint8_t n = Read_MFRC522(FIFOLevelReg);
        if (n > *backLen) n = *backLen;
        *backLen = n;
        for (int k = 0; k < n; k++) backData[k] = Read_MFRC522(FIFODataReg);
    }
    return MI_OK;
}

uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType) {
    Write_MFRC522(BitFramingReg, 0x07);
    TagType[0] = reqMode;
    unsigned short len = 2;
    if (MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &len) != MI_OK || len != 2) return MI_ERR;
    return MI_OK;
}

uint8_t MFRC522_Anticoll(uint8_t *serNum) {
    Write_MFRC522(BitFramingReg, 0x00);
    uint8_t ser[2] = { PICC_ANTICOLL, 0x20 };
    unsigned short len = 5;
    uint8_t buffer[5];
    if (MFRC522_ToCard(PCD_TRANSCEIVE, ser, 2, buffer, &len) != MI_OK) return MI_ERR;
    if (buffer[4] != (buffer[0] ^ buffer[1] ^ buffer[2] ^ buffer[3])) return MI_ERR;
    memcpy(serNum, buffer, 5);
    return MI_OK;
}

uint8_t MFRC522_SelectTag(uint8_t *serNum) {
    uint8_t buf[9];
    buf[0] = PICC_SElECTTAG; buf[1] = 0x70;
    memcpy(&buf[2], serNum, 5);
    CalulateCRC(buf, 7, &buf[7]);
    unsigned short len = 0; 
    uint8_t dump[4]; len=sizeof(dump);
    return (MFRC522_ToCard(PCD_TRANSCEIVE, buf, 9, dump, &len) == MI_OK) ? MI_OK : MI_ERR;
}

uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum) {
    uint8_t buf[12];
    buf[0] = authMode; buf[1] = BlockAddr;
    memcpy(&buf[2], Sectorkey, 6);
    memcpy(&buf[8], serNum, 4);
    unsigned short len = 0;
    if (MFRC522_ToCard(PCD_AUTHENT, buf, 12, NULL, &len) != MI_OK) return MI_ERR;
    if ((Read_MFRC522(Status2Reg) & 0x08) == 0) return MI_ERR;
    return MI_OK;
}

uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t *recvData) {
    uint8_t buf[4];
    buf[0] = PICC_READ; buf[1] = blockAddr;
    CalulateCRC(buf, 2, &buf[2]);
    unsigned short len = 18; 
    uint8_t buffer[18];
    if (MFRC522_ToCard(PCD_TRANSCEIVE, buf, 4, buffer, &len) != MI_OK) return MI_ERR;
    if (len >= 16) {
        memcpy(recvData, buffer, 16);
        return MI_OK;
    }
    return MI_ERR;
}

uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t *writeData) {
    uint8_t buf[4] = { PICC_WRITE, blockAddr, 0, 0 };
    CalulateCRC(buf, 2, &buf[2]);
    unsigned short len = 0;
    uint8_t ack[4]; len=sizeof(ack);
    if (MFRC522_ToCard(PCD_TRANSCEIVE, buf, 4, ack, &len) != MI_OK) return MI_ERR;
    
    uint8_t frame[18];
    memcpy(frame, writeData, 16);
    CalulateCRC(frame, 16, &frame[16]);
    len=sizeof(ack);
    return MFRC522_ToCard(PCD_TRANSCEIVE, frame, 18, ack, &len);
}

void MFRC522_Halt(void) {
    uint8_t buf[4] = { PICC_HALT, 0 };
    CalulateCRC(buf, 2, &buf[2]);
    unsigned short len = 0;
    MFRC522_ToCard(PCD_TRANSCEIVE, buf, 4, NULL, &len);
}

void MFRC522_StopCrypto1(void) {
    ClearBitMask(Status2Reg, 0x08);
}

void MFRC522_Init(void) {
    if (rc522_hw_init_once() != 0) return;
    
    MFRC522_Reset();
    
    Write_MFRC522(TModeReg, 0x8D);
    Write_MFRC522(TPrescalerReg, 0x3E);
    Write_MFRC522(TReloadRegL, 30); 
    Write_MFRC522(TReloadRegH, 0);
    Write_MFRC522(TxAutoReg, 0x40);
    Write_MFRC522(ModeReg, 0x3D); 
    Write_MFRC522(RFCfgReg, 0x70); // Gain Max
    AntennaOn();
}