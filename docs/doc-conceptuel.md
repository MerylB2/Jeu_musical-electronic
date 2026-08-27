# Dossier de conception — Piano Diatonique

Ce document retrace les choix qui ont guidé le projet — cahier des charges, arbitrages techniques et leurs justifications — pour qui souhaite comprendre le *pourquoi* avant le *comment*.

## 1. Contexte et objectifs

Ce projet consiste à concevoir un jouet musical électronique autour d'un Arduino Uno, combinant un instrument de musique simple (piano diatonique 8 notes) et un aspect ludique (jeu de mémorisation musicale façon "Simon").

Objectifs pédagogiques et fonctionnels visés :

- manipuler des entrées numériques multiples (8 boutons) avec anti-rebond,
- piloter des sorties au-delà du nombre de broches disponibles (8 LED via un registre à décalage),
- générer du son (fréquences, gammes, mélodies) avec le buzzer,
- afficher des informations utilisateur sur un écran LCD,
- structurer un programme autour d'une machine à états (menu / 3 modes),
- gérer une entrée analogique (potentiomètre) pour un réglage en temps réel,
- mémoriser et rejouer des séquences saisies par l'utilisateur.

## 2. Cahier des charges fonctionnel

### 2.1 Bête à cornes

| Question | Réponse |
|---|---|
| À qui rend-il service ? | À l'utilisateur (enfant ou adulte curieux de musique) |
| Sur quoi agit-il ? | Sur l'apprentissage et le jeu autour de la gamme diatonique |
| Dans quel but ? | Offrir un instrument ludique et un jeu de mémorisation musicale accessibles sans connaissances préalables |

### 2.2 Fonctions de service

| N° | Fonction | Description |
|---|---|---|
| FP1 | Jouer un instrument diatonique | L'utilisateur appuie sur un bouton, la note correspondante est jouée et affichée, avec retour visuel (LED) |
| FP2 | Jouer à un jeu de mémorisation | Le système propose une séquence de notes croissante en difficulté, à reproduire fidèlement |
| FP3 | Écouter / créer des mélodies | L'utilisateur choisit une mélodie préenregistrée ou en enregistre une nouvelle, et peut les enchaîner |
| FC1 | Informer l'utilisateur | Un écran affiche en permanence l'état du système (mode, note jouée, résultat) |
| FC2 | Permettre une transposition | Un potentiomètre permet de changer la tonalité en mode Piano |
| FC3 | Être navigable simplement | Un geste unique (appui long) permet de revenir au menu depuis n'importe quel mode |
| FC4 | Rester vivant à l'arrêt | Une musique de fond joue au menu pour signaler que l'appareil est sous tension et actif |

### 2.3 Contraintes

- Fonctionner sur un Arduino Uno (2 Ko de RAM, 32 Ko de flash) : les choix de conception doivent respecter ce budget mémoire serré.
- N'utiliser que des composants courants et bon marché (boutons, LED, buzzer, LCD I2C, registre à décalage).
- Rester utilisable sans notice, avec un minimum de boutons physiques (pas de bouton "menu" dédié : réutilisation des boutons de notes).

## 3. Choix de conception et justifications

### 3.1 Pourquoi 8 boutons = 1 octave diatonique (Do à Do) ?

Une gamme diatonique majeure comporte 7 degrés (Do Ré Mi Fa Sol La Si) ; le 8ᵉ bouton rejoue le Do de l'octave supérieure, ce qui referme visuellement et musicalement la gamme (comme les 8 touches blanches d'un piano entre deux Do). Ce choix limite le nombre de boutons à câbler tout en couvrant une gamme complète et reconnaissable.

### 3.2 Pourquoi un écran LCD I2C plutôt qu'en parallèle ?

Un LCD piloté en parallèle (mode 4 bits classique) nécessite 6 à 7 broches numériques. Avec déjà 8 boutons à câbler, il ne resterait quasiment plus de broches libres pour le reste du montage (LED, buzzer, potentiomètre). Le module I2C ramène l'écran à 2 broches partagées (SDA/SCL, communes à tout composant I2C), ce qui libère le budget de broches pour les autres fonctions.

### 3.3 Pourquoi un registre à décalage (74HC595) pour les LED ?

Avec 8 boutons (8 broches) + LCD I2C (2 broches) + buzzer (1 broche) + potentiomètre (1 broche analogique), il ne reste que 6 à 7 broches libres sur un Uno — insuffisant pour piloter individuellement 8 LED supplémentaires. Le 74HC595 permet de piloter 8 sorties à partir de 3 broches seulement (données, horloge, verrou), au prix d'un composant supplémentaire à quelques centimes. C'est le compromis classique "peu de broches, un peu plus de câblage" adapté à un microcontrôleur aux broches limitées.

Alternative envisagée et écartée : multiplexage des boutons sur une seule broche analogique (pont diviseur de tension), qui aurait libéré des broches côté entrées plutôt que sorties — écarté car moins fiable (mesures analogiques sensibles au bruit) et moins pédagogique que l'ajout d'un registre à décalage.

### 3.4 Pourquoi un potentiomètre pour la tonalité plutôt que des boutons dédiés ?

Le réglage de tonalité (transposition) est un réglage continu et fréquent en mode Piano : un potentiomètre offre un retour immédiat et intuitif ("je tourne, j'entends/je vois le changement") sans consommer de bouton supplémentaire ni complexifier la navigation.

### 3.5 Pourquoi réutiliser les boutons de notes pour naviguer les menus plutôt qu'ajouter des boutons dédiés ?

Ajouter des boutons "Menu / Valider / Retour" aurait consommé 2 à 3 broches supplémentaires, déjà rares sur ce montage. Le choix retenu réutilise les 8 boutons existants avec une règle simple et unique dans tout le programme : **un appui court joue une note ou sélectionne une option numérotée à l'écran, un appui long (~1,2 s) sur le dernier bouton ramène toujours au menu principal.** Ce geste unique, appliqué partout, limite la charge mentale de l'utilisateur malgré l'absence de boutons dédiés.

### 3.6 Pourquoi une "musique de fond" au menu ?

Sans retour sonore ni visuel actif, un utilisateur pourrait croire l'appareil éteint ou planté pendant qu'il hésite entre les modes. La musique de fond (avec LED synchronisées) confirme que le système est vivant et réactif, tout en donnant un aspect plus abouti/ludique à l'attente. Un bouton dédié permet de la couper pour un usage silencieux.

## 4. Analyse fonctionnelle des 3 modes

### 4.1 Mode Piano

```
Utilisateur appuie sur un bouton (1-8)
        │
        ▼
Lecture du potentiomètre → décalage de tonalité (-6 à +6 demi-tons)
        │
        ▼
Calcul de la fréquence transposée de la note correspondante
        │
        ▼
Affichage du nom de la note + tonalité courante sur le LCD
        │
        ▼
Émission du son (buzzer) + allumage de la LED correspondante
        │
        ▼
Retour à l'écoute du prochain appui (boucle)
```

Sortie : appui long sur le bouton 8 → retour au menu.

### 4.2 Mode Jeu (Simon musical)

```
Pour chaque niveau (1 à 3) :
        │
        ▼
Génération d'une séquence aléatoire (4, 6 ou 8 notes selon le niveau)
        │
        ▼
Lecture de la séquence par le système (son + LED, tempo décroissant selon le niveau)
        │
        ▼
L'utilisateur reproduit la séquence, note par note (4 s max par note)
        │
   ┌────┴─────┐
   ▼          ▼
Erreur     Séquence entièrement correcte
   │          │
   ▼          ▼
Son d'échec  Son de réussite
+ émoji      + émoji content + cœur
triste          │
   │          ▼
   │       Niveau suivant (ou écran de victoire si niveau 3 terminé)
   ▼
Nouvelle séquence générée, même niveau
```

La difficulté croît avec le niveau : séquences plus longues (4 → 6 → 8 notes) et tempo plus rapide (500 ms → 420 ms → 340 ms par note), pour un sentiment de progression cohérent.

### 4.3 Mode Sons

```
Sous-menu (boutons 1 à 7) :
  1-3 → Jouer une mélodie préchargée (Au clair de la lune / Frère Jacques / Anniversaire)
  4   → Jouer les 3 mélodies préchargées à la suite
  5   → Enregistrer une nouvelle mélodie
          - Choix de l'emplacement (1 à 3) via le potentiomètre
          - Chaque note jouée par l'utilisateur est mémorisée (note + durée entre appuis)
          - Fin automatique après 3 s d'inactivité
  6   → Rejouer l'enregistrement de l'emplacement choisi par le potentiomètre
  7   → Rejouer tous les enregistrements non vides, à la suite
```

## 5. Interface utilisateur

L'appareil s'articule autour de 3 zones physiques :

- **8 boutons colorés** (une note chacun), doublés d'une LED de retour visuel juste au-dessus.
- **1 écran LCD** centralisant tout retour d'information textuel (mode courant, note jouée, résultat de jeu, menus).
- **1 potentiomètre** pour les réglages continus (tonalité, choix d'emplacement d'enregistrement).

Aucun bouton "on/off" logiciel n'existe : la mise sous/hors tension se fait par l'alimentation physique de l'Arduino. Au démarrage, un écran d'accueil (1,5 s) précède l'entrée automatique dans le menu principal.

## 6. Perspectives d'évolution

- Sauvegarde des enregistrements en EEPROM pour qu'ils survivent à une coupure d'alimentation.
- Ajout d'un réglage de volume (broche PWM dédiée + potentiomètre supplémentaire, ou bouton +/-).
- Affichage d'un meilleur score / historique en mode Jeu.
- Mode "endless" en mode Jeu : séquences de longueur croissante sans limite de niveaux.
- Export/import de mélodies enregistrées via le port série.

## 7. Outils et assistance

Le code du sketch (`piano_diatonique.ino`, `i2c_scanner.ino`) est le fruit du travail de l'auteur. Une IA (Claude) a été sollicitée à deux titres pendant le projet : pour la rédaction et la mise en forme de cette documentation (README, dossier de conception, documentation technique), et ponctuellement pendant le développement pour du débogage et des vérifications — par exemple la validation des notes des mélodies préchargées du mode Sons.
