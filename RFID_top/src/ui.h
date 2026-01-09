#pragma once

#include <zephyr/device.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fonctions principales */
int ui_init(const struct device *display);
void ui_tick(void);
void ui_show_prompt(void);

/* Fonctions pour le MAIN (Getters) */
bool ui_is_scan_mode_active(void);
const char* ui_get_scanned_name(void);
void ui_go_back_to_settings(void);

/* Fonctions pour l'EDITION */
bool ui_is_edit_mode(void);

/* Fonctions pour la SUPPRESSION (C'est ici que ça manquait) */
bool ui_is_delete_mode(void);

/* Fonctions pour le CLONAGE */
bool ui_is_clone_mode(void);
int ui_get_clone_step(void);
void ui_set_clone_step(int step);

bool ui_is_admin_auth_mode(void);
void ui_open_settings(void); /* Pour forcer l'ouverture si admin OK */

#ifdef __cplusplus
}
#endif