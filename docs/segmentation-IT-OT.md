# Segmentation IT/OT

## 1. Les niveaux du modèle en couches

Le système est organisé en niveaux, du terrain vers le bureau :

- **Niveau 0-1 (OT, terrain)** : capteurs et ESP32 (température DHT22, vibration MPU6050, relais moteur).
- **Niveau 2-3 (OT, supervision)** : broker Mosquitto et Node-RED, qui valident les données et affichent le dashboard.
- **Niveau 4 (IT, bureautique)** : postes des techniciens et réseau de l'entreprise.

Les niveaux 0 à 3 forment le réseau industriel (OT). Le niveau 4 est le réseau informatique (IT).

## 2. Ce qui est simulé dans ce projet

Dans notre prototype, tout tourne sur un seul ordinateur : le broker, Node-RED et la simulation Wokwi. Il n'existe donc **aucune séparation réelle** entre les zones IT et OT.

C'est une limite du prototype, que nous assumons. Le projet vise à montrer les bons principes de sécurité, pas à les mettre en œuvre sur un réseau de production.

## 3. Ce qu'il faudrait dans un déploiement réel

- L'ESP32 et ses capteurs sur un **VLAN OT** dédié, isolé du réseau bureautique.
- Le broker Mosquitto dans une **DMZ industrielle** (zone intermédiaire entre IT et OT).
- Un **pare-feu** entre la zone OT et la zone IT.
- Seul le port MQTT (1883, ou 8883 avec TLS) est autorisé à traverser le pare-feu.
- Node-RED et le dashboard côté IT, accessibles uniquement aux techniciens autorisés.
- Aucune connexion directe entre Internet et la zone OT.

## 4. Lien avec la sécurité

La segmentation limite les dégâts : si le réseau bureautique est compromis, l'attaquant ne peut pas atteindre directement les capteurs ni le moteur.

Les ACL du broker ajoutent une deuxième protection : chaque compte n'a que les droits nécessaires. Par exemple, l'ESP32 peut publier ses mesures mais ne peut pas envoyer de commande au moteur. Si un ESP32 est volé ou piraté, il ne peut donc pas actionner le moteur.