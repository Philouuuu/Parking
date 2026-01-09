#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/storage/disk_access.h>
#include <stdio.h>
#include <string.h>
#include "storage.h"

#define DISK_NAME   "SD"
#define MOUNT_POINT "/SD:"
#define DB_FILE     MOUNT_POINT "/db.txt"

static struct fs_mount_t fs_mnt;

int storage_init(void)
{
    int rc = -1;

    /* Petite pause au démarrage pour laisser la SD s'alimenter */
    k_msleep(100);

    /* Tentative d'init bas niveau (3 essais) */
    for (int i = 0; i < 3; i++) {
        rc = disk_access_init(DISK_NAME);
        if (rc == 0) break;
        printk("Attente SD (%d/3)...\n", i+1);
        k_msleep(200);
    }

    if (rc != 0) {
        printk("ERR: disk_access_init echoue (%d)\n", rc);
        return rc;
    }

    fs_mnt.type = FS_FATFS;
    fs_mnt.fs_data = NULL;
    fs_mnt.storage_dev = (void *)DISK_NAME;
    fs_mnt.mnt_point = MOUNT_POINT;

    rc = fs_mount(&fs_mnt);
    if (rc != 0) {
        printk("ERR: fs_mount echoue (%d)\n", rc);
        /* Si erreur, on ne crash pas, on retourne juste l'erreur */
        return rc;
    }

    printk("SD montee: %s\n", MOUNT_POINT);
    return 0;
}

int storage_add_card(const char *uid, const char *name)
{
    struct fs_file_t file;
    fs_file_t_init(&file);

    /* Buffer un peu plus grand pour éviter les débordements */
    char line[256]; 

    int rc = fs_open(&file, DB_FILE, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (rc < 0) {
        printk("FS: Err open write (%d)\n", rc);
        return rc;
    }

    snprintf(line, sizeof(line), "%s;%s\n", uid, name);
    fs_write(&file, line, strlen(line));
    fs_close(&file);
    
    printk("FS: Ecrit -> %s", line);
    return 0;
}

int storage_find_name(const char *target_uid, char *name_buf, size_t max_len)
{
    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, DB_FILE, FS_O_READ);
    if (rc < 0) return -1;

    char buffer[128];
    int pos = 0;
    char c;
    int found = -1;

    while (fs_read(&file, &c, 1) > 0) {
        if (c == '\n' || pos >= sizeof(buffer) - 1) {
            buffer[pos] = '\0';
            char *sep = strchr(buffer, ';');
            if (sep) {
                *sep = '\0';
                char *f_uid = buffer;
                char *f_name = sep + 1;
                /* clean \r pour windows compat */
                char *cr = strchr(f_name, '\r'); if(cr) *cr='\0';

                if (strcmp(f_uid, target_uid) == 0) {
                    strncpy(name_buf, f_name, max_len);
                    name_buf[max_len - 1] = '\0';
                    found = 0;
                    break;
                }
            }
            pos = 0;
        } else {
            buffer[pos++] = c;
        }
    }
    fs_close(&file);
    return found;
}