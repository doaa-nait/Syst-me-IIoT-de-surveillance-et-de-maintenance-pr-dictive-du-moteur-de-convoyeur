#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------- Broches (identiques au diagram.json) ----------
const int PIN_DHT    = 4;   // DHT22 : fil de données
const int PIN_RELAIS = 26;  // relais : HIGH = moteur en marche
const int PIN_BUZZER = 27;
const int LED_VERTE  = 18;
const int LED_JAUNE  = 19;
const int LED_ROUGE  = 23;
const int PIN_INTER  = 33;  // interrupteur de test (utilisé à l'étape 4)

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
  if (isnan(t)) return Q_DECONNECTE;                  // le DHT22 ne répond pas
  if (t < T_MIN || t > T_MAX) return Q_HORS_PLAGE;    // physiquement impossible
  if (!premiereTemp && fabs(t - derniereTempValide) > SAUT_MAX_T) {
    nbSautsConsecutifs++;
    if (nbSautsConsecutifs < CONFIRMATIONS) return Q_PIC;   // saut pas encore confirmé
  }
  nbSautsConsecutifs = 0;                             // valeur acceptée
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

// ---------- 5. Commandes (ici au clavier ; à l'étape 4 : par MQTT) ----------
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
}

void loop() {
  lireCommandeSerie();

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
  }

  digitalWrite(PIN_RELAIS, relaisOn ? HIGH : LOW);   // une commande agit tout de suite
}