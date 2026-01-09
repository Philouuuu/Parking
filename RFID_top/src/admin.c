#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <ff.h>  
#include <stdio.h>
#include <string.h>
#include "admin.h"

/* ATTENTION : Même clé que dans le script Python ! */
#define SECRET_KEY "PARKING_SECURE_KEY"

#define MOUNT_POINT "/SD:"
#define ADMIN_FILE  MOUNT_POINT "/admin.csv"

static FATFS fat_fs;
static struct fs_mount_t fs_mnt = {
    .type = FS_FATFS,
    .mnt_point = MOUNT_POINT,
    .fs_data = &fat_fs,
    .storage_dev = (void *)"SD",
};

static bool sd_mounted = false;

/* --- Fonction Helper pour chiffrer comme le Python --- */
void encrypt_uid_in_place(const char *input_uid, char *output_hex_str)
{
    const char *key = SECRET_KEY;
    int key_len = strlen(key);
    int input_len = strlen(input_uid);
    
    /* Le buffer de sortie est rempli petit à petit */
    output_hex_str[0] = '\0'; 
    char temp[4];

    for (int i = 0; i < input_len; i++) {
        /* XOR entre l'UID et la Clé */
        unsigned char encrypted_char = input_uid[i] ^ key[i % key_len];
        
        /* Conversion en HEX (2 caractères) et ajout à la suite */
        sprintf(temp, "%02X", encrypted_char);
        strcat(output_hex_str, temp);
    }
}

/* Initialisation (Version propre) */
int admin_init_sd(void)
{
    if (sd_mounted) return 0;

    printk("SD: Init Hardware...\n");
    k_msleep(50);

    if (disk_access_init("SD") != 0) {
        printk("SD: Erreur Hardware (disk_access)\n");
        return -1;
    }

    int res = fs_mount(&fs_mnt);
    if (res != 0) {
        printk("SD: Erreur Mount (%d)\n", res);
        return res;
    }
    
    sd_mounted = true;
    printk("SD: Montage REUSSI.\n");
    return 0;
}

/* Vérification UID Sécurisée */
bool admin_check_uid(const char *uid_input)
{
    /* On tente l'init si pas encore fait */
    if (!sd_mounted) {
        if (admin_init_sd() != 0) return false;
    }

    /* 1. On crypte l'UID qu'on vient de scanner pour voir à quoi il doit ressembler */
    char encrypted_target[64];
    encrypt_uid_in_place(uid_input, encrypted_target);
    
    printk("SD: UID Scanne: %s -> Cherche Hash: [%s]\n", uid_input, encrypted_target);

    struct fs_file_t file;
    fs_file_t_init(&file);

    if (fs_open(&file, ADMIN_FILE, FS_O_READ) != 0) {
        printk("SD: Impossible d'ouvrir %s\n", ADMIN_FILE);
        return false;
    }

    char line[128]; // Un peu plus grand car le hex prend 2x plus de place
    char c;
    int pos = 0;
    bool found = false;

    /* Lecture du fichier */
    while (fs_read(&file, &c, 1) > 0) {
        if (c == '\n' || pos >= sizeof(line) - 1) {
            line[pos] = '\0';
            
            /* Nettoyage CR/LF et découpage */
            char *cr = strchr(line, '\r'); if (cr) *cr = '\0';
            
            /* On cherche le séparateur (soit , soit ;) */
            char *sep = strchr(line, ','); 
            if (!sep) sep = strchr(line, ';');
            
            if (sep) *sep = '\0'; // Coupe la ligne pour isoler l'UID crypté

            /* Comparaison : On compare le CRYPTÉ scanné avec le CRYPTÉ du fichier */
            if (strcmp(line, encrypted_target) == 0) {
                found = true;
                break;
            }
            pos = 0;
        } else {
            if (c != '\r') line[pos++] = c;
        }
    }

    fs_close(&file);
    return found;
}