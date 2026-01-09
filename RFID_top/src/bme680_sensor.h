/*
 * bme680_sensor.h
 * Gestion du capteur environnemental BME680 (Temp/Hum/Pres)
 */

#ifndef BME680_SENSOR_H
#define BME680_SENSOR_H

#include <stdint.h> // Nécessaire pour int32_t

void bme680_init(void);
void bme680_read(void); // Garde l'ancienne pour le debug si tu veux

void bme680_get_values(int32_t *temp, int32_t *hum, int32_t *pres);

#endif