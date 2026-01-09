#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h> 
#include <stdbool.h>

/* Initialisation et boucle de vie MQTT */
void network_init(void);
void network_poll(void); // A appeler dans le main loop pour gérer les paquets MQTT

/* Météo (Récupération simple de la dernière valeur reçue) */
bool network_get_weather(char *buf_out, size_t max_len);

/* Fonctions Métiers (Bloquantes : attendent la réponse MQTT) */
int network_send_check(const char *uid, char *name_out, size_t max_len);
int network_send_add(const char *uid, const char *name);
int network_send_edit(const char *uid, const char *name);
int network_send_del(const char *uid);

/* Logs UI */
void network_get_last_logs(char *clear_buf, size_t clear_len, char *crypted_buf, size_t crypted_len);

#endif /* NETWORK_H */