#pragma once

#include <zephyr/device.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Définition du type de fonction pour le callback d'état */
typedef void (*ui_state_cb_t)(void);

/* Fonctions principales */
int ui_init(const struct device *display);
void ui_tick(void);
void ui_show_prompt(void);

/* Enregistrement du callback pour le MAIN */
void ui_set_state_change_cb(ui_state_cb_t cb);

/* Fonctions pour le MAIN (Getters) */
bool ui_is_scan_mode_active(void);
const char* ui_get_scanned_name(void);
void ui_go_back_to_settings(void);

/* Fonctions pour l'EDITION et SUPPRESSION */
bool ui_is_edit_mode(void);
bool ui_is_delete_mode(void);

/* Gestion de l'authentification Admin */
bool ui_is_admin_auth_mode(void);
void ui_open_settings(void);

#ifdef __cplusplus
}
#endif