# 🎹 Piano Diatonique — Jeu Musical Arduino

Jouet musical électronique sur Arduino Uno : un piano diatonique 8 notes avec écran LCD, LED et buzzer, proposant 3 modes de jeu — piano libre, "Simon" musical à 3 niveaux, et jukebox avec enregistrement de mélodies.

## Sommaire

- [Fonctionnalités](#fonctionnalités)
- [Matériel nécessaire](#matériel-nécessaire)
- [Câblage rapide](#câblage-rapide)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Structure du dépôt](#structure-du-dépôt)
- [Documentation](#documentation)
- [Limites connues](#limites-connues)
- [Licence](#licence)

## Fonctionnalités

**🎼 Mode Piano** — joue les notes de la gamme diatonique (Do à Do) au fil des appuis, avec un potentiomètre pour transposer la tonalité en temps réel (affichée "key: +X").

**🎯 Mode Jeu (Simon musical)** — une mélodie aléatoire est jouée, à reproduire dans l'ordre. Échec → son + émoji triste, nouvel essai sur le même niveau. Réussite → son + émoji content + cœur, passage au niveau suivant (3 niveaux, de plus en plus longs et rapides).

**🎵 Mode Sons** — jukebox de 3 mélodies préchargées (Au clair de la lune, Frère Jacques, Joyeux anniversaire — notes vérifiées) jouables une par une ou à la suite, et enregistrement de mélodies personnalisées (3 emplacements) rejouables individuellement ou à la suite.

**🎶 Musique de fond** — un petit arpège tourne en boucle au menu principal tant qu'aucun mode n'est lancé ; un bouton dédié le met en pause/relance à la demande.

**8 LED synchronisées** — une par bouton, pilotées par un registre à décalage, s'allument avec chaque note jouée (par l'utilisateur ou par l'Arduino).

## Matériel nécessaire

| Composant | Quantité |
|---|---|
| Arduino Uno | 1 |
| Écran LCD I2C 16x2 (ou 20x4) | 1 |
| Boutons poussoir | 8 |
| LED | 8 |
| Résistances 220-330 Ω | 8 |
| Registre à décalage 74HC595 | 1 |
| Buzzer / haut-parleur | 1 |
| Potentiomètre 10 kΩ | 1 |
| Breadboard + fils | - |

## Câblage rapide

| Fonction | Pin Arduino |
|---|---|
| Boutons (notes 1 à 8) | D2 → D9 |
| Buzzer | D10 |
| 74HC595 — SER / RCLK / SRCLK | D11 / D12 / D13 |
| Potentiomètre (curseur) | A1 |
| LCD I2C — SDA / SCL | A4 / A5 |

Détail complet (brochage du 74HC595, valeurs de résistances, schéma) dans [`docs/doc-technique.md`](docs/doc-technique.md).

## Installation

1. Installe la bibliothèque **LiquidCrystal_I2C** de Frank de Brabander :
   [github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library](https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library)
   (via *Croquis → Inclure une bibliothèque → Gérer les bibliothèques*, ou en ZIP).
2. Câble le montage selon le tableau ci-dessus.
3. Si l'écran reste blanc, upload d'abord `i2c_scanner.ino` pour vérifier l'adresse I2C (`0x27` par défaut) et règle le potentiomètre de contraste du module LCD.
4. Ouvre `piano_diatonique.ino`, adapte si besoin `LCD_ADDR`, `LCD_COLS`, `LCD_ROWS` en haut du fichier à ton écran.
5. Compile et upload sur l'Arduino Uno.

## Utilisation

Au menu principal :

| Bouton | Action |
|---|---|
| 1 | Mode Piano |
| 2 | Mode Jeu |
| 3 | Mode Sons |
| 4 | Pause / reprise de la musique de fond |
| 8 (maintenu ~1,2 s) | Retour au menu, depuis n'importe quel mode |

Dans le sous-menu Sons :

| Bouton | Action |
|---|---|
| 1-3 | Jouer la mélodie préchargée correspondante |
| 4 | Jouer les 3 mélodies préchargées à la suite |
| 5 | Enregistrer une nouvelle mélodie (potentiomètre = choix de l'emplacement 1-3) |
| 6 | Rejouer l'enregistrement choisi par le potentiomètre |
| 7 | Rejouer tous les enregistrements à la suite |

## Structure du dépôt

```
piano_diatonique/
├── piano_diatonique.ino   # Sketch principal
├── i2c_scanner.ino        # Utilitaire de diagnostic (trouver l'adresse I2C du LCD)
├── README.md               # Ce fichier
└── docs/
    ├── doc-conceptuel.md   # Cahier des charges, choix de conception, analyse fonctionnelle
    └── doc-technique.md    # Architecture logicielle, brochage détaillé, référence du code
```

## Documentation

- 📐 [**Dossier de conception**](docs/doc-conceptuel.md) — objectifs du projet, cahier des charges, choix technologiques justifiés, analyse fonctionnelle des 3 modes.
- 🔧 [**Documentation technique**](docs/doc-technique.md) — architecture du programme, brochage complet, structures de données, algorithmes clés, budget mémoire.

## Limites connues

- Les enregistrements du mode Sons sont en RAM : perdus à la coupure d'alimentation (pas de sauvegarde EEPROM pour l'instant).
- L'instrument ne couvre qu'une octave (8 boutons) : les mélodies qui dépassent cette plage (ex. Joyeux Anniversaire) sont rejouées avec les notes réelles grâce à un système de fréquences multi-octave, mais les LED ne s'allument que pour les notes correspondant à un vrai bouton.
- Écran testé en 16x2 ; le code s'adapte normalement à un 20x4 mais n'a pas été validé sur ce format.

## Licence

Projet personnel et pédagogique, partagé pour apprendre et s'en inspirer. Aucune licence n'est définie pour l'instant — ajoute-en une (MIT, par exemple) avant toute réutilisation ou publication officielle.
