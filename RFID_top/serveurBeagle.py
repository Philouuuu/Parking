import socket
import csv
import os
import hmac
import hashlib
import binascii
import time
import threading
import datetime
import requests # <--- Pour la météo
import paho.mqtt.client as mqtt

# --- CONFIGURATION DU SYSTEME ---
BROKER_ADDRESS = "127.0.0.1"
PORT = 1883

# Topics MQTT
TOPIC_CMD = "parking/cmd"
TOPIC_RESP = "parking/resp"
TOPIC_WEATHER = "parking/weather"

# Fichiers et Clés
DB_FILE = "base_donnees.csv"
SECRET_KEY = b"PARKING_SECRET_KEY_2025"

# Config Parking & Météo
MAX_PLACES = 10  # <--- Change ici la capacité totale de ton parking
API_KEY = "7607347c68bd81aaaf533572f02501eb"
CITY = "Lyon"

# --- INITIALISATION CSV ---
if not os.path.exists(DB_FILE):
    with open(DB_FILE, 'w', newline='') as f:
        csv.writer(f).writerow(["UID", "NOM"])

print(f"=== SERVEUR PARKING MQTT + METEO DEMARRE ===")

# =========================================================
# 1. HELPER METEO (Ton code original)
# =========================================================
def get_weather_desc(weather_id):
    if 200 <= weather_id <= 232: return "ORAGE"
    if 300 <= weather_id <= 321: return "BRUINE"
    if 500 <= weather_id <= 531: return "PLUIE"
    if 600 <= weather_id <= 622: return "NEIGE"
    if 701 <= weather_id <= 781: return "BRUME"
    if weather_id == 800:        return "SOLEIL"
    if weather_id > 800:         return "NUAGES"
    return "VARIABLE"

# =========================================================
# 2. FONCTIONS METIERS (Gestion Badges)
# =========================================================
def verifier_carte(uid):
    if not os.path.exists(DB_FILE): return None
    with open(DB_FILE, 'r') as f:
        for row in csv.reader(f):
            if len(row) >= 2 and row[0] == uid: return row[1]
    return None

def ajouter_carte(uid, nom):
    if verifier_carte(uid): return False
    with open(DB_FILE, 'a', newline='') as f:
        csv.writer(f).writerow([uid, nom])
    return True

def supprimer_carte(uid):
    l = []
    with open(DB_FILE, 'r') as f: l = list(csv.reader(f))
    n = [r for r in l if len(r) < 1 or r[0] != uid]
    if len(n) < len(l):
        with open(DB_FILE, 'w', newline='') as f:
            csv.writer(f).writerows(n)
        return True
    return False

def modifier_carte(uid, nom):
    l = []
    trouve = False
    with open(DB_FILE, 'r') as f: l = list(csv.reader(f))
    for r in l:
        if len(r) >= 2 and r[0] == uid:
            r[1] = nom
            trouve = True
    if trouve:
        with open(DB_FILE, 'w', newline='') as f:
            csv.writer(f).writerows(l)
    return trouve

# =========================================================
# 3. FONCTIONS CRYPTO
# =========================================================
def get_mask(iv_val):
    seed = f"IV_SEED:{iv_val}"
    return hmac.new(SECRET_KEY, seed.encode('utf-8'), hashlib.sha256).digest()

def xor_decrypt(hex_input, iv_val):
    try:
        mask = get_mask(iv_val)
        encrypted_bytes = binascii.unhexlify(hex_input)
        decrypted_chars = []
        mask_len = len(mask)
        for i in range(len(encrypted_bytes)):
            val = encrypted_bytes[i] ^ mask[i % mask_len]
            decrypted_chars.append(chr(val))
        return "".join(decrypted_chars)
    except: return None

def xor_encrypt(plain_text, iv_val):
    try:
        mask = get_mask(iv_val)
        encrypted_hex = []
        mask_len = len(mask)
        input_bytes = plain_text.encode('utf-8')
        for i in range(len(input_bytes)):
            val = input_bytes[i] ^ mask[i % mask_len]
            encrypted_hex.append(f"{val:02x}")
        return "".join(encrypted_hex)
    except: return ""

# =========================================================
# 4. THREAD D'INFORMATION (BOUCLE PRINCIPALE D'AFFICHAGE)
# =========================================================
def info_loop(client):
    """Gère l'Heure, la Météo API et le calcul des Places"""
    while True:
        try:
            # --- A. RECUPERATION HEURE ---
            now = datetime.datetime.now()
            heure_fmt = now.strftime("%H:%M")

            # --- B. RECUPERATION METEO (API) ---
            temp_str = "??"
            desc_str = "OFFLINE"

            try:
                url = f"http://api.openweathermap.org/data/2.5/weather?q={CITY}&appid={API_KEY}&units=metric"
                r = requests.get(url, timeout=5)
                if r.status_code == 200:
                    data = r.json()
                    temp_val = int(data['main']['temp'])
                    w_id = data['weather'][0]['id']

                    temp_str = f"{temp_val}"
                    desc_str = get_weather_desc(w_id)
            except Exception as e_meteo:
                print(f"[METEO ERR] {e_meteo}")

            # --- C. CALCUL DES PLACES (REEL) ---
            nb_inscrits = 0
            if os.path.exists(DB_FILE):
                with open(DB_FILE, 'r') as f:
                    # On compte les lignes moins l'entête
                    lines = f.readlines()
                    if len(lines) > 0:
                        nb_inscrits = len(lines) - 1

            if nb_inscrits < 0: nb_inscrits = 0
            places_libres = MAX_PLACES - nb_inscrits
            if places_libres < 0: places_libres = 0 # Sécurité si plus d'inscrits que de places

            # --- D. CONSTRUCTION & ENVOI MESSAGE ---
            # Format : "14:30 | LYON 24C SOLEIL | P:08/10"
            msg_final = f"{heure_fmt} | {CITY.upper()} {temp_str}C {desc_str} | P:{places_libres:02d}/{MAX_PLACES}"

            print(f"[INFO] Update: {msg_final}")
            client.publish(TOPIC_WEATHER, msg_final, retain=True)

        except Exception as e:
            print(f"Erreur Loop Info: {e}")

        # Pause de 30 secondes avant la prochaine mise à jour
        time.sleep(30)

# =========================================================
# 5. GESTION MQTT (CALLBACKS)
# =========================================================
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("Connecté au Broker MQTT !")
        client.subscribe(TOPIC_CMD)
    else:
        print(f"Echec connexion, code={rc}")

def on_message(client, userdata, msg):
    try:
        raw_msg = msg.payload.decode('utf-8').strip()
        print(f"\n[RECU CRYPTE] {raw_msg}")

        if "|" in raw_msg and raw_msg.startswith("IV="):
            parts = raw_msg.split("|")
            current_iv = int(parts[0].split("=")[1])
            encrypted_blob = parts[1]

            decrypted_blob = xor_decrypt(encrypted_blob, current_iv)

            if decrypted_blob and "|" in decrypted_blob:
                inner_payload, received_sig = decrypted_blob.split("|")
                comp_sig = hmac.new(SECRET_KEY, inner_payload.encode(), hashlib.sha256).hexdigest()

                if hmac.compare_digest(comp_sig, received_sig):
                    print(f"[DECRYPT CLAIR] {inner_payload}")

                    msg_parts = inner_payload.split(";")
                    cmd = msg_parts[0]
                    reponse_claire = "FAIL"

                    if cmd == "CHECK":
                        nom = verifier_carte(msg_parts[1])
                        reponse_claire = f"CHECK;{msg_parts[1]};OK;{nom}" if nom else f"CHECK;{msg_parts[1]};NO"
                    elif cmd == "ADD":
                        if len(msg_parts) > 2 and ajouter_carte(msg_parts[1], msg_parts[2]):
                            reponse_claire = f"ADD;{msg_parts[1]};OK"
                    elif cmd == "DEL":
                        if supprimer_carte(msg_parts[1]):
                            reponse_claire = f"DEL;{msg_parts[1]};OK"
                    elif cmd == "EDIT":
                        if len(msg_parts) > 2 and modifier_carte(msg_parts[1], msg_parts[2]):
                            reponse_claire = f"EDIT;{msg_parts[1]};OK"

                    resp_sig = hmac.new(SECRET_KEY, reponse_claire.encode(), hashlib.sha256).hexdigest()
                    resp_full = f"{reponse_claire}|{resp_sig}"
                    resp_crypted = xor_encrypt(resp_full, current_iv)

                    client.publish(TOPIC_RESP, resp_crypted)
                    print(f"[REPONSE ENVOYEE] {resp_crypted}")

                    # PETITE ASTUCE : Si on ajoute/supprime quelqu'un,
                    # on peut forcer la mise à jour des places immédiatement (optionnel)
                    # mais le thread le fera sous 30 sec max.

                else:
                    print("Signature invalide")
            else:
                print("Echec decryptage")
    except Exception as e:
        print(f"Erreur process: {e}")

# =========================================================
# 6. MAIN
# =========================================================

# Utilisation de la Version 2 pour éviter le warning Deprecation
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(BROKER_ADDRESS, PORT, 60)
except Exception as e:
    print(f"Impossible de se connecter à Mosquitto: {e}")
    print("Verifiez que le service tourne : sudo systemctl restart mosquitto")
    exit(1)

# Lancer le thread d'info (Météo/Heure/Places)
t_info = threading.Thread(target=info_loop, args=(client,))
t_info.daemon = True
t_info.start()

# Boucle principale
try:
    client.loop_forever()
except KeyboardInterrupt:
    print("Arrêt serveur.")