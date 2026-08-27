/*
  =====================================================================
  PIANO DIATONIQUE - JEU MUSICAL ARDUINO
  =====================================================================
  3 modes :
    1) PIANO      -> joue les notes de la gamme diatonique (Do a Do)
    2) JEU        -> "Simon" musical : reproduis la melodie jouee
                      (3 niveaux, echec = son + emoji triste puis on
                       recommence, reussite = son + emoji content+coeur
                       puis niveau suivant)
    3) SONS       -> jukebox de melodies precharges + enregistrement
                      de tes propres melodies, avec lecture a la suite

  MATERIEL :
    - Arduino Uno
    - Ecran LCD I2C (16x2 ou 20x4, voir LCD_COLS/LCD_ROWS ci-dessous)
    - 8 boutons poussoir (notes DO RE MI FA SOL LA SI DO)
    - 8 LED (une par bouton), pilotees via un registre a decalage 74HC595
    - 1 buzzer / haut-parleur
    - 1 potentiometre (choix de la tonalite "key" en mode Piano)

  CABLAGE (voir README.md pour le detail et les valeurs de resistances) :
    Boutons  -> D2..D9  (INPUT_PULLUP, actifs a l'etat BAS)
    Buzzer   -> D10
    74HC595  -> SER=D11 (data), RCLK=D12 (latch), SRCLK=D13 (clock)
                8 sorties Q0..Q7 -> 8 LED (+ resistance 220-330 ohm) -> GND
    Potar    -> A1 (curseur), extremites sur 5V et GND
    LCD I2C  -> SDA=A4, SCL=A5, VCC=5V, GND=GND

  BIBLIOTHEQUES REQUISES (Gestionnaire de bibliotheques Arduino IDE) :
    - LiquidCrystal_I2C  (par Frank de Brabander)
    - Wire (fournie avec l'IDE)
  =====================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

// ---------------------------------------------------------------------
// Type utilise par le mode SONS (doit rester tout en haut du fichier :
// l'IDE Arduino genere automatiquement les prototypes de fonctions et
// les place juste apres les #include, AVANT le reste du code. Si ce
// type est defini plus bas, la prototype generee pour playSong(...)
// ne le connait pas encore -> erreur "MelodyStep does not name a type")
// ---------------------------------------------------------------------
struct MelodyStep { int8_t degree; uint16_t ms; }; // degree=-1 -> silence, degree=99 -> fin de melodie

// ---------------------------------------------------------------------
// CONFIGURATION - a adapter a ton montage
// ---------------------------------------------------------------------
const uint8_t LCD_ADDR = 0x27;   // adresse I2C du LCD (0x27 ou 0x3F le plus souvent)
const uint8_t LCD_COLS = 16;     // 16 ou 20
const uint8_t LCD_ROWS = 2;      // 2 ou 4  (l'affichage s'adapte automatiquement)

const uint8_t BTN_PINS[8] = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t BUZZER_PIN  = 10;

const uint8_t LED_DATA_PIN  = 11; // SER  (pin 14 du 74HC595)
const uint8_t LED_LATCH_PIN = 12; // RCLK (pin 12 du 74HC595)
const uint8_t LED_CLOCK_PIN = 13; // SRCLK(pin 11 du 74HC595)

const uint8_t POT_PIN = A1;       // potentiometre "key" (tonalite)

const uint8_t BACK_BTN_INDEX = 7;   // dernier bouton (Do aigu) = bouton "retour" en appui long
const uint16_t BACK_HOLD_MS  = 1200;
const uint16_t DEBOUNCE_MS   = 20;

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ---------------------------------------------------------------------
// CARACTERES PERSONNALISES LCD (emojis simplifies)
// ---------------------------------------------------------------------
byte CHAR_HEART[8] = {
  0b00000, 0b01010, 0b11111, 0b11111,
  0b11111, 0b01110, 0b00100, 0b00000
};
byte CHAR_HAPPY[8] = {
  0b00000, 0b01010, 0b00000, 0b10001,
  0b01110, 0b00000, 0b00000, 0b00000
};
byte CHAR_SAD[8] = {
  0b00000, 0b01010, 0b00000, 0b00000,
  0b01110, 0b10001, 0b00000, 0b00000
};
byte CHAR_NOTE[8] = {
  0b00100, 0b00110, 0b00101, 0b00100,
  0b11100, 0b11100, 0b00000, 0b00000
};
#define ICO_HEART 0
#define ICO_HAPPY 1
#define ICO_SAD   2
#define ICO_NOTE  3

// ---------------------------------------------------------------------
// GAMME DIATONIQUE
// ---------------------------------------------------------------------
const char* NOTE_NAMES[8] = {"DO", "RE", "MI", "FA", "SOL", "LA", "SI", "DO"};
const uint16_t BASE_FREQ[7] = {523, 587, 659, 698, 784, 880, 988}; // Do5..Si5 (7 notes uniques)

float keySemitones = 0; // mis a jour via le potentiometre en mode Piano

// degree = 0..6 -> Do..Si de l'octave de base, 7..13 -> Do..Si octave au-dessus,
// 14..20 -> encore une octave au-dessus, etc. Permet de coder des melodies qui
// depassent une simple octave (ex: Joyeux Anniversaire) tout en restant
// compatible avec les boutons 0..7 utilises en mode Piano/Jeu (7 == Do aigu).
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
    ledOnly(7); // clignote sur le dernier bouton en signe d'erreur
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
// AFFICHAGE LCD (s'adapte a 2 ou 4 lignes)
// ---------------------------------------------------------------------
void lcdClearLine(uint8_t row) {
  lcd.setCursor(0, row);
  for (uint8_t i = 0; i < LCD_COLS; i++) lcd.print(' ');
}

void showPianoScreen(const char* noteName, int shift) {
  lcd.clear();
  if (LCD_ROWS >= 4) {
    lcd.setCursor(0, 0); lcd.print("Piano");
    lcd.setCursor(0, 1); lcd.print("Diatonique");
    lcd.setCursor(0, 2); lcd.print("key: "); lcd.print(shift >= 0 ? "+" : ""); lcd.print(shift);
    lcd.setCursor(0, 3); lcd.print(noteName);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Piano key:");
    lcd.print(shift >= 0 ? "+" : ""); lcd.print(shift);
    lcd.setCursor(0, 1);
    lcd.print("Note: "); lcd.print(noteName);
  }
}

void showMessage(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  if (LCD_ROWS >= 2) { lcd.setCursor(0, 1); lcd.print(line2); }
  if (LCD_ROWS >= 4) { lcd.setCursor(0, 2); lcd.print(line3); lcd.setCursor(0, 3); lcd.print(line4); }
  else if (LCD_ROWS == 2 && line3[0] != 0) { /* pas de place, ignore */ }
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
  showMessage("PIANO", "Joue une note !", "(bouton DO aigu", "maintenu = retour)");
  delay(1200);

  while (true) {
    int pot = analogRead(POT_PIN);
    keySemitones = map(pot, 0, 1023, -6, 6);

    int btn = readButtonBlocking(200); // se re-rafraichit toutes les 200ms pour suivre le potar
    if (btn == -2) return; // retour au menu
    if (btn == -1) {
      // pas d'appui : on rafraichit juste l'ecran avec la derniere note vide
      showPianoScreen("--", (int)keySemitones);
      continue;
    }
    uint16_t freq = transposedFrequency(btn, (int)keySemitones);
    showPianoScreen(NOTE_NAMES[btn], (int)keySemitones);
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

// renvoie true si le joueur a reussi, false si echec, et laisse la
// possibilite de sortir via le geste retour (auquel cas on renvoie false
// et applique un drapeau global 'wantBack')
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
  showMessage("JEU MUSICAL", "Reproduis la", "melodie jouee !", "");
  delay(1500);

  uint8_t level = 0;
  while (level < 3) {
    uint8_t length = GAME_LENGTHS[level];
    uint16_t tempo = GAME_TEMPO[level];
    generateSequence(length);

    char lvlTxt[17];
    snprintf(lvlTxt, sizeof(lvlTxt), "Niveau %d / 3", level + 1);
    showMessage(lvlTxt, "Ecoute...", "", "");
    delay(1000);

    playSequence(length, tempo);

    showMessage(lvlTxt, "A toi de jouer !", "", "");
    bool ok = playerReproduce(length);

    if (wantBack) return; // retour immediat au menu

    if (ok) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("REUSSI !");
      lcd.write(ICO_HAPPY); lcd.write(ICO_HEART);
      playSuccessJingle();
      delay(1200);
      level++;
      if (level >= 3) {
        showMessage("BRAVO !!!", "3 niveaux finis", "Tu es un pro", "du piano !");
        lcd.setCursor(15, 0); lcd.write(ICO_HEART);
        playVictoryJingle();
        delay(2500);
      }
    } else {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("ECHEC");
      lcd.write(ICO_SAD);
      lcd.setCursor(0, 1); lcd.print("On recommence !");
      playFailSound();
      delay(1200);
      // on reste sur le meme niveau, une nouvelle sequence sera generee
    }
  }

  showMessage("Retour au menu", "dans 2s...", "", "");
  delay(2000);
}

// =======================================================================
// MODE 3 : SONS (jukebox precharge + enregistrement)
// =======================================================================
// (struct MelodyStep definie tout en haut du fichier, voir remarque)

// Melodies verifiees note par note (solfege confirme par recherche), avec
// durees approximatives. Les degres vont de 0 a 13 : 0..6 = Do..Si de
// l'octave de base (0=Do, 7=Do aigu, comme les boutons), 7..13 = Do..Si de
// l'octave suivante (voir baseFrequency() plus haut). Necessaire pour
// "Joyeux Anniversaire" qui depasse une simple octave.
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
const char* SONG_NAMES[3] = { "Au clair lune", "Frere Jacques", "Anniversaire" };

void playSong(const MelodyStep* song) {
  for (uint8_t i = 0; song[i].degree != 99; i++) {
    if (song[i].degree < 0) { delay(song[i].ms); continue; }
    playNote(song[i].degree, song[i].ms, baseFrequency(song[i].degree), true);
  }
}

void playAllSongs() {
  for (uint8_t s = 0; s < 3; s++) {
    showMessage("Lecture :", SONG_NAMES[s], "", "");
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

  char msg[17];
  snprintf(msg, sizeof(msg), "Slot: %d", slot + 1);
  showMessage("ENREGISTREMENT", msg, "Joue ta melodie", "(3s pause= stop)");
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

  char res[17];
  snprintf(res, sizeof(res), "%d notes gardees", count);
  showMessage("Enregistrement", "termine !", res, "");
  delay(1500);
}

void playRecordedSlot(uint8_t slot) {
  if (recLength[slot] == 0) {
    showMessage("Slot vide", "", "", "");
    delay(1000);
    return;
  }
  char msg[17];
  snprintf(msg, sizeof(msg), "Lecture slot %d", slot + 1);
  showMessage(msg, "", "", "");
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
    if (LCD_ROWS >= 4) {
      showMessage("SONS - Menu", "1-3:air 4:tout", "5:enreg 6:lire", "7:lire tout");
    } else {
      showMessage("1-3:air 4:tous", "5:enr 6:lit 7:tt");
    }
    int btn = readButtonBlocking(0);
    if (btn == -2) return;

    switch (btn) {
      case 0: showMessage("Lecture :", SONG_NAMES[0], "", ""); playSong(SONGS[0]); break;
      case 1: showMessage("Lecture :", SONG_NAMES[1], "", ""); playSong(SONGS[1]); break;
      case 2: showMessage("Lecture :", SONG_NAMES[2], "", ""); playSong(SONGS[2]); break;
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
  if (LCD_ROWS >= 4) {
    showMessage("MENU PRINCIPAL", "1:Piano  2:Jeu", "3:Sons", "(maintenir 8=retour)");
  } else {
    showMessage("1:Piano 2:Jeu", "3:Sons  8=retour");
  }
}

// ---------------------------------------------------------------------
// MUSIQUE DE FOND - jouee en boucle tant qu'on est au menu et qu'aucun
// bouton n'est presse ("quand on ne fait rien").
// ---------------------------------------------------------------------
const MelodyStep BG_MUSIC[] = { // petit arpege Do-Mi-Sol-Do'-Sol-Mi, en boucle
  {0, 280}, {2, 280}, {4, 280}, {7, 280}, {4, 280}, {2, 280},
  {99, 0}
};

// Joue une note de la musique de fond SANS bloquer les autres pins :
// tone(...,duree) est non-bloquant sur Arduino, donc pendant qu'elle sonne
// on peut quand meme scruter les boutons via readButtonBlocking(duree).
// Renvoie 0..7 si un bouton est presse, -2 pour le geste retour, -1 si la
// note s'est terminee sans qu'on appuie sur rien.
int playBgNoteAndListen(uint8_t degree, uint16_t durationMs) {
  ledOnly(degree % 8);
  tone(BUZZER_PIN, baseFrequency(degree), durationMs);
  int btn = readButtonBlocking(durationMs);
  noTone(BUZZER_PIN);
  ledsAllOff();
  return btn;
}

// Bouton 4 (index 3) = pause/reprise de la musique de fond.
bool bgMusicOn = true;

// Attend un appui bouton (0..7) en jouant BG_MUSIC en boucle pendant l'attente.
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
    // les autres boutons (5-8), -1 (fin de note/silence) ou -2 (retour) -> on continue
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

  randomSeed(analogRead(A0)); // pin libre non utilisee, pour un tirage different a chaque demarrage

  lcd.begin();
  lcd.backlight();
  lcd.createChar(ICO_HEART, CHAR_HEART);
  lcd.createChar(ICO_HAPPY, CHAR_HAPPY);
  lcd.createChar(ICO_SAD,   CHAR_SAD);
  lcd.createChar(ICO_NOTE,  CHAR_NOTE);

  showMessage("Piano", "Diatonique", "Jeu musical", "");
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
