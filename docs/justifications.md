# Justifications des choix techniques

## 1. Protocole MQTT et niveaux de QoS

- **Mesures (QoS 0)** : une mesure perdue n'est pas grave, car la suivante arrive 2 secondes après. QoS 0 limite aussi la charge sur le réseau et sur l'ESP32. Note : la bibliothèque PubSubClient ne publie qu'en QoS 0.
- **Commandes (QoS 1)** : une commande d'arrêt ou de marche doit arriver au moins une fois. QoS 1 garantit cette livraison.
- **États et LWT (retain)** : l'état de connexion est retenu par le broker, pour qu'un technicien connecté plus tard le voie immédiatement.

Notion de cours : Chapitre II, protocoles de l'IIoT (MQTT)

## 2. Authentification MQTT

Le broker refuse les connexions anonymes (`allow_anonymous false`). Chaque appareil a son propre compte et son mot de passe.

Justification : sans authentification, n'importe quel appareil du réseau peut se faire passer pour l'ESP32 et envoyer de fausses mesures, ou publier des commandes au moteur. C'est le risque d'usurpation.

Notion de cours : Chapitre V, cybersécurité industrielle (OT Security)

## 3. Droits par compte (ACL, moindre privilège)

- `esp32_convoyeur1` : peut publier les capteurs et l'état, et ne peut **pas** écrire de commande.
- `nodered` : peut lire les données et publier les commandes.
- `technicien` : peut seulement lire.

Justification : chaque compte n'a que les droits nécessaires. Un compte compromis ne peut pas faire plus que son rôle.

Notion de cours : Chapitre V, cybersécurité industrielle (OT Security)

## 4. Last Will (LWT)

L'ESP32 déclare à la connexion le message `offline` sur `etat/connexion`. Si la connexion est coupée brutalement, le broker publie ce message à sa place.

Justification : le dashboard signale la perte d'un capteur, au lieu de rester silencieux.

Notion de cours : Chapitre II, protocoles de l'IIoT (MQTT)

## 5. Validation des données

- Sur l'ESP32 : rejet des valeurs hors plage (température de -40 à 80 °C, vibration de 0 à 30 m/s²), détection des sauts brusques (confirmés après 3 mesures) et des capteurs déconnectés.
- Dans Node-RED : même vérification, une seconde fois.

Justification : défense en profondeur. Un capteur défectueux ou piraté peut envoyer n'importe quoi. Deux contrôles indépendants réduisent le risque de fausse alerte ou de fausse sécurité.

Notion de cours : Chapitre III, capteurs (précision et erreur de mesure) et Chapitre V, contrôle qualité par l'IIoT

## 6. Sécurité locale

Si la température dépasse 75 °C, l'ESP32 coupe le moteur lui-même, sans attendre le réseau.

Justification : la sécurité ne doit pas dépendre d'une connexion réseau.

Notion de cours : Chapitre III, actionneurs industriels

## 7. Réarmement manuel

Après un arrêt de sécurité, le moteur ne redémarre pas tout seul : il faut une commande ON explicite, refusée si la température est encore au-dessus du seuil.

Justification : éviter un redémarrage dangereux automatique.

Notion de cours : Chapitre III, actionneurs industriels