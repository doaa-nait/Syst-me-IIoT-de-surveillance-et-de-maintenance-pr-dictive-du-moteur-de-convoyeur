#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

const int PIN_DHT    = 4;
const int PIN_RELAIS = 26;
const int PIN_BUZZER = 27;
const int LED_VERTE  = 18;
const int LED_JAUNE  = 19;
const int LED_ROUGE  = 23;
const int PIN_INTER  = 33;

// ---------- Valeurs du cahier des charges ----------
const unsigned long PERIODE_MS = 2000;       // période de mesure
const float T_MIN = -40, T_MAX = 80;         // plage plausible température (°C)
const float V_MIN = 0,   V_MAX = 30;         // plage plausible vibration (m/s²)
const float T_ALERTE = 60, T_CRITIQUE = 70, T_SECURITE = 75;
const float V_ALERTE = 3,  V_CRITIQUE = 6,  V_SECURITE = 8;
const float SAUT_MAX_T = 10;                 // saut suspect entre deux mesures (°C)
const int   CONFIRMATIONS = 3;               // mesures pour confirmer un saut

enum Qualite { Q_OK, Q_HORS_PLAGE, Q_DECONNECTE, Q_PIC };
enum Niveau  { N_NORMAL, N_ALERTE, N_CRITIQUE, N_SECURITE };
const char* NOM_QUALITE[] = {"OK", "HORS_PLAGE", "CAPTEUR_DECONNECTE", "PIC_SUSPECT"};
const char* NOM_NIVEAU[]  = {"NORMAL", "ALERTE", "CRITIQUE", "SECURITE"};

DHT dht(PIN_DHT, DHT22);
Adafruit_MPU6050 mpu;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

const char* TOPIC_TEMP = "usine/ligne1/convoyeur1/capteur/temperature";
const char* TOPIC_VIB  = "usine/ligne1/convoyeur1/capteur/vibration";
const char* TOPIC_ETAT = "usine/ligne1/convoyeur1/etat/connexion";
const char* TOPIC_CMD  = "usine/ligne1/convoyeur1/cmd/relais";

unsigned long dernierEssaiMqtt = 0;
unsigned long seq = 0;           // numéro de chaque mesure publiée

bool  mpuOk = false;
float gRepos = 9.81;
unsigned long dernierCycle = 0;

bool  premiereTemp = true;       // mémoire de l'anti-pic
float derniereTempValide = 0;
int   nbSautsConsecutifs = 0;

bool   relaisOn = false;         // au démarrage, le moteur reste ARRÊTÉ
bool   arretSecurite = false;    // vrai après un arrêt de sécurité, jusqu'au réarmement
bool   mesuresFiables = false;   // les deux capteurs donnent-ils une valeur OK ?
bool   buzzerActif = false;
Niveau niveauActuel = N_NORMAL;
String ligneSerie = "";
bool   coupureWifi = false;      // vrai quand l'interrupteur simule une panne réseau

float normeAcceleration() {
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  return sqrt(a.acceleration.x * a.acceleration.x +
              a.acceleration.y * a.acceleration.y +
              a.acceleration.z * a.acceleration.z);
}

void autotestActionneurs() {
  const int leds[] = {LED_VERTE, LED_JAUNE, LED_ROUGE};
  for (int led : leds) { digitalWrite(led, HIGH); delay(300); digitalWrite(led, LOW); }
  digitalWrite(PIN_RELAIS, HIGH); delay(300); digitalWrite(PIN_RELAIS, LOW);
  tone(PIN_BUZZER, 1000, 200);
}

// ---------- 1. Validation des mesures ----------
Qualite validerTemperature(float t) {
  if (isnan(t)) return Q_DECONNECTE;                  
  if (t < T_MIN || t > T_MAX) return Q_HORS_PLAGE;    
  if (!premiereTemp && fabs(t - derniereTempValide) > SAUT_MAX_T) {
    nbSautsConsecutifs++;
    if (nbSautsConsecutifs < CONFIRMATIONS) return Q_PIC;   
  }
  nbSautsConsecutifs = 0;                             
  premiereTemp = false;
  derniereTempValide = t;
  return Q_OK;
}

Qualite validerVibration(float v) {
  if (!mpuOk || isnan(v)) return Q_DECONNECTE;
  if (v < V_MIN || v > V_MAX) return Q_HORS_PLAGE;
  return Q_OK;
}

// ---------- 2. Niveau d'alarme (valeurs valides uniquement) ----------
Niveau plusGrave(Niveau a, Niveau b) { return a > b ? a : b; }

Niveau niveauDe(float x, float alerte, float critique, float securite) {
  if (x >= securite) return N_SECURITE;
  if (x >= critique) return N_CRITIQUE;
  if (x >= alerte)   return N_ALERTE;
  return N_NORMAL;
}

Niveau calculerNiveau(float t, Qualite qt, float v, Qualite qv) {
  Niveau n = N_NORMAL;
  if (qt == Q_OK) n = plusGrave(n, niveauDe(t, T_ALERTE, T_CRITIQUE, T_SECURITE));
  if (qv == Q_OK) n = plusGrave(n, niveauDe(v, V_ALERTE, V_CRITIQUE, V_SECURITE));
  if (qt != Q_OK || qv != Q_OK) n = plusGrave(n, N_ALERTE);   // jamais « vert » en cas de doute
  return n;
}

// ---------- 3. Sécurité locale (fonctionne sans réseau) ----------
void securiteLocale(float t, float v) {
  if (niveauActuel == N_SECURITE && relaisOn) {
    relaisOn = false;
    arretSecurite = true;
    Serial.printf(">>> ARRET DE SECURITE LOCAL (T=%.1f C, V=%.2f m/s2)\n", t, v);
  }
}

// ---------- 4. Signalisation sur place ----------
void appliquerSignalisation() {
  digitalWrite(LED_VERTE, niveauActuel == N_NORMAL ? HIGH : LOW);
  digitalWrite(LED_JAUNE, niveauActuel == N_ALERTE ? HIGH : LOW);
  digitalWrite(LED_ROUGE, niveauActuel >= N_CRITIQUE ? HIGH : LOW);
  bool doitSonner = (niveauActuel >= N_CRITIQUE);
  if (doitSonner && !buzzerActif) tone(PIN_BUZZER, 1000);
  if (!doitSonner && buzzerActif) noTone(PIN_BUZZER);
  buzzerActif = doitSonner;
}

// ---------- 5. Commandes (clavier ou MQTT, mêmes règles) ----------
void traiterCommande(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "OFF") {
    relaisOn = false;
    Serial.println(">>> Commande OFF : moteur arrete");
  } else if (cmd == "ON") {
    if (niveauActuel == N_SECURITE) {
      Serial.println(">>> Commande ON REFUSEE : valeur au-dessus du seuil de securite");
    } else if (!mesuresFiables) {
      Serial.println(">>> Commande ON REFUSEE : mesures non fiables (capteur en defaut)");
    } else {
      relaisOn = true;
      arretSecurite = false;
      Serial.println(">>> Commande ON : moteur en marche");
    }
  } else {
    Serial.printf(">>> Commande inconnue refusee : %s\n", cmd.c_str());
  }
}

void lireCommandeSerie() {             // lecture caractère par caractère, sans bloquer
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (ligneSerie.length() > 0) { traiterCommande(ligneSerie); ligneSerie = ""; }
    } else {
      ligneSerie += c;
    }
  }
}

// ---------- 6. MQTT ----------
void surCommande(char* topic, byte* payload, unsigned int longueur) {
  String msg = "";
  for (unsigned int i = 0; i < longueur; i++) msg += (char)payload[i];
  if (String(topic) == TOPIC_CMD) traiterCommande(msg);
}

void gererMqtt() {
  if (mqtt.connected()) return;                       // déjà connecté : rien à faire
  if (millis() - dernierEssaiMqtt < 5000) return;     // un essai maximum toutes les 5 s
  dernierEssaiMqtt = millis();
  if (WiFi.status() != WL_CONNECTED) return;          // pas encore de Wi-Fi : on attend

  Serial.print("Connexion MQTT... ");
  // LWT : si l'ESP32 disparaît sans prévenir, le broker publie "offline" à sa place
  if (mqtt.connect("esp32_convoyeur1", MQTT_USER, MQTT_PASS,
                   TOPIC_ETAT, 1, true, "offline")) {
    Serial.println("OK");
    mqtt.publish(TOPIC_ETAT, "online", true);         // retain : l'état reste visible
    mqtt.subscribe(TOPIC_CMD, 1);                     // commandes reçues en QoS 1
  } else {
    Serial.printf("echec (code %d)\n", mqtt.state());
  }
}

// ---------- Initialisation ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_VERTE, OUTPUT);
  pinMode(LED_JAUNE, OUTPUT);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(PIN_RELAIS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_INTER, INPUT_PULLUP);
  autotestActionneurs();

  dht.begin();
  Wire.begin(21, 22);
  mpuOk = mpu.begin();
  if (!mpuOk) {
    Serial.println("ERREUR : MPU6050 introuvable");
  } else {
    float somme = 0;
    for (int i = 0; i < 20; i++) { somme += normeAcceleration(); delay(10); }
    gRepos = somme / 20;
    Serial.printf("Calibration : acceleration au repos = %.2f m/s2\n", gRepos);
  }
  Serial.println("Demarrage : moteur ARRETE. Tapez ON pour le demarrer, OFF pour l'arreter.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(surCommande);
}

// ---------- Boucle principale ----------
void loop() {
  lireCommandeSerie();

  // Interrupteur GPIO 33 : simule une coupure du Wi-Fi
  bool boutonAppuye = (digitalRead(PIN_INTER) == LOW);   // INPUT_PULLUP : appuyé = LOW
  if (boutonAppuye && !coupureWifi) {
    coupureWifi = true;
    WiFi.disconnect();
    Serial.println(">>> Coupure Wi-Fi simulee");
  } else if (!boutonAppuye && coupureWifi) {
    coupureWifi = false;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println(">>> Wi-Fi retabli");
  }

  gererMqtt();      // reconnexion si besoin, sans bloquer
  mqtt.loop();      // traite les messages entrants (commandes ON/OFF)

  if (millis() - dernierCycle >= PERIODE_MS) {
    dernierCycle = millis();

    float t = dht.readTemperature();
    float v = mpuOk ? fabs(normeAcceleration() - gRepos) : NAN;
    Qualite qt = validerTemperature(t);
    Qualite qv = validerVibration(v);
    mesuresFiables = (qt == Q_OK && qv == Q_OK);
    niveauActuel = calculerNiveau(t, qt, v, qv);

    securiteLocale(t, v);
    appliquerSignalisation();

    Serial.printf("T=%.1f C [%s] | V=%.2f m/s2 [%s] | %s | Moteur %s%s\n",
                  t, NOM_QUALITE[qt], v, NOM_QUALITE[qv], NOM_NIVEAU[niveauActuel],
                  relaisOn ? "EN MARCHE" : "ARRETE", arretSecurite ? " (arret de securite)" : "");

    if (mqtt.connected()) {
      seq++;
      char buf[128];

      // Un capteur muet publie null, pas "nan" (ce qui casserait le JSON)
      if (isnan(t)) {
        snprintf(buf, sizeof(buf), "{\"valeur\":null,\"unite\":\"C\",\"qualite\":\"%s\",\"seq\":%lu}",
                 NOM_QUALITE[qt], seq);
      } else {
        snprintf(buf, sizeof(buf), "{\"valeur\":%.1f,\"unite\":\"C\",\"qualite\":\"%s\",\"seq\":%lu}",
                 t, NOM_QUALITE[qt], seq);
      }
      mqtt.publish(TOPIC_TEMP, buf);

      if (isnan(v)) {
        snprintf(buf, sizeof(buf), "{\"valeur\":null,\"unite\":\"m/s2\",\"qualite\":\"%s\",\"seq\":%lu}",
                 NOM_QUALITE[qv], seq);
      } else {
        snprintf(buf, sizeof(buf), "{\"valeur\":%.2f,\"unite\":\"m/s2\",\"qualite\":\"%s\",\"seq\":%lu}",
                 v, NOM_QUALITE[qv], seq);
      }
      mqtt.publish(TOPIC_VIB, buf);
    }
  }

  digitalWrite(PIN_RELAIS, relaisOn ? HIGH : LOW);   // une commande agit tout de suite
}