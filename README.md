Projet Robotique – Programmation et contrôle d'un robot (ATLAS)
Présentation:
Ce projet universitaire consiste à découvrir et programmer un robot mobile à travers plusieurs travaux pratiques. Il permet de mettre en œuvre des algorithmes de commande, de développer des comportements autonomes et d'étudier les performances des différentes architectures de décision.
L'objectif est de comprendre le fonctionnement du robot, de communiquer avec lui depuis un téléphone et d'améliorer progressivement ses capacités de déplacement et de navigation.
Objectifs généraux
    • Prendre en main l'environnement de développement.
    • Implémenter des algorithmes simples sur le robot.
    • Étudier les compromis entre performances et ressources.
    • Développer et tester des comportements autonomes.
    • Analyser les délais de communication et de décision.
Travaux pratiques
TP 1 – Découverte du robot
    • Identifier les composants du robot et vérifier le câblage.
    • Compiler et charger un programme sur la carte.
    • Piloter le robot via une connexion USB.
    • Ajouter une nouvelle commande au programme.
TP 2 – Missions sur le téléphone
    • Écrire des missions en JavaScript exécutées par le téléphone.
    • Comprendre la réception de la télémétrie et le fonctionnement de done.
    • Préparer la mission « avancer-obstacle », utilisée pour les comparaisons du TP 4.
TP 3 – Communication entre le téléphone et le robot
    • Comprendre les échanges entre la page du téléphone et le robot.
    • Compléter et améliorer la page de contrôle.
TP 4 – Mesure des deux boucles de décision
    • Comparer une même condition d'arrêt exécutée sur le robot (Robot::mission()) et sur le téléphone (Mission.run()).
    • Mesurer les différences de distance d'arrêt.
    • Estimer le délai supplémentaire lié à la communication téléphone → Wi-Fi → robot.
    • Distinguer ce délai des deux indicateurs de latence affichés par la page.
TP 5 – Longer un mur
    • Développer un comportement autonome directement sur le robot.
    • Passer d'une correction en tout ou rien à une correction proportionnelle.
    • Tester et analyser la précision du déplacement le long d'un mur.
TP 6 – Correcteur PID
    • Comprendre le rôle des trois termes d'un correcteur PID : proportionnel, intégral et dérivé.
    • Mettre en œuvre un correcteur PID.
    • Régler ses paramètres sur un système réel.
    • Observer et analyser le comportement du robot.
Technologies et outils
    • C / C++ : programmation du robot (selon l'environnement fourni).
    • JavaScript : développement des missions exécutées sur le téléphone.
    • Wi-Fi : communication entre le téléphone et le robot.
    • USB : connexion et pilotage du robot.
    • Environnement de développement : compilation et chargement des programmes.
Compétences développées
    • Programmation embarquée.
    • Algorithmique et logique de commande.
    • Programmation JavaScript.
    • Communication entre systèmes.
    • Analyse des performances et des latences.
    • Automatisation et régulation.
    • Mise en œuvre et réglage d'un correcteur PID.
Auteur
SELMI Manar 
