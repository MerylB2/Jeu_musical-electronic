/*
  =====================================================================
  PIANO DIATONIQUE - JEU MUSICAL ARDUINO (version Arduino MEGA 2560
  + shield TFT 2.8" type MCUFRIEND, ecran couleur au lieu du LCD I2C)
  =====================================================================
  Meme jeu que la version Uno/LCD I2C (voir piano_diatonique_uno_lcd_i2c.ino.bak) :
    1) PIANO      -> joue les notes de la gamme diatonique (Do a Do)
    2) JEU        -> "Simon" musical, 3 niveaux
    3) SONS       -> jukebox de melodies precharges + enregistrement

  POURQUOI UN MEGA ET PAS UN UNO ?
  Le shield TFT 2.8" (type MCUFRIEND) est un shield qui se clipse sur
  les pins de l'Arduino et les occupe presque toutes :
    - bus de donnees LCD (8 bits) -> D2 a D9
    - carte SD (SPI)              -> D10 a D13
    - controle LCD (CS/RS/WR/RD/RESET) -> A0 a A4
  Il ne reste donc RIEN de libre sur un Uno pour les boutons/LED/buzzer.
  Sur un Mega, le shield utilise exactement les memes pins (D2-D13,
  A0-A4) mais le Mega a plein d'autres pins libres (D22 a D53, A6 a
  A15) pour tout le reste du montage.

  CABLAGE (partie ajoutee par nous, en plus du shield TFT clipse) :
    Boutons  -> D22..D29 (INPUT_PULLUP, actifs a l'etat BAS)
    Buzzer   -> D30
    74HC595  -> SER=D31 (data), RCLK=D32 (latch), SRCLK=D33 (clock)
                8 sorties Q0..Q7 -> 8 LED (+ resistance 220-330 ohm) -> GND
    Potar    -> A8 (curseur), extremites sur 5V et GND

  BIBLIOTHEQUES REQUISES (Gestionnaire de bibliotheques Arduino IDE) :
    - Adafruit GFX Library
    - MCUFRIEND_kbv
    (Wire et SD ne sont pas necessaires ici, on n'utilise pas le tactile
     ni la carte SD du shield)
  =====================================================================
*/

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <math.h>

MCUFRIEND_kbv tft;

// ---------------------------------------------------------------------
// Type utilise par le mode SONS (voir remarque dans la version Uno :
// par prudence on le garde tout en haut du fichier egalement ici).
// ---------------------------------------------------------------------
struct MelodyStep { int8_t degree; uint16_t ms; }; // degree=-1 -> silence, degree=99 -> fin de melodie

// ---------------------------------------------------------------------
// CONFIGURATION - broches ajoutees sur le Mega (le shield TFT occupe
// deja D2-D13 et A0-A4, on n'y touche pas)
// ---------------------------------------------------------------------
const uint8_t BTN_PINS[8] = {22, 23, 24, 25, 26, 27, 28, 29};
const uint8_t BUZZER_PIN  = 30;

const uint8_t LED_DATA_PIN  = 31; // SER  (pin 14 du 74HC595)
const uint8_t LED_LATCH_PIN = 32; // RCLK (pin 12 du 74HC595)
const uint8_t LED_CLOCK_PIN = 33; // SRCLK(pin 11 du 74HC595)

const uint8_t POT_PIN = A8;       // potentiometre "key" (tonalite)

const uint8_t BACK_BTN_INDEX = 7;   // dernier bouton (Do aigu) = bouton "retour" en appui long
const uint16_t BACK_HOLD_MS  = 1200;
const uint16_t DEBOUNCE_MS   = 20;

// ---------------------------------------------------------------------
// ECRAN TFT - dimensions et couleurs (RGB565)
// ---------------------------------------------------------------------
const int16_t SCREEN_W = 320; // apres setRotation(1), format paysage
const int16_t SCREEN_H = 240;

#define COL_BG      0x0000 // noir
#define COL_TEXT    0xFFFF // blanc
#define COL_HINT    0x8410 // gris
#define COL_OK      0x07E0 // vert
#define COL_FAIL    0xF800 // rouge
#define COL_MENU    0x001F // bleu
#define COL_GAME    0xFC00 // orange
#define COL_SOUNDS  0x07E0 // vert

// Couleurs des 8 notes, dans l'ordre des boutons (mêmes teintes que les
// boutons physiques d'origine : violet, rouge, orange, jaune, vert,
// cyan, bleu, magenta)
const uint16_t NOTE_COLORS[8] = {
  0x780F, 0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F
};

// ---------------------------------------------------------------------
// GAMME DIATONIQUE
// ---------------------------------------------------------------------
const char* NOTE_NAMES[8] = {"DO", "RE", "MI", "FA", "SOL", "LA", "SI", "DO"};
const uint16_t BASE_FREQ[7] = {523, 587, 659, 698, 784, 880, 988}; // Do5..Si5 (7 notes uniques)

float keySemitones = 0; // mis a jour via le potentiometre en mode Piano

// degree = 0..6 -> Do..Si de l'octave de base, 7..13 -> Do..Si octave au-dessus,
// 14..20 -> encore une octave au-dessus, etc.
uint16_t baseFrequency(uint8_t degree) {
  uint8_t idx = degree % 7;
  uint8_t octave = degree / 7;
  uint16_t f = BASE_FREQ[idx];
  for (uint8_t o = 0; o < octave; o++) f *= 2;
  return f;
}

uint16_t transposedFrequency(uint8_t degree, int shift) {
  return (uint16_t) round(baseFrequency(degree) * pow(2.0, shift / 12.0));
}

// ---------------------------------------------------------------------
// GESTION DES 8 LED VIA LE REGISTRE A DECALAGE 74HC595
// ---------------------------------------------------------------------
uint8_t ledState = 0;

void writeLeds() {
  digitalWrite(LED_LATCH_PIN, LOW);
  shiftOut(LED_DATA_PIN, LED_CLOCK_PIN, MSBFIRST, ledState);
  digitalWrite(LED_LATCH_PIN, HIGH);
}
void ledsAllOff() { ledState = 0; writeLeds(); }
void ledOn(uint8_t i)   { ledState |= (1 << i);  writeLeds(); }
void ledOff(uint8_t i)  { ledState &= ~(1 << i); writeLeds(); }
void ledOnly(uint8_t i) { ledState = (i < 8) ? (1 << i) : 0; writeLeds(); }

// ---------------------------------------------------------------------
// LECTURE DES BOUTONS (bloquant, avec anti-rebond et geste "retour")
// Retour :  0..7 = bouton presse brievement
//           -1   = timeout (aucun bouton avant timeoutMs, 0 = infini)
//           -2   = geste RETOUR (appui long sur le dernier bouton)
// ---------------------------------------------------------------------
int readButtonBlocking(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (timeoutMs == 0 || millis() - start < timeoutMs) {
    for (uint8_t i = 0; i < 8; i++) {
      if (digitalRead(BTN_PINS[i]) == LOW) {
        delay(DEBOUNCE_MS);
        if (digitalRead(BTN_PINS[i]) != LOW) continue; // faux positif
        unsigned long pressStart = millis();
        while (digitalRead(BTN_PINS[i]) == LOW) {
          if (i == BACK_BTN_INDEX && millis() - pressStart >= BACK_HOLD_MS) {
            while (digitalRead(BTN_PINS[i]) == LOW) { /* attend le relachement */ }
            return -2;
          }
        }
        return i;
      }
    }
  }
  return -1;
}

// ---------------------------------------------------------------------
// SON
// ---------------------------------------------------------------------
void playNote(uint8_t degree, uint16_t durationMs, uint16_t freq, bool light) {
  if (light) ledOnly(degree);
  tone(BUZZER_PIN, freq, durationMs);
  delay(durationMs);
  noTone(BUZZER_PIN);
  if (light) ledsAllOff();
  delay(30); // petit blanc entre les notes
}

void playSuccessJingle() {
  uint16_t seq[4] = {659, 784, 988, 1319};
  for (uint8_t i = 0; i < 4; i++) {
    ledOnly(i % 8);
    tone(BUZZER_PIN, seq[i], 120);
    delay(140);
  }
  noTone(BUZZER_PIN);
  ledsAllOff();
}

void playFailSound() {
  uint16_t seq[3] = {392, 330, 220};
  for (uint8_t i = 0; i < 3; i++) {
    ledOnly(7);
    tone(BUZZER_PIN, seq[i], 200);
    delay(220);
    ledsAllOff();
    delay(60);
  }
}

void playVictoryJingle() {
  uint16_t seq[6] = {523, 659, 784, 1047, 784, 1047};
  for (uint8_t i = 0; i < 6; i++) {
    ledOnly(i % 8);
    tone(BUZZER_PIN, seq[i], 150);
    delay(170);
  }
  noTone(BUZZER_PIN);
  ledsAllOff();
}

// ---------------------------------------------------------------------
// DESSIN TFT - remplace l'ecran LCD texte par de vrais graphismes
// ---------------------------------------------------------------------
void clearScreen() {
  tft.fillScreen(COL_BG);
}

// Bandeau de titre colore en haut de l'ecran
void drawHeader(const char* title, uint16_t color) {
  tft.fillRect(0, 0, SCREEN_W, 42, color);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print(title);
}

// Affiche jusqu'a 3 lignes de texte sous le bandeau de titre
void showScreen(const char* title, uint16_t headerColor,
                 const char* line1, const char* line2 = "", const char* line3 = "") {
  clearScreen();
  drawHeader(title, headerColor);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 65);  tft.print(line1);
  if (line2[0]) { tft.setCursor(10, 100); tft.print(line2); }
  if (line3[0]) { tft.setCursor(10, 135); tft.print(line3); }
}

// Centre un texte horizontalement a une taille donnee
void printCentered(const char* text, int16_t y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_W - w) / 2, y);
  tft.print(text);
}

// Petit coeur simple (2 cercles + 1 triangle)
void drawHeartIcon(int16_t cx, int16_t cy, int16_t size, uint16_t color) {
  int16_t r = size / 2;
  tft.fillCircle(cx - r / 2, cy - r / 4, r / 2, color);
  tft.fillCircle(cx + r / 2, cy - r / 4, r / 2, color);
  tft.fillTriangle(cx - r, cy - r / 4, cx + r, cy - r / 4, cx, cy + r, color);
}

// Smiley content : cercle + 2 yeux + sourire (arc de cercle bas)
void drawSmileyIcon(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  tft.drawCircle(cx, cy, r, color);
  tft.fillCircle(cx - r / 3, cy - r / 4, 2, color);
  tft.fillCircle(cx + r / 3, cy - r / 4, 2, color);
  for (int16_t a = 20; a <= 160; a += 5) {
    float rad = a * PI / 180.0;
    int16_t mx = cx + cos(rad) * (r * 0.6);
    int16_t my = cy + sin(rad) * (r * 0.5) + r * 0.1;
    tft.drawPixel(mx, my, color);
    tft.drawPixel(mx, my + 1, color);
  }
}

// Smiley triste : cercle + 2 yeux + moue (arc de cercle inverse, en haut)
void drawSadIcon(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  tft.drawCircle(cx, cy, r, color);
  tft.fillCircle(cx - r / 3, cy - r / 4, 2, color);
  tft.fillCircle(cx + r / 3, cy - r / 4, 2, color);
  for (int16_t a = 20; a <= 160; a += 5) {
    float rad = a * PI / 180.0;
    int16_t mx = cx + cos(rad) * (r * 0.6);
    int16_t my = cy + r * 0.55 - sin(rad) * (r * 0.4);
    tft.drawPixel(mx, my, color);
    tft.drawPixel(mx, my + 1, color);
  }
}

void showPianoScreen(uint8_t degree, const char* noteName, int shift) {
  clearScreen();
  uint16_t c = NOTE_COLORS[degree % 8];
  drawHeader("PIANO", c);
  printCentered(noteName, 80, 7, c);
  tft.setTextSize(2);
  tft.setTextColor(COL_HINT);
  tft.setCursor(10, 190);
  tft.print("Tonalite : ");
  tft.print(shift >= 0 ? "+" : "");
  tft.print(shift);
  tft.setCursor(10, 215);
  tft.print("(maintenir 8 = retour)");
}

// ---------------------------------------------------------------------
// ETATS DU PROGRAMME
// ---------------------------------------------------------------------
enum AppState { MENU, MODE_PIANO, MODE_GAME, MODE_SOUNDS };
AppState state = MENU;

// =======================================================================
// MODE 1 : PIANO DIATONIQUE
// =======================================================================
void modePiano() {
  showScreen("PIANO", COL_MENU, "Joue une note !", "", "Maintenir 8 = retour");
  delay(1200);

  while (true) {
    int pot = analogRead(POT_PIN);
    keySemitones = map(pot, 0, 1023, -6, 6);

    int btn = readButtonBlocking(200); // se re-rafraichit toutes les 200ms pour suivre le potar
    if (btn == -2) return; // retour au menu
    if (btn == -1) continue; // pas d'appui, on ne redessine rien (evite le clignotement)

    uint16_t freq = transposedFrequency(btn, (int)keySemitones);
    showPianoScreen(btn, NOTE_NAMES[btn], (int)keySemitones);
    playNote(btn, 350, freq, true);
  }
}

// =======================================================================
// MODE 2 : JEU - REPRODUIRE LA MELODIE (3 niveaux)
// =======================================================================
const uint8_t GAME_LENGTHS[3] = {4, 6, 8};
const uint16_t GAME_TEMPO[3]  = {500, 420, 340};
uint8_t gameSequence[8];

void generateSequence(uint8_t length) {
  for (uint8_t i = 0; i < length; i++) gameSequence[i] = random(0, 8);
}

void playSequence(uint8_t length, uint16_t tempoMs) {
  for (uint8_t i = 0; i < length; i++) {
    playNote(gameSequence[i], tempoMs, baseFrequency(gameSequence[i]), true);
  }
}

bool wantBack = false;

bool playerReproduce(uint8_t length) {
  wantBack = false;
  for (uint8_t i = 0; i < length; i++) {
    int btn = readButtonBlocking(4000); // 4s pour repondre a chaque note
    if (btn == -2) { wantBack = true; return false; }
    if (btn == -1) return false; // trop lent
    uint16_t freq = baseFrequency(btn);
    playNote(btn, 250, freq, true);
    if (btn != gameSequence[i]) return false;
  }
  return true;
}

void modeGame() {
  showScreen("JEU MUSICAL", COL_GAME, "Reproduis la melodie", "jouee par l'Arduino !");
  delay(1500);

  uint8_t level = 0;
  while (level < 3) {
    uint8_t length = GAME_LENGTHS[level];
    uint16_t tempo = GAME_TEMPO[level];
    generateSequence(length);

    char lvlTxt[24];
    snprintf(lvlTxt, sizeof(lvlTxt), "Niveau %d / 3", level + 1);
    showScreen(lvlTxt, COL_GAME, "Ecoute...");
    delay(1000);

    playSequence(length, tempo);

    showScreen(lvlTxt, COL_GAME, "A toi de jouer !");
    bool ok = playerReproduce(length);

    if (wantBack) return; // retour immediat au menu

    if (ok) {
      clearScreen();
      drawHeader("REUSSI !", COL_OK);
      drawSmileyIcon(90, 140, 40, COL_OK);
      drawHeartIcon(220, 140, 50, COL_FAIL);
      playSuccessJingle();
      delay(1400);
      level++;
      if (level >= 3) {
        clearScreen();
        drawHeader("BRAVO !!!", COL_OK);
        printCentered("3 niveaux finis", 90, 2, COL_TEXT);
        printCentered("Tu es un pro du piano !", 130, 2, COL_TEXT);
        drawHeartIcon(SCREEN_W / 2, 190, 40, COL_FAIL);
        playVictoryJingle();
        delay(2500);
      }
    } else {
      clearScreen();
      drawHeader("ECHEC", COL_FAIL);
      drawSadIcon(SCREEN_W / 2, 140, 45, COL_FAIL);
      printCentered("On recommence !", 200, 2, COL_TEXT);
      playFailSound();
      delay(1200);
      // on reste sur le meme niveau, une nouvelle sequence sera generee
    }
  }

  showScreen("Menu", COL_MENU, "Retour au menu dans 2s...");
  delay(2000);
}

// =======================================================================
// MODE 3 : SONS (jukebox precharge + enregistrement)
// =======================================================================
const MelodyStep SONG1[] = { // Au clair de la lune : Do Do Do Re Mi Re Do Mi Re Re Do
  {0,300},{0,300},{0,300},{1,300},{2,600},
  {1,300},{0,300},{2,300},{1,300},{1,300},{0,600},
  {99,0}
};
const MelodyStep SONG2[] = { // Frere Jacques (simplifie, 2 phrases)
  {0,300},{1,300},{2,300},{0,300},
  {0,300},{1,300},{2,300},{0,300},
  {2,300},{3,300},{4,600},
  {2,300},{3,300},{4,600},
  {99,0}
};
const MelodyStep SONG3[] = { // Joyeux anniversaire : Sol Sol La Sol Do' Si | Sol Sol La Sol Re' Do' |
                              // Sol Sol Sol'(octave) Mi' Do' Si La | Fa Fa Mi Do Re Do
  {4,200},{4,200},{5,350},{4,350},{7,350},{6,700},
  {4,200},{4,200},{5,350},{4,350},{8,350},{7,700},
  {4,200},{4,200},{11,350},{9,350},{7,350},{6,350},{5,700},
  {3,200},{3,200},{2,350},{0,350},{1,350},{0,700},
  {99,0}
};
const MelodyStep* SONGS[3] = { SONG1, SONG2, SONG3 };
const char* SONG_NAMES[3] = { "Au clair de la lune", "Frere Jacques", "Joyeux anniversaire" };

void playSong(const MelodyStep* song) {
  for (uint8_t i = 0; song[i].degree != 99; i++) {
    if (song[i].degree < 0) { delay(song[i].ms); continue; }
    playNote(song[i].degree, song[i].ms, baseFrequency(song[i].degree), true);
  }
}

void playAllSongs() {
  for (uint8_t s = 0; s < 3; s++) {
    showScreen("Lecture", COL_SOUNDS, SONG_NAMES[s]);
    playSong(SONGS[s]);
    delay(400);
  }
}

// --- Enregistrement de melodies personnalisees -------------------------
#define MAX_SLOTS 3
#define MAX_STEPS 30
int8_t  recDegree[MAX_SLOTS][MAX_STEPS];
uint8_t recDurationTicks[MAX_SLOTS][MAX_STEPS]; // unite = 20 ms
uint8_t recLength[MAX_SLOTS] = {0, 0, 0};

void recordNewSound() {
  int pot = analogRead(POT_PIN);
  uint8_t slot = map(pot, 0, 1023, 0, MAX_SLOTS - 1);

  char msg[24];
  snprintf(msg, sizeof(msg), "Emplacement : %d", slot + 1);
  showScreen("ENREGISTREMENT", COL_SOUNDS, msg, "Joue ta melodie...", "(3s de pause = fin)");
  delay(1500);

  uint8_t count = 0;
  unsigned long lastEventTime = millis();

  while (count < MAX_STEPS) {
    int btn = readButtonBlocking(3000); // 3s d'inactivite = fin d'enregistrement
    if (btn == -2 || btn == -1) break;

    unsigned long now = millis();
    uint16_t deltaMs = (uint16_t)min(now - lastEventTime, (unsigned long)5000);
    lastEventTime = now;

    playNote(btn, 250, baseFrequency(btn), true); // retour sonore immediat
    recDegree[slot][count] = btn;
    recDurationTicks[slot][count] = (uint8_t) min((deltaMs + 20) / 20, 255);
    count++;
  }
  recLength[slot] = count;

  char res[24];
  snprintf(res, sizeof(res), "%d notes gardees", count);
  showScreen("Enregistrement", COL_SOUNDS, "Termine !", res);
  delay(1500);
}

void playRecordedSlot(uint8_t slot) {
  if (recLength[slot] == 0) {
    showScreen("Sons", COL_SOUNDS, "Emplacement vide");
    delay(1000);
    return;
  }
  char msg[24];
  snprintf(msg, sizeof(msg), "Emplacement %d", slot + 1);
  showScreen("Lecture", COL_SOUNDS, msg);
  for (uint8_t i = 0; i < recLength[slot]; i++) {
    uint16_t dur = max((uint16_t)(recDurationTicks[slot][i] * 20), (uint16_t)150);
    playNote(recDegree[slot][i], min(dur, (uint16_t)900), baseFrequency(recDegree[slot][i]), true);
  }
}

void playAllRecorded() {
  for (uint8_t s = 0; s < MAX_SLOTS; s++) {
    if (recLength[s] == 0) continue;
    playRecordedSlot(s);
    delay(400);
  }
}

// --- Sous-menu du mode SONS ---------------------------------------------
void modeSounds() {
  while (true) {
    showScreen("SONS", COL_SOUNDS,
      "1-3: melodie   4: tout",
      "5: enregistrer 6: lire",
      "7: tout lire  (8=retour)");
    int btn = readButtonBlocking(0);
    if (btn == -2) return;

    switch (btn) {
      case 0: showScreen("Lecture", COL_SOUNDS, SONG_NAMES[0]); playSong(SONGS[0]); break;
      case 1: showScreen("Lecture", COL_SOUNDS, SONG_NAMES[1]); playSong(SONGS[1]); break;
      case 2: showScreen("Lecture", COL_SOUNDS, SONG_NAMES[2]); playSong(SONGS[2]); break;
      case 3: playAllSongs(); break;
      case 4: recordNewSound(); break;
      case 5: {
        int pot = analogRead(POT_PIN);
        uint8_t slot = map(pot, 0, 1023, 0, MAX_SLOTS - 1);
        playRecordedSlot(slot);
        break;
      }
      case 6: playAllRecorded(); break;
      default: break; // bouton 7 (index) reserve au retour (geste long deja gere)
    }
  }
}

// =======================================================================
// MENU PRINCIPAL
// =======================================================================
void showMenu() {
  showScreen("MENU PRINCIPAL", COL_MENU,
    "1: Piano      2: Jeu",
    "3: Sons       4: Pause",
    "Maintenir 8 = retour");
}

// ---------------------------------------------------------------------
// MUSIQUE DE FOND - jouee en boucle tant qu'on est au menu et qu'aucun
// bouton n'est presse ("quand on ne fait rien").
// ---------------------------------------------------------------------
const MelodyStep BG_MUSIC[] = { // petit arpege Do-Mi-Sol-Do'-Sol-Mi, en boucle
  {0, 280}, {2, 280}, {4, 280}, {7, 280}, {4, 280}, {2, 280},
  {99, 0}
};

int playBgNoteAndListen(uint8_t degree, uint16_t durationMs) {
  ledOnly(degree % 8);
  tone(BUZZER_PIN, baseFrequency(degree), durationMs);
  int btn = readButtonBlocking(durationMs);
  noTone(BUZZER_PIN);
  ledsAllOff();
  return btn;
}

bool bgMusicOn = true; // bouton 4 = pause/reprise

int waitForButtonWithMusic() {
  uint8_t i = 0;
  while (true) {
    int btn;
    if (bgMusicOn) {
      if (BG_MUSIC[i].degree == 99) i = 0; // reboucle la musique
      btn = playBgNoteAndListen(BG_MUSIC[i].degree, BG_MUSIC[i].ms);
      i++;
    } else {
      btn = readButtonBlocking(300); // silence, mais on continue de scruter les boutons
    }
    if (btn == 3) { bgMusicOn = !bgMusicOn; i = 0; continue; } // bouton 4 = pause/reprise
    if (btn == 0 || btn == 1 || btn == 2) return btn; // seuls 1/2/3 lancent un mode
  }
}

// =======================================================================
// SETUP / LOOP
// =======================================================================
void setup() {
  for (uint8_t i = 0; i < 8; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_DATA_PIN, OUTPUT);
  pinMode(LED_LATCH_PIN, OUTPUT);
  pinMode(LED_CLOCK_PIN, OUTPUT);
  ledsAllOff();

  randomSeed(analogRead(A15)); // pin libre non utilisee sur le Mega

  uint16_t id = tft.readID();
  if (id == 0xD3D3) id = 0x9481; // certains clones ne renvoient pas un ID standard
  tft.begin(id);
  tft.setRotation(1); // format paysage (320x240)

  clearScreen();
  printCentered("Piano", 60, 4, COL_TEXT);
  printCentered("Diatonique", 110, 3, COL_TEXT);
  printCentered("Jeu musical", 160, 2, COL_HINT);
  delay(1500);
  state = MENU;
}

void loop() {
  switch (state) {
    case MENU: {
      showMenu();
      int btn = waitForButtonWithMusic(); // musique de fond en boucle tant qu'on ne joue pas
      if (btn == 0) state = MODE_PIANO;
      else if (btn == 1) state = MODE_GAME;
      else if (btn == 2) state = MODE_SOUNDS;
      break;
    }
    case MODE_PIANO:
      modePiano();
      state = MENU;
      break;
    case MODE_GAME:
      modeGame();
      state = MENU;
      break;
    case MODE_SOUNDS:
      modeSounds();
      state = MENU;
      break;
  }
}
