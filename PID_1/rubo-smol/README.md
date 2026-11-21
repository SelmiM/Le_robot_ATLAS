# Robot Pico

Programme d'un robot à quatre moteurs piloté par un Raspberry Pi Pico W. Le robot reçoit des commandes JSON par le câble USB ou par WiFi, depuis la page web qu'il sert à un téléphone.

Pour la première mise en route, suivre [`DEMARRAGE.md`](DEMARRAGE.md).

## Commandes utiles

À lancer dans le conteneur VS Code, depuis la racine du projet :

| Commande                          | Effet                                                  |
| --------------------------------- | ------------------------------------------------------ |
| `python3 tools/build.py --flash`  | Compiler et charger le programme sur le robot          |
| `python3 tools/pico.py console`   | Ouvrir la console USB (quitter : ctrl-A puis ctrl-X)   |
| `python3 tools/test.py`           | Lancer les tests sur l'ordinateur, sans robot          |
| `node client/fakerobot.js`        | Faux robot pour tester la page sur `http://localhost:8080/` |
| `python3 tools/sim.py`            | Simuler une mission de `Robot.h` le long d'un mur ; voir [`SIMULATION.md`](SIMULATION.md) |

Si le chargement échoue, rebrancher l'USB en maintenant le bouton BOOTSEL, puis relancer la commande.

## Où écrire le code

| Fichier                 | Contenu                                         |
| ----------------------- | ----------------------------------------------- |
| `robot/Robot.h`         | Missions exécutées par le robot (C++)           |
| `robot/web/missions.js` | Missions exécutées par le téléphone (JavaScript) |
| `robot/web/index.html`  | Page web du téléphone                           |
| `robot/Config.h`        | Broches, WiFi et options du matériel            |

Les énoncés des exercices sont dans les sujets de TP. Toute modification, y compris sous `robot/web/`, demande de recompiler et de recharger le robot.

[`API.md`](API.md), en anglais, décrit les commandes JSON et les fonctions disponibles dans `Robot.h` et `robot.js`.

## Configuration du robot

Ne pas modifier `robot/Config.h` directement : le copier en `robot/Config.local.h` et modifier la copie. Quand ce fichier existe, il remplace `Config.h`. Le WiFi s'y règle :

```c
#define RADIO         RADIO_WIFI
#define WIFI_SSID     "nom-du-reseau"
#define WIFI_PASSWORD "mot-de-passe"
```

## Règles de déplacement

- Dans le robot, faire bouger les roues avec `drive(gauche, droite, durée_ms)`, jamais avec `motors.set()`.
- Un mouvement dure 1 s par défaut et 5 s au maximum, puis le robot s'arrête.
- Le robot s'arrête aussi s'il ne reçoit plus rien du téléphone pendant 1,5 s.

<a id="firmware-and-phone-missions"></a>

## Mission sur le robot ou sur le téléphone

- Une mission du robot (`Robot::mission()` dans `Robot.h`) lit ses capteurs directement et peut arrêter les roues tout de suite.
- Une mission du téléphone (`missions.js`) attend la télémétrie envoyée par le robot, puis renvoie une commande : elle réagit plus lentement.

<a id="http-interface"></a>

## Interface HTTP

En WiFi, le robot répond à l'adresse affichée sur son écran :

| Route                    | Rôle                                          |
| ------------------------ | --------------------------------------------- |
| `POST /api/command`      | Envoyer des commandes JSON, une par ligne     |
| `GET /api/lines?after=N` | Lire les lignes JSON envoyées après la ligne N |

```sh
curl -d '{"left":200,"right":200}' http://<adresse-du-robot>/api/command
```
