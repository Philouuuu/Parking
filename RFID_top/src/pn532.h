#ifndef PN532_H
#define PN532_H

#include <stdint.h>
#include <stdbool.h>

/* Initialise le module PN532 en SPI */
/* Retourne 0 si OK, -1 si échec (module pas trouvé) */
int PN532_Init(void);

/* Fonction principale : Cherche un tag/badge à proximité */
/* Remplit uid_out avec l'ID (4 ou 7 octets) et uid_len avec la longueur */
/* Retourne true si un badge est détecté, false sinon */
bool PN532_ReadPassiveTargetID(uint8_t *uid_out, uint8_t *uid_len);

/* Met le module en veille (Optionnel, pour économiser l'énergie) */
void PN532_Sleep(void);

#endif