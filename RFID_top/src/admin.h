#ifndef ADMIN_H
#define ADMIN_H

#include <stdbool.h>

/* Initialise la SD Card */
int admin_init_sd(void);

/* Vérifie si l'UID est présent dans admin.csv */
/* Format CSV attendu : UID,Nom (ex: 0E7AB005,Admin) */
bool admin_check_uid(const char *uid_str);

#endif
