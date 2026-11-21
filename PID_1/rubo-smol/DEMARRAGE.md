# Démarrage

Ce guide va du branchement au premier déplacement, d'abord par USB, puis par WiFi. Prévoir environ une demi-heure pour la première mise en route.

Ouvrir le projet dans VS Code et choisir "Reopen in Container". Le conteneur installe les outils de compilation et récupère le SDK du Pico lors de sa création. Le chargement et la console nécessitent un hôte Linux ; voir `.devcontainer/devcontainer.json` pour Windows et macOS.

## 1. Préparer le projet

Dans le conteneur, `tools/setup.py` a déjà été exécuté. Si le SDK manque (par exemple hors du conteneur), lancer :

```
python3 tools/setup.py
```

Ce script récupère le SDK et vérifie la présence du compilateur ; il n'installe pas le compilateur lui-même.

## 2. Vérifier le câblage

Le schéma complet est dans `schematics/sketches/Robot_Schema_completed.fzz` (Fritzing). Les numéros de broches et les options du matériel sont dans `robot/Config.h`. Vérifier avant d'alimenter le robot :

| Élément           | Branché sur                                              |
| ----------------- | -------------------------------------------------------- |
| Télémètre avant   | connecteur D18                                           |
| Télémètre latéral | broches 26 (SDA), 27 (SCL), 28 (GND) et 36 (3V3) du Pico |
| Écran 16x2        | connecteur I2C1                                          |
| Moteurs           | GP2, GP3, GP26, GP27                                     |

Les deux moteurs de chaque côté sont reliés en parallèle à une sortie du L298N. Laisser ses cavaliers ENA et ENB en place. Le télémètre avant va sur une prise numérique, **pas sur I2C** : il bloquerait le bus. Le capteur latéral VL53L1X utilise GP20/GP21 ; laisser la prise D20 libre. Les détails de câblage et les variantes de matériel sont commentés dans `Config.h`.

## 3. Relier les masses

La batterie des moteurs alimente le L298N ; celle du Pico passe par un convertisseur abaisseur relié à `VSYS`. **Relier le `GND` du L298N à un `GND` du Pico ou du shield. Ne pas relier les bornes positives.**

Sans masse commune, le L298N ne peut pas interpréter les signaux 3,3 V du Pico : les moteurs peuvent rester immobiles ou fonctionner par intermittence. C'est le premier fil à vérifier si les roues ne tournent pas.

## 4. Compiler et charger le programme

Brancher le Pico à l'ordinateur avec le câble USB, puis lancer :

```
python3 tools/build.py --flash
```

La commande compile et charge le programme. BOOTSEL n'est normalement pas nécessaire.

## 5. Ouvrir la console

```
python3 tools/pico.py console
```

La console ouvre le port série USB, quel que soit le réglage `RADIO`. Quitter avec **ctrl-A puis ctrl-X**. Par défaut, `RADIO_SERIAL` permet aussi d'y taper les commandes. Avec `RADIO_WIFI`, la console n'affiche que les messages de démarrage et les `printf()` ; les commandes passent par la page du téléphone.

## 6. Lire le message de démarrage

Au démarrage, le robot indique quels périphériques lui répondent :

```
Robot ready.
  radio    : USB serial at 115200 baud
  front distance sensor : ok
  display : ok
```

Si le télémètre avant affiche `ok`, tout est prêt. Sinon, voir "Quand ça ne marche pas" plus bas.

L'écran affiche d'abord `Robot ready` et `USB cable`, puis, au bout d'une demi-seconde, l'état du robot : la vitesse de chaque roue sur la première ligne, la distance mesurée devant et le numéro de mission sur la seconde.

En WiFi, la deuxième ligne affiche l'adresse IP du robot jusqu'à la première commande reçue. Une fois le câble débranché, c'est le seul endroit où la lire.

## 7. Faire tourner les roues

Taper ces commandes dans la console, une par ligne, en validant chacune par Entrée :

```json
{"left": 200, "right": 200}
{"left": 200, "right": -200}
{"stop": true}
```

La première fait avancer le robot, la deuxième le fait tourner sur place, la troisième l'arrête. Chaque mouvement dure une seconde, puis le robot s'arrête de lui-même, même sans `stop`.

## 8. Vérifier le télémètre

Le robot envoie son état dans la console cinq fois par seconde : c'est la télémétrie. Approcher la main du télémètre avant : la valeur `front_mm`, en millimètres, doit diminuer. Vérifier `front_ok` avant de lire cette distance : si la mesure n'est pas valide, `front_mm` vaut `-1`. Par exemple :

```json
{"seq":-1,"ms":41230,"left":0,"right":0,"moving":false,"mission":0,"front_mm":412,"front_ok":true,"front_q":-1,"side_mm":-1,"side_ok":false,"side_q":-1}
```

`seq` vaut `-1` tant qu'aucune commande ne l'a précisé. Quand une durée de déplacement expire, une ligne avec `"done":true` et `"moving":false` signale l'arrêt des roues ; la commande `{"stop":true}` les arrête sans envoyer de ligne `done`. Le format complet est décrit dans [`API.md` — JSON protocol](API.md#json-protocol).

---

## Quand ça ne marche pas

- **`--flash` échoue** : rebrancher l'USB en maintenant BOOTSEL, puis relancer la commande.
- **Aucun message au démarrage** : lancer `python3 tools/pico.py list`. `0003` indique le mode BOOTSEL ; `0009` ou `000a`, un programme en cours. Si le port série manque, rebrancher l'USB et relancer `tools/pico.py console`.
- **`ENABLED BUT NOT RESPONDING` sur un télémètre** : vérifier D18 pour le capteur avant et, pour le latéral, SDA sur la broche 26 et SCL sur la 27. Le robot réessaie chaque seconde : une mesure peut apparaître sans redémarrer.
- **Aucun moteur ne tourne, ou un seul sens fonctionne** : vérifier la masse commune (section 3), puis les cavaliers ENA et ENB. Si un moteur bourdonne, augmenter la vitesse demandée au-dessus de 100.
- **Le robot recule, un côté tourne à l'envers, ou les virages sont inversés** : tester chaque côté seul dans la console, en corrigeant `Config.h` au fur et à mesure (recompiler et recharger après chaque modification).
  1. Envoyer `{"left": 200, "right": 0}`. Seules les roues gauches doivent tourner, vers l'avant.
     - Si ce sont les roues droites qui tournent, les moteurs gauches sont branchés sur IN3/IN4 et les droits sur IN1/IN2 : échanger les valeurs de `LEFT_MOTOR_PIN_A`/`_B` avec celles de `RIGHT_MOTOR_PIN_A`/`_B`, puis reprendre à l'étape 1.
     - Si les roues gauches tournent vers l'arrière, inverser `LEFT_MOTOR_REVERSED`.
  2. Envoyer `{"left": 0, "right": 200}`. Seules les roues droites doivent tourner, vers l'avant. Si elles tournent vers l'arrière, inverser `RIGHT_MOTOR_REVERSED`.
  3. Vérifier : `{"left": 200, "right": 200}` fait avancer le robot, `{"left": -200, "right": 200}` le fait tourner sur place vers la gauche.

  **Attention.** Un robot qui recule sur `{"left": 200, "right": 200}` peut avoir deux câblages différents. Si ses virages sont inversés, il suffit d'inverser les deux réglages `*_MOTOR_REVERSED`. Si ses virages sont corrects, il faut inverser les deux réglages et échanger les broches. Appliquer la mauvaise correction donne un robot qui avance droit mais tourne à l'envers. Les étapes 1 et 2 évitent d'avoir à faire la différence.
- **Écran muet** : vérifier la prise I2C1 et le modèle du module. Le pilote ne prend pas en charge les cartes PCF8574. Si l'écran s'allume sans texte, régler le potentiomètre de contraste.

## Passer en WiFi

En WiFi, le robot se pilote depuis un téléphone : il sert lui-même une page web. Les ordinateurs de la salle ne sont pas sur le réseau du robot, et n'en ont pas besoin.

Dans `Config.h` :

- `RADIO` à `RADIO_WIFI` ;
- `WIFI_SSID` et `WIFI_PASSWORD` : ceux du point d'accès de la salle. Pour un réseau ouvert, sans mot de passe, écrire `#define WIFI_PASSWORD nullptr`, sans guillemets ;
- `ROBOT_NAME` : le numéro du binôme, par exemple `binome-07`.

Le programme se charge toujours par le câble USB :

1. `python3 tools/build.py --flash`
2. `python3 tools/pico.py console`. Le message de démarrage contient une ligne `radio : http://192.168.x.y/`. Quitter par ctrl-A ctrl-X.
3. Débrancher le câble. L'adresse reste affichée sur la deuxième ligne de l'écran jusqu'à la première commande reçue.
4. Connecter le téléphone au point d'accès de la salle, puis ouvrir cette adresse dans son navigateur. Le titre de la page est le `ROBOT_NAME` du robot.
5. Maintenir ▲ : le robot avance tant que le doigt reste appuyé.

Le point d'accès ne donne pas accès à Internet. Android le signale et propose de changer de réseau : répondre "Rester connecté".

Si la connexion échoue :

- **Pas d'adresse après vingt secondes**, ou `Could not bring up the radio` : vérifier le SSID, le mot de passe et la présence d'un réseau 2,4 GHz. Sans connexion, les roues restent à l'arrêt.
- **L'adresse est affichée, mais la page ne s'ouvre pas** : vérifier que le téléphone est sur le même point d'accès et saisir l'adresse avec `http://`. Vérifier aussi que le réseau autorise les appareils à se joindre entre eux.
- **La page cesse de répondre** : Android peut avoir basculé sur les données mobiles. Les désactiver et recharger la page.
- **Le nom du robot est incorrect** : vérifier l'adresse affichée sur *son* écran avant de piloter ; il peut s'agir du robot d'un autre binôme.

Le robot s'arrête après `WEB_TIMEOUT_MS` (1,5 s) sans requête, notamment quand le téléphone est verrouillé. En WiFi, les commandes tapées dans la console USB ne sont pas transmises au robot.
