# Système IIoT de surveillance et de maintenance prédictive d'un moteur de convoyeur

Mini-projet IIoT — Filière TDI, ENSA Béni Mellal. Binôme : Naitbenali Doaa, Harbaz Imane

Prototype entièrement simulé : un ESP32 (Wokwi) mesure la température et la vibration
du moteur et les publie en MQTT vers un broker Mosquitto sécurisé. Node-RED affiche les
mesures, déclenche les alertes et commande l'arrêt du moteur en cas de dépassement.

## Structure du dépôt
| Dossier | Contenu |
|---|---|
| `firmware/` | Code de l'ESP32 (PlatformIO) et circuit Wokwi |
| `broker/` | Configuration de Mosquitto (authentification, ACL) |
| `dashboard/` | Flux Node-RED exporté |
| `data/` | Historique des mesures, journal des alertes, script Python |
| `docs/` | Cahier des charges, schémas, rapport, captures |

## Lancer le projet
À compléter au fil du projet.