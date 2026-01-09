/*
 * Module LVGL : menu + keypad + paramètres + Logs
 * CORRECTION : Ordre de déclaration des fonctions
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <string.h>
#include <stdio.h>

#include "lvgl.h" 
#include "network.h" 

/* ======= Paramètres ======= */
#define PIN_RESIDENT    "2580"
#define PIN_VISITEUR    "1234"
#define PIN_MAX_LEN     6
#define OPEN_MS_OK      3000
#define OPEN_MS_ERR     1500

/* ======= États d'écran ======= */
typedef enum {
    UI_SCREEN_MENU = 0,
    UI_SCREEN_KEYPAD_RESIDENT,
    UI_SCREEN_KEYPAD_VISITOR,
    UI_SCREEN_SETTINGS_MENU,
    UI_SCREEN_CARD_ADD_NAME,
    UI_SCREEN_CARD_ADD_WAIT,
    UI_SCREEN_LOGS
} ui_screen_t;

/* ======= Globals ======= */
static ui_state_cb_t g_state_cb = NULL;

static lv_obj_t *menu_cont;
static lv_obj_t *menu_title;
static lv_obj_t *btn_resident;
static lv_obj_t *btn_visiteur;

static bool g_is_editing = false;
static bool g_is_deleting = false;
static bool g_waiting_admin = false;
static char g_owner_name[64];

/* Keypad */
static lv_obj_t *keypad_cont;
static lv_obj_t *title_lbl;
static lv_obj_t *status_label;
static lv_obj_t *ta;          
static lv_obj_t *btnm;        
static lv_obj_t *btn_back;
static lv_obj_t *overlay;
static lv_obj_t *overlay_label;

/* Settings */
static lv_obj_t *settings_btn; 
static lv_obj_t *settings_cont;
static lv_obj_t *settings_title;
static lv_obj_t *btn_add_card;
static lv_obj_t *btn_remove_card;
static lv_obj_t *btn_edit_card;
static lv_obj_t *btn_settings_back;
static lv_obj_t *btn_logs;      

/* Sous-menus */
static lv_obj_t *card_name_cont;
static lv_obj_t *card_name_label;
static lv_obj_t *card_name_ta;
static lv_obj_t *card_name_kb;
static lv_obj_t *card_name_cancel_btn;
static lv_obj_t *btn_name_back;

static lv_obj_t *card_wait_cont;
static lv_obj_t *card_wait_label;
static lv_obj_t *card_wait_cancel_btn;
static lv_obj_t *btn_wait_back;

/* Logs */
static lv_obj_t *logs_cont;
static lv_obj_t *logs_title;
static lv_obj_t *logs_ta;       
static lv_obj_t *btn_logs_back;

/* Logique */
static ui_screen_t g_screen = UI_SCREEN_MENU;
static const struct device *g_display = NULL;
static int64_t overlay_hide_at = 0;
static bool overlay_was_success = false;

/* ======================================================= */
/* === DECLARATIONS ANTICIPEES (OBLIGATOIRES ICI) === */
/* ======================================================= */
static void show_menu(void);             
static void show_keypad_resident(void);  
static void show_keypad_visitor(void);   
static void show_settings_menu(void);    
static void show_card_add_name(void);    
static void show_card_add_wait(void);    
static void show_logs_screen(void);     

/* ======= Helper Notification Main ======= */
static void notify_main_app(void) {
    if (g_state_cb) {
        g_state_cb(); 
    }
}

void ui_set_state_change_cb(ui_state_cb_t cb) {
    g_state_cb = cb;
}

/* ======= Helpers UI ======= */
static void set_status(const char *txt) {
    if (status_label) lv_label_set_text(status_label, txt ? txt : "");
}

static void hide_all_main_views(void) {
    if (menu_cont)       lv_obj_add_flag(menu_cont, LV_OBJ_FLAG_HIDDEN);
    if (keypad_cont)     lv_obj_add_flag(keypad_cont, LV_OBJ_FLAG_HIDDEN);
    if (settings_cont)   lv_obj_add_flag(settings_cont, LV_OBJ_FLAG_HIDDEN);
    if (card_name_cont)  lv_obj_add_flag(card_name_cont, LV_OBJ_FLAG_HIDDEN);
    if (card_wait_cont)  lv_obj_add_flag(card_wait_cont, LV_OBJ_FLAG_HIDDEN);
    if (logs_cont)       lv_obj_add_flag(logs_cont, LV_OBJ_FLAG_HIDDEN);
}

static void show_overlay(bool show) {
    if (!overlay) return;
    if (show) lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

static void update_logs_text_content(void) {
    if (!logs_ta) return;
    char clear[128];
    char crypted[256];
    network_get_last_logs(clear, sizeof(clear), crypted, sizeof(crypted));
    char full_log[512];
    snprintf(full_log, sizeof(full_log), 
        "--- [DONNEES CLAIRES] ---\n%s\n\n"
        "--- [VUE SNIFFER (CABLE)] ---\n%s\n"
        "(Donnees chiffrees et signees)",
        clear, crypted);
    const char *current = lv_textarea_get_text(logs_ta);
    if (strcmp(current, full_log) != 0) {
        lv_textarea_set_text(logs_ta, full_log);
    }
}

static void show_result_overlay(bool ok) {
    overlay_was_success = ok;
    if (!overlay || !overlay_label) return;
    if (ok) {
        set_status("Acces autorise");
        lv_obj_set_style_bg_color(overlay, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_label_set_text(overlay_label, "OUVERT");
        overlay_hide_at = k_uptime_get() + OPEN_MS_OK;
    } else {
        set_status("Code incorrect");
        lv_obj_set_style_bg_color(overlay, lv_palette_main(LV_PALETTE_RED), 0);
        lv_label_set_text(overlay_label, "REFUSE");
        overlay_hide_at = k_uptime_get() + OPEN_MS_ERR;
    }
    show_overlay(true);
}

/* ======= Callbacks Keypad ======= */
static void handle_ok(void) {
    if (!ta) return;
    const char *entered = lv_textarea_get_text(ta);
    if (!entered) entered = "";
    const char *expected = NULL;
    if (g_screen == UI_SCREEN_KEYPAD_RESIDENT) expected = PIN_RESIDENT;
    else if (g_screen == UI_SCREEN_KEYPAD_VISITOR) expected = PIN_VISITEUR;
    else return;
    bool ok = (strcmp(entered, expected) == 0);
    lv_textarea_set_text(ta, "");
    show_result_overlay(ok);
}

static void btnm_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t id = lv_buttonmatrix_get_selected_button(obj);
    const char *txt = lv_buttonmatrix_get_button_text(obj, id);
    if (!txt) return;
    if (!strcmp(txt, "OK")) handle_ok();
    else if (!strcmp(txt, "DEL")) lv_textarea_delete_char(ta);
    else if (!strcmp(txt, "CLR")) lv_textarea_set_text(ta, "");
    else if (strlen(lv_textarea_get_text(ta)) < PIN_MAX_LEN) lv_textarea_add_text(ta, txt);
}

/* ======= Callbacks ======= */
/* Maintenant que les déclarations sont en haut, ceci fonctionnera */

static void btn_resident_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_keypad_resident(); }
static void btn_visiteur_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_keypad_visitor(); }
static void btn_back_cb(lv_event_t *e)     { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_menu(); }

static void settings_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_waiting_admin = true;
        if (card_wait_label) lv_label_set_text(card_wait_label, "ACCES PROTEGE\nBadge Admin Requis");
        show_card_add_wait();
    }
}
static void btn_settings_back_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_menu(); }
static void btn_logs_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_logs_screen(); }
static void btn_logs_back_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_settings_menu(); }

static void btn_add_card_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_is_editing = false; g_is_deleting = false;
        show_card_add_name();
    }
}
static void btn_remove_card_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_is_deleting = true; g_is_editing = false;
        if (card_wait_label) lv_label_set_text(card_wait_label, "SUPPRESSION:\nPassez la carte a retirer");
        show_card_add_wait();
    }
}
static void btn_edit_card_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_is_editing = true; g_is_deleting = false;
        if(card_name_label) lv_label_set_text(card_name_label, "Nouveau nom du proprietaire");
        show_card_add_name();
    }
}
static void card_name_cancel_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_settings_menu(); }
static void card_wait_cancel_cb(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_settings_menu(); }

static void handle_card_name_ok(void) {
    if (!card_name_ta) return;
    const char *txt = lv_textarea_get_text(card_name_ta);
    if (!txt || txt[0] == '\0') {
        lv_obj_set_style_border_color(card_name_ta, lv_palette_main(LV_PALETTE_RED), 0);
        return;
    }
    strncpy(g_owner_name, txt, sizeof(g_owner_name) - 1);
    g_owner_name[sizeof(g_owner_name) - 1] = '\0';
    if (card_wait_label) lv_label_set_text(card_wait_label, "ENREGISTREMENT:\nPassez la carte a ajouter");
    show_card_add_wait();
}

static void azerty_kb_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t id = lv_buttonmatrix_get_selected_button(kb);
    const char *txt = lv_buttonmatrix_get_button_text(kb, id);
    if (!txt || !card_name_ta) return;
    if (!strcmp(txt, "OK")) handle_card_name_ok();
    else if (!strcmp(txt, "DEL")) lv_textarea_delete_char(card_name_ta);
    else if (!strcmp(txt, "ESPACE")) lv_textarea_add_text(card_name_ta, " ");
    else lv_textarea_add_text(card_name_ta, txt);
}

/* ======= Creation UI ======= */

static void create_menu_ui(lv_obj_t *parent) {
    menu_cont = lv_obj_create(parent);
    lv_obj_set_size(menu_cont, 300, 220);
    lv_obj_align(menu_cont, LV_ALIGN_CENTER, 0, 0);

    menu_title = lv_label_create(menu_cont);
    lv_label_set_text(menu_title, "Choisissez le type d'entree");
    lv_obj_align(menu_title, LV_ALIGN_TOP_MID, 0, 4);

    btn_resident = lv_button_create(menu_cont);
    lv_obj_set_size(btn_resident, 200, 60);
    lv_obj_align(btn_resident, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn_resident, btn_resident_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_resident), "Resident");

    btn_visiteur = lv_button_create(menu_cont);
    lv_obj_set_size(btn_visiteur, 200, 60);
    lv_obj_align(btn_visiteur, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(btn_visiteur, btn_visiteur_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_visiteur), "Visiteur");
}

static void create_keypad_ui(lv_obj_t *parent) {
    keypad_cont = lv_obj_create(parent);
    lv_obj_set_size(keypad_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_align(keypad_cont, LV_ALIGN_CENTER, 0, 0);

    title_lbl = lv_label_create(keypad_cont);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 4);

    btn_back = lv_button_create(keypad_cont);
    lv_obj_set_size(btn_back, 55, 22);
    lv_obj_set_pos(btn_back, 4, 4);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_back), "<-");

    status_label = lv_label_create(keypad_cont);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 28);

    ta = lv_textarea_create(keypad_cont);
    lv_obj_set_width(ta, 120);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, PIN_MAX_LEN);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "••••");

    static const char *const btnm_map[] = { "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", "CLR", "0", "OK", "DEL", "" };
    btnm = lv_buttonmatrix_create(keypad_cont);
    lv_buttonmatrix_set_map(btnm, btnm_map);
    lv_obj_set_size(btnm, 220, 140);
    lv_obj_align(btnm, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btnm, btnm_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    overlay = lv_obj_create(parent);
    lv_obj_set_size(overlay, 200, 80);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    overlay_label = lv_label_create(overlay);
    lv_obj_center(overlay_label);
}

static void create_settings_ui(lv_obj_t *parent) {
    settings_btn = lv_button_create(parent);
    lv_obj_set_size(settings_btn, 45, 40); 
    lv_obj_align(settings_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_add_event_cb(settings_btn, settings_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_gear = lv_label_create(settings_btn);
    lv_label_set_text(lbl_gear, "OPTS"); 
    lv_obj_center(lbl_gear);

    settings_cont = lv_obj_create(parent);
    lv_obj_set_size(settings_cont, 300, 220);
    lv_obj_align(settings_cont, LV_ALIGN_CENTER, 0, 0);

    settings_title = lv_label_create(settings_cont);
    lv_label_set_text(settings_title, "Parametres cartes");
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 8);

    btn_settings_back = lv_button_create(settings_cont);
    lv_obj_set_size(btn_settings_back, 60, 30);
    lv_obj_set_pos(btn_settings_back, 0, 0); 
    lv_obj_add_event_cb(btn_settings_back, btn_settings_back_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_settings_back), "<-");

    btn_logs = lv_button_create(settings_cont);
    lv_obj_set_size(btn_logs, 40, 30);
    lv_obj_align(btn_logs, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(btn_logs, btn_logs_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_i = lv_label_create(btn_logs);
    lv_label_set_text(lbl_i, "i");
    lv_obj_center(lbl_i);

    btn_add_card = lv_button_create(settings_cont);
    lv_obj_set_size(btn_add_card, 220, 40);
    lv_obj_align(btn_add_card, LV_ALIGN_CENTER, 0, -35); 
    lv_obj_add_event_cb(btn_add_card, btn_add_card_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_add_card), "Ajouter une carte");

    btn_remove_card = lv_button_create(settings_cont);
    lv_obj_set_size(btn_remove_card, 220, 40);
    lv_obj_align(btn_remove_card, LV_ALIGN_CENTER, 0, 15); 
    lv_obj_add_event_cb(btn_remove_card, btn_remove_card_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_remove_card), "Retirer une carte");

    btn_edit_card = lv_button_create(settings_cont);
    lv_obj_set_size(btn_edit_card, 220, 40);
    lv_obj_align(btn_edit_card, LV_ALIGN_CENTER, 0, 65); 
    lv_obj_add_event_cb(btn_edit_card, btn_edit_card_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_edit_card), "Modifier une carte");

    card_name_cont = lv_obj_create(parent);
    lv_obj_set_size(card_name_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_align(card_name_cont, LV_ALIGN_CENTER, 0, 0);
    card_name_label = lv_label_create(card_name_cont);
    lv_obj_align(card_name_label, LV_ALIGN_TOP_MID, 0, 4);
    
    btn_name_back = lv_button_create(card_name_cont);
    lv_obj_set_size(btn_name_back, 60, 30);
    lv_obj_set_pos(btn_name_back, 4, 4);
    lv_obj_add_event_cb(btn_name_back, card_name_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_name_back), "<-");

    card_name_ta = lv_textarea_create(card_name_cont);
    lv_obj_set_width(card_name_ta, 220);
    lv_obj_align(card_name_ta, LV_ALIGN_TOP_MID, 0, 40);
    lv_textarea_set_one_line(card_name_ta, true);
    card_name_cancel_btn = lv_button_create(card_name_cont);
    lv_obj_set_size(card_name_cancel_btn, 80, 26);
    lv_obj_align(card_name_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(card_name_cancel_btn, card_name_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(card_name_cancel_btn), "Annuler");

    static const char *const azerty_map[] = {
        "A","Z","E","R","T","Y","U","I","O","P","\n",
        "Q","S","D","F","G","H","J","K","L","M","\n",
        "W","X","C","V","B","N","-","_","\n",
        "ESPACE","DEL","OK",""
    };
    card_name_kb = lv_buttonmatrix_create(card_name_cont);
    lv_buttonmatrix_set_map(card_name_kb, azerty_map);
    lv_obj_set_size(card_name_kb, LV_PCT(100), 150);
    lv_obj_align(card_name_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(card_name_kb, azerty_kb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    card_wait_cont = lv_obj_create(parent);
    lv_obj_set_size(card_wait_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_align(card_wait_cont, LV_ALIGN_CENTER, 0, 0);
    card_wait_label = lv_label_create(card_wait_cont);
    lv_label_set_text(card_wait_label, "Veuillez passer la carte...");
    lv_obj_align(card_wait_label, LV_ALIGN_CENTER, 0, -10);
    
    btn_wait_back = lv_button_create(card_wait_cont);
    lv_obj_set_size(btn_wait_back, 60, 30);
    lv_obj_set_pos(btn_wait_back, 4, 4);
    lv_obj_add_event_cb(btn_wait_back, card_wait_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_wait_back), "<-");

    card_wait_cancel_btn = lv_button_create(card_wait_cont);
    lv_obj_set_size(card_wait_cancel_btn, 80, 26);
    lv_obj_align(card_wait_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(card_wait_cancel_btn, card_wait_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(card_wait_cancel_btn), "Annuler");
    
    logs_cont = lv_obj_create(parent);
    lv_obj_set_size(logs_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_align(logs_cont, LV_ALIGN_CENTER, 0, 0);
    
    logs_title = lv_label_create(logs_cont);
    lv_label_set_text(logs_title, "Inspecteur de Trafic");
    lv_obj_align(logs_title, LV_ALIGN_TOP_MID, 0, 4);
    
    logs_ta = lv_textarea_create(logs_cont);
    lv_obj_set_size(logs_ta, 400, 200); 
    lv_obj_align(logs_ta, LV_ALIGN_CENTER, 0, 15);
    lv_obj_clear_flag(logs_ta, LV_OBJ_FLAG_CLICKABLE); 

    btn_logs_back = lv_button_create(logs_cont);
    lv_obj_set_size(btn_logs_back, 60, 30);
    lv_obj_set_pos(btn_logs_back, 4, 4);
    lv_obj_add_event_cb(btn_logs_back, btn_logs_back_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_logs_back), "<-");

    /* Hide All */
    lv_obj_add_flag(settings_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card_name_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(card_wait_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(logs_cont, LV_OBJ_FLAG_HIDDEN);
}

/* ======= Transitions ======= */

static void show_menu(void) {
    g_screen = UI_SCREEN_MENU;
    hide_all_main_views();
    if (menu_cont) lv_obj_clear_flag(menu_cont, LV_OBJ_FLAG_HIDDEN);
    if (ta) lv_textarea_set_text(ta, "");
    set_status("");
    show_overlay(false);
    notify_main_app();
}

static void show_keypad_common(const char *title_text) {
    if (strcmp(title_text, "Entree resident") == 0) g_screen = UI_SCREEN_KEYPAD_RESIDENT;
    else g_screen = UI_SCREEN_KEYPAD_VISITOR;
    hide_all_main_views();
    if (keypad_cont) lv_obj_clear_flag(keypad_cont, LV_OBJ_FLAG_HIDDEN);
    if (title_lbl) lv_label_set_text(title_lbl, title_text);
    set_status("Entrer le code");
    if (ta) lv_textarea_set_text(ta, "");
    show_overlay(false);
    notify_main_app();
}

static void show_keypad_resident(void) { show_keypad_common("Entree resident"); }
static void show_keypad_visitor(void)  { show_keypad_common("Entree visiteur"); }

static void show_settings_menu(void) {
    g_screen = UI_SCREEN_SETTINGS_MENU;
    hide_all_main_views();
    if (settings_cont) lv_obj_clear_flag(settings_cont, LV_OBJ_FLAG_HIDDEN);
    g_is_editing = false; g_is_deleting = false; g_waiting_admin = false;
    notify_main_app();
}

static void show_card_add_name(void) {
    g_screen = UI_SCREEN_CARD_ADD_NAME;
    hide_all_main_views();
    if (card_name_cont) lv_obj_clear_flag(card_name_cont, LV_OBJ_FLAG_HIDDEN);
    if (card_name_ta) {
        lv_textarea_set_text(card_name_ta, "");
        lv_obj_set_style_border_color(card_name_ta, lv_color_black(), 0);
    }
    notify_main_app();
}

static void show_card_add_wait(void) {
    g_screen = UI_SCREEN_CARD_ADD_WAIT;
    hide_all_main_views();
    if (card_wait_cont) lv_obj_clear_flag(card_wait_cont, LV_OBJ_FLAG_HIDDEN);
    notify_main_app();
}

static void show_logs_screen(void) {
    g_screen = UI_SCREEN_LOGS;
    hide_all_main_views();
    update_logs_text_content(); 
    if (logs_cont) lv_obj_clear_flag(logs_cont, LV_OBJ_FLAG_HIDDEN);
    notify_main_app();
}

static void ui_timer_tick_internal(void) {
    int64_t now = k_uptime_get();
    
    if (overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN) &&
        overlay_hide_at != 0 && now >= overlay_hide_at) {
        show_overlay(false);
        overlay_hide_at = 0;
        if (overlay_was_success) show_menu();
        else set_status("Entrer le code");
    }

    static int64_t last_log_refresh = 0;
    if (g_screen == UI_SCREEN_LOGS) {
        if (now - last_log_refresh > 500) {
            update_logs_text_content();
            last_log_refresh = now;
        }
    }
}

/* ======= API Publique ======= */

int ui_init(const struct device *display) {
    if (!device_is_ready(display)) return -ENODEV;
    g_display = display;
    display_blanking_off(display);
    lv_obj_t *scr = lv_scr_act();
    create_menu_ui(scr);
    create_keypad_ui(scr);
    create_settings_ui(scr);
    show_menu();
    return 0;
}

void ui_tick(void) {
    lv_timer_handler();
    ui_timer_tick_internal();
}

void ui_show_prompt(void) { show_menu(); }
bool ui_is_scan_mode_active(void) { return (g_screen == UI_SCREEN_CARD_ADD_WAIT && !g_waiting_admin); }
const char* ui_get_scanned_name(void) { return g_owner_name; }
void ui_go_back_to_settings(void) { show_settings_menu(); }
bool ui_is_edit_mode(void) { return g_is_editing; }
bool ui_is_delete_mode(void) { return g_is_deleting; }
bool ui_is_admin_auth_mode(void) { return (g_screen == UI_SCREEN_CARD_ADD_WAIT && g_waiting_admin); }
void ui_open_settings(void) { g_waiting_admin = false; show_settings_menu(); }