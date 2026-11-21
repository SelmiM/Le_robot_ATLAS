# Simulation du suivi de mur

`tools/sim.py` compile `robot/Robot.h` sur l'ordinateur et fait rouler un robot simulé le long d'un mur, placé à sa gauche. Le code de `Robot.h` est celui qu'on chargera ensuite sur le robot ; seuls les fichiers du matériel (`Motors.h`, `TofSensor.h`, `Clock.h`, etc.) sont remplacés par ceux de `tools/sim/stubs/`.

Le robot simulé a les défauts d'un vrai robot : un côté plus faible que l'autre, des roues qui réagissent avec retard, un capteur qui mesure toutes les 40 ms et perd parfois une mesure. Les valeurs du modèle sont inventées (elles sont listées dans `tools/sim/stubs/SimWorld.h`) : les comportements sont ceux du robot, mais pas les amplitudes ni les périodes. Il faudra les mesurer à nouveau sur le robot.

## Lancer une simulation

1. Mettre `SIDE_SENSOR_ENABLED` à `true` dans `robot/Config.h`, et dans `robot/Config.local.h` s'il existe. Sinon le capteur latéral ne renvoie rien, comme sur le robot.
2. Depuis la racine du projet :

   ```sh
   python3 tools/sim.py
   ```

3. Ouvrir `sim/run.svg` depuis l'explorateur de VS Code : VS Code affiche le graphique. Le garder ouvert à côté de `Robot.h`, par exemple en faisant glisser son onglet vers la droite de l'éditeur. L'affichage se met à jour à chaque nouvelle simulation.

La simulation envoie `{"mission": 3}` au robot, puis le laisse rouler 20 s, parallèle au mur et à 260 mm de celui-ci au départ. Elle s'arrête plus tôt si le robot touche le mur, s'en éloigne de plus de 3 m, ou si ses roues ne tournent plus pendant 3 s.

Elle produit deux fichiers :

| Fichier       | Contenu                                                                  |
| ------------- | ------------------------------------------------------------------------ |
| `sim/run.svg` | Trajectoire vue de dessus, mesure latérale et commandes des roues dans le temps |
| `sim/run.csv` | Une ligne toutes les 10 ms : position, cap, commandes, mesure latérale   |

Le terminal affiche un résumé :

```text
end      time up at 20.00 s
position x = 4741 mm, clearance 240 mm, heading 5.1 deg
driven   4.75 m
side     500 readings, 7 missing
safety   0 stops by enforceSafety() (telemetry lines with "done":true)
```

`clearance` est la distance entre le flanc gauche et le mur. `safety` compte les arrêts imposés par `enforceSafety()`.

**Note.** Le graphique du haut n'est pas à l'échelle : l'axe vertical est très étiré pour rendre les oscillations visibles. Le cercle vide marque le départ du robot, le point plein l'endroit où la simulation s'est arrêtée.

## Options

| Option                  | Effet                                                                |
| ----------------------- | -------------------------------------------------------------------- |
| `--out sim/nom`         | Écrire `sim/nom.svg` et `sim/nom.csv` au lieu de `sim/run.*`          |
| `--compare sim/nom.csv` | Dessiner en gris, derrière, une simulation précédente                 |
| `-D NOM=VALEUR`         | Définir une constante à la compilation, par exemple un gain           |
| `--start 300`           | Distance de départ entre le flanc gauche et le mur, en mm             |
| `--heading 10`          | Angle de départ en degrés, positif vers le mur                        |
| `--door 2000:2900`      | Une ouverture dans le mur, de x = 2000 mm à x = 2900 mm               |
| `--duration 30`         | Durée en secondes                                                     |
| `--mission 4`           | Numéro de la mission lancée                                           |
| `--seed 2`              | Autre tirage du bruit du capteur                                      |

`-D` ne remplace une valeur que si `Robot.h` la définit avec `#ifndef`, comme `OBSTACLE_STOP_MM` :

```cpp
#ifndef WALL_GAIN_NUM
#define WALL_GAIN_NUM  1
#endif
```

## Le TP5 en simulation

1. Étape 1 : remplacée par le réglage de `SIDE_SENSOR_ENABLED` ci-dessus.
2. Étape 2 : sans objet, la page du téléphone n'est pas simulée.
3. Étapes 3 et 4 : sans changement. Lancer la version en tout ou rien :

   ```sh
   python3 tools/sim.py --out sim/tout-ou-rien
   ```

4. Étape 5 : mesurer l'amplitude et la période sur `sim/tout-ou-rien.svg`. Pour des valeurs précises, ouvrir le fichier `.csv` dans un tableur.
5. Étape 6 : lancer la version proportionnelle et la comparer à la précédente :

   ```sh
   python3 tools/sim.py --out sim/proportionnel --compare sim/tout-ou-rien.csv
   ```

6. Étape 7 : chercher un gain trop faible et un gain trop fort, en relançant avec `-D` ou en modifiant `Robot.h`.

Pour les questions :

- Question 1 : ajouter `--door 2000:2900` et regarder ce que fait le robot pendant et après l'ouverture.
- Question 2 : après le remplacement de `drive()` par `motors.set()`, lire la ligne `safety` du résumé.
- Question 3 : se traite sur papier, avec les mesures du TP4.

**Watch out.** Un départ loin de la consigne (par exemple `--start 400` pour une consigne de 250 mm) peut finir dans le mur, même avec un gain correct. Regarder `side_mm` pendant que le robot tourne vers le mur : que mesure le capteur quand le robot n'est plus parallèle au mur ?

**Note.** Toute erreur de compilation s'affiche avec le chemin de `robot/Robot.h` : on la corrige comme pour `tools/build.py`.

## Le TP6 en simulation

La mission 4 se simule comme la mission 3. Définir les gains avec `#ifndef`, comme au TP5, permet de les changer avec `-D` sans modifier `Robot.h`.

1. Étape 1 : simuler la mission 3, puis la mission 4 une fois écrite, en superposant les deux :

   ```sh
   python3 tools/sim.py --mission 3 --duration 30 --out sim/p
   python3 tools/sim.py --mission 4 --duration 30 --out sim/pid --compare sim/p.csv
   ```

2. Étapes 2 et 3 : relancer la mission 4 après chaque terme ajouté, avec `--compare` sur la simulation précédente. Pour l'étape 3, calculer dans un tableur la moyenne de `side_mm` sur les 20 dernières secondes de `sim/pid.csv`, et la comparer à la distance voulue.
3. Étape 4 : passer chaque valeur essayée avec `-D`, et garder pour chacune les fichiers produits avec un nom de sortie différent.
4. Bruit du capteur : `python3 tools/sim.py --mission 0 --out sim/immobile` laisse le robot immobile pendant 3 s ; `sim/immobile.csv` contient les mesures de `side_mm` de cette période.

Pour les questions :

- Questions 1 et 2 : à faire sur le robot. La simulation exécute la boucle une fois par milliseconde simulée, quel que soit le code : `dt` y est toujours le même, et un `printf` n'y ralentit rien.
- Question 3 : comparer la moyenne de `side_mm` avec et sans terme I. Refaire la comparaison avec `-D SIM_LEFT_STRENGTH=1`, qui équilibre les moteurs du robot simulé : le terme I sert-il encore ?
- Question 4 : sans changement.

**Note.** Les gains trouvés en simulation ne sont pas ceux du robot. Refaire le réglage de l'étape 4 sur le robot une fois le capteur monté.
