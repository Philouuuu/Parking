#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

/* Initialise la carte SD */
int storage_init(void);

/* Ajoute une carte : écrit "UID;Nom" à la fin du fichier */
int storage_add_card(const char *uid, const char *name);

/* Cherche un UID et remplit name_buf si trouvé. Retourne 0 si OK, -1 si inconnu */
int storage_find_name(const char *uid, char *name_buf, size_t max_len);

#endif
