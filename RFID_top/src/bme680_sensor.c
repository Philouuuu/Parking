/*
 * bme680_sensor.c
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>
#include "bme680_sensor.h"

// On récupère le device défini dans app.overlay avec le label "bme680"
// ou compatible "bosch,bme680"
const struct device *dev_bme = DEVICE_DT_GET_ANY(bosch_bme680);

void bme680_init(void) {
    if (!device_is_ready(dev_bme)) {
        printk("[BME680] ERREUR : Capteur introuvable ou mal branché !\n");
        return;
    }
    printk("[BME680] Capteur détecté et prêt.\n");
}

void bme680_read(void) {
    struct sensor_value temp, press, hum;

    if (!device_is_ready(dev_bme)) return;

    // 1. Lance la mesure (Fetch)
    if (sensor_sample_fetch(dev_bme) < 0) {
        printk("[BME680] Echec lecture échantillon\n");
        return;
    }

    // 2. Récupère les données
    sensor_channel_get(dev_bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    sensor_channel_get(dev_bme, SENSOR_CHAN_PRESS, &press);
    sensor_channel_get(dev_bme, SENSOR_CHAN_HUMIDITY, &hum);

    // 3. Affiche (Debug)
    // Zephyr divise les float en deux entiers (val1 = entier, val2 = décimale)
    printk("[INT] Temp: %d.%02d C | Hum: %d.%02d %% | Pres: %d kPa\n",
           temp.val1, temp.val2 / 10000, 
           hum.val1, hum.val2 / 10000,
           press.val1);
}

void bme680_get_values(int32_t *temp_out, int32_t *hum_out, int32_t *pres_out) {
    struct sensor_value temp, press, hum;

    if (!device_is_ready(dev_bme)) return;

    // On lance la mesure
    if (sensor_sample_fetch(dev_bme) < 0) return;

    // On récupère les canaux
    sensor_channel_get(dev_bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    sensor_channel_get(dev_bme, SENSOR_CHAN_HUMIDITY, &hum);
    sensor_channel_get(dev_bme, SENSOR_CHAN_PRESS, &press);

    // On remplit les pointeurs (Partie entière uniquement pour l'affichage LED)
    if (temp_out) *temp_out = temp.val1;
    if (hum_out)  *hum_out  = hum.val1;
    // La pression est souvent en kPa (ex: 101). Pour hPa (1013), on * 10
    if (pres_out) *pres_out = press.val1 * 10; 
}