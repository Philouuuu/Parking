#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/random/random.h>
#include <tinycrypt/sha256.h>
#include <tinycrypt/hmac.h>
#include "network.h"

/* --- CONFIGURATION MQTT --- */
#define BROKER_IP       "192.168.137.2"
#define BROKER_PORT     1883
#define TOPIC_CMD       "parking/cmd"
#define TOPIC_RESP      "parking/resp"
#define TOPIC_WEATHER   "parking/weather"
#define MQTT_TIMEOUT    2000 

#define SECRET_KEY      "PARKING_SECRET_KEY_2025"

/* Buffers MQTT */
static uint8_t rx_buffer[1024];
static uint8_t tx_buffer[1024];
static struct mqtt_client client_ctx;
static struct sockaddr_in broker;
static struct pollfd fds[1];
static int nfds;

/* Gestion Sync */
static struct k_sem response_sem; 
static char received_payload_buf[512]; 
static char last_weather_buf[256] = "EN ATTENTE SERVEUR...";
static bool new_weather = false;

/* Crypto vars */
static uint32_t packet_counter = 0;
static uint32_t session_id = 0;
static char inner_payload[256]; 
static char signature_hex[65];
static uint8_t sig_raw[32];
static char full_block_to_encrypt[350]; 
static char encrypted_hex[700]; 
static char final_packet[800];  
static char seed[64];
static uint8_t mask[32];

/* Logs UI */
static char log_last_clear[128] = "Start MQTT";
static char log_last_crypted[256] = "...";

/* --- OUTILS CRYPTO (Inchangés) --- */
static void calculate_hmac(const char *key, const char *data, uint8_t *digest_out) {
    struct tc_hmac_state_struct h;
    memset(&h, 0x00, sizeof(h));
    tc_hmac_set_key(&h, (uint8_t *)key, strlen(key));
    tc_hmac_init(&h);
    tc_hmac_update(&h, (uint8_t *)data, strlen(data));
    tc_hmac_final(digest_out, 32, &h);
}

void generate_mask(uint32_t iv) {
    snprintf(seed, sizeof(seed), "IV_SEED:%u", iv);
    calculate_hmac(SECRET_KEY, seed, mask);
}

void xor_cipher_iv(const char *input, char *output_hex, uint32_t iv) {
    generate_mask(iv);
    int len = strlen(input);
    for(int i=0; i<len; i++) {
        uint8_t val = input[i] ^ mask[i % 32];
        sprintf(&output_hex[i*2], "%02x", val);
    }
    output_hex[len*2] = '\0';
}

uint8_t hex2int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

void xor_decrypt_iv(const char *hex_input, char *output_str, uint32_t iv) {
    generate_mask(iv);
    int len = strlen(hex_input) / 2; 
    for(int i=0; i<len; i++) {
        uint8_t byte_val = (hex2int(hex_input[i*2]) << 4) | hex2int(hex_input[i*2 + 1]);
        char decoded_char = byte_val ^ mask[i % 32];
        output_str[i] = decoded_char;
    }
    output_str[len] = '\0';
}

/* --- GESTION MQTT --- */

void mqtt_evt_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_PUBLISH: {
        const struct mqtt_publish_param *p = &evt->param.publish;
        int len = p->message.payload.len;
        if (len >= sizeof(received_payload_buf)) len = sizeof(received_payload_buf) - 1;
        
        if (mqtt_read_publish_payload(client, received_payload_buf, len) >= 0) {
            received_payload_buf[len] = '\0';
            if (strncmp(p->message.topic.topic.utf8, TOPIC_RESP, p->message.topic.topic.size) == 0) {
                k_sem_give(&response_sem);
            } 
            else if (strncmp(p->message.topic.topic.utf8, TOPIC_WEATHER, p->message.topic.topic.size) == 0) {
                strncpy(last_weather_buf, received_payload_buf, sizeof(last_weather_buf)-1);
                new_weather = true;
            }
        }
        break;
    }
    case MQTT_EVT_CONNACK:
        printk("MQTT Connecte !\n");
        break;
    case MQTT_EVT_DISCONNECT:
        printk("MQTT Deconnecte !\n");
        break;
    default:
        break;
    }
}

static void prepare_fds(struct mqtt_client *client) {
    if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
        fds[0].fd = client->transport.tcp.sock;
    }
    fds[0].events = ZSOCK_POLLIN;
    nfds = 1;
}

/* --- LOGIQUE DE CONNEXION ROBUSTE --- */
// Cette fonction boucle TANT QUE la connexion n'est pas établie
static void connect_to_broker_loop(void) {
    int rc;
    
    // Si une connexion existait malproprement, on tente de fermer le socket
    if (client_ctx.transport.tcp.sock >= 0) {
        zsock_close(client_ctx.transport.tcp.sock);
    }
    
    printk(">>> Recherche du Serveur BeagleBone (%s)...\n", BROKER_IP);
    
    while (true) {
        // On doit réinitialiser le client à chaque tentative
        mqtt_client_init(&client_ctx);
        client_ctx.broker = &broker;
        client_ctx.evt_cb = mqtt_evt_handler;
        client_ctx.client_id.utf8 = (uint8_t *)"zephyr_parking_client";
        client_ctx.client_id.size = strlen("zephyr_parking_client");
        client_ctx.protocol_version = MQTT_VERSION_3_1_1;
        client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;
        client_ctx.rx_buf = rx_buffer;
        client_ctx.rx_buf_size = sizeof(rx_buffer);
        client_ctx.tx_buf = tx_buffer;
        client_ctx.tx_buf_size = sizeof(tx_buffer);

        rc = mqtt_connect(&client_ctx);
        if (rc == 0) {
            // Attente active de l'évènement CONNACK
            prepare_fds(&client_ctx);
            if (zsock_poll(fds, 1, 2000) > 0) {
                mqtt_input(&client_ctx); // Traite le CONNACK
                printk(">>> SUCCES : Connexion etablie !\n");
                
                // CRUCIAL : On doit se réabonner après chaque connexion
                struct mqtt_topic topics[2];
                struct mqtt_subscription_list sub_list;

                topics[0].topic.utf8 = TOPIC_RESP;
                topics[0].topic.size = strlen(TOPIC_RESP);
                topics[0].qos = MQTT_QOS_0_AT_MOST_ONCE;
                
                topics[1].topic.utf8 = TOPIC_WEATHER;
                topics[1].topic.size = strlen(TOPIC_WEATHER);
                topics[1].qos = MQTT_QOS_0_AT_MOST_ONCE;

                sub_list.list = topics;
                sub_list.list_count = 2;
                sub_list.message_id = 1234;

                mqtt_subscribe(&client_ctx, &sub_list);
                return; // Sortie de la boucle, on est connecté
            }
        }
        
        printk("... Serveur introuvable. Nouvelle tentative dans 2s...\n");
        k_msleep(2000);
    }
}

void network_init(void) {
    k_sem_init(&response_sem, 0, 1);
    session_id = sys_rand32_get();

    broker.sin_family = AF_INET;
    broker.sin_port = htons(BROKER_PORT);
    inet_pton(AF_INET, BROKER_IP, &broker.sin_addr);

    // Premier lancement : on boucle jusqu'à trouver le serveur
    connect_to_broker_loop();
}

void network_poll(void) {
    prepare_fds(&client_ctx);
    
    // On vérifie les messages entrants
    if (zsock_poll(fds, 1, 0) > 0) { 
        int rc = mqtt_input(&client_ctx);
        if (rc != 0) {
            printk("Erreur MQTT Input (%d) -> Reconnexion...\n", rc);
            connect_to_broker_loop(); // Reconnexion bloquante
            return;
        }
    }
    
    // On envoie un PING (Keep Alive) et on vérifie si le serveur est toujours là
    int rc = mqtt_live(&client_ctx);
    if (rc != 0 && rc != -EAGAIN) {
        printk("Erreur MQTT Live/Ping (%d) -> Reconnexion...\n", rc);
        connect_to_broker_loop(); // Reconnexion bloquante
    }
}

bool network_get_weather(char *buf_out, size_t max_len) {
    if (new_weather) {
        strncpy(buf_out, last_weather_buf, max_len);
        new_weather = false;
        return true;
    }
    return false;
}

/* --- LOGIQUE TRANSACTIONNELLE --- */
int network_transaction(const char *msg_out, char *buf_in, size_t max_len) {
    // Si on est déconnecté avant d'envoyer, on tente de se reconnecter d'abord
    network_poll(); 

    packet_counter++;
    uint32_t iv = sys_rand32_get(); 

    /* 1. CONSTRUCTION */
    snprintf(inner_payload, sizeof(inner_payload), "%s;C=%u;S=%u", msg_out, packet_counter, session_id);
    calculate_hmac(SECRET_KEY, inner_payload, sig_raw);
    for (int i = 0; i < 32; i++) sprintf(&signature_hex[i * 2], "%02x", sig_raw[i]);
    snprintf(full_block_to_encrypt, sizeof(full_block_to_encrypt), "%s|%s", inner_payload, signature_hex);
    
    xor_cipher_iv(full_block_to_encrypt, encrypted_hex, iv);
    snprintf(final_packet, sizeof(final_packet), "IV=%u|%s", iv, encrypted_hex);

    /* LOGS UI */
    strncpy(log_last_clear, msg_out, sizeof(log_last_clear) - 1);
    strncpy(log_last_crypted, final_packet, sizeof(log_last_crypted) - 1);

    /* 2. ENVOI MQTT */
    struct mqtt_publish_param param;
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.message.topic.topic.utf8 = (uint8_t *)TOPIC_CMD;
    param.message.topic.topic.size = strlen(TOPIC_CMD);
    param.message.payload.data = final_packet;
    param.message.payload.len = strlen(final_packet);
    param.message_id = sys_rand32_get();
    param.dup_flag = 0;
    param.retain_flag = 0;

    k_sem_reset(&response_sem);

    if (mqtt_publish(&client_ctx, &param) != 0) {
        printk("Echec Envoi -> Reconnexion...\n");
        connect_to_broker_loop(); // Reconnexion si l'envoi échoue
        return -1;
    }

    /* 3. ATTENTE REPONSE */
    int waited = 0;
    while (waited < MQTT_TIMEOUT) {
        network_poll(); 
        if (k_sem_take(&response_sem, K_NO_WAIT) == 0) {
            goto process_response;
        }
        k_msleep(50);
        waited += 50;
    }
    return -1; 

process_response:
    /* 4. DECRYPTAGE */
    xor_decrypt_iv(received_payload_buf, full_block_to_encrypt, iv); 
    
    char *pipe_ptr = strchr(full_block_to_encrypt, '|');
    if (pipe_ptr) {
        *pipe_ptr = '\0'; 
        char *msg_clair = full_block_to_encrypt;
        char *rec_sig = pipe_ptr + 1;
        
        calculate_hmac(SECRET_KEY, msg_clair, sig_raw);
        for (int i = 0; i < 32; i++) sprintf(&signature_hex[i * 2], "%02x", sig_raw[i]);
        
        if (strcmp(signature_hex, rec_sig) == 0) {
            strncpy(buf_in, msg_clair, max_len);
            return strlen(buf_in);
        }
    }
    return -1;
}

// Les wrappers restent identiques...
int network_send_check(const char *uid, char *name_out, size_t max_len) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "CHECK;%s", uid);
    if (network_transaction(buffer, name_out, max_len) > 0) {
        char *ok_ptr = strstr(name_out, ";OK;");
        if (ok_ptr) {
            char *nom_seul = ok_ptr + 4;
            memmove(name_out, nom_seul, strlen(nom_seul) + 1);
            return 1;
        }
    }
    return 0; 
}
int network_send_edit(const char *uid, const char *name) {
    char b[100], r[100]; snprintf(b, sizeof(b), "EDIT;%s;%s", uid, name);
    if (network_transaction(b, r, sizeof(r)) > 0) return (strstr(r, ";OK") != NULL);
    return 0;
}
int network_send_add(const char *uid, const char *name) {
    char b[100], r[100]; snprintf(b, sizeof(b), "ADD;%s;%s", uid, name); 
    if (network_transaction(b, r, sizeof(r)) > 0) return (strstr(r, ";OK") != NULL);
    return 0;
}
int network_send_del(const char *uid) {
    char b[100], r[100]; snprintf(b, sizeof(b), "DEL;%s", uid); 
    if (network_transaction(b, r, sizeof(r)) > 0) return (strstr(r, ";OK") != NULL);
    return 0;
}
void network_get_last_logs(char *clear_buf, size_t clear_len, char *crypted_buf, size_t crypted_len) {
    strncpy(clear_buf, log_last_clear, clear_len);
    strncpy(crypted_buf, log_last_crypted, crypted_len);
}