// Editeur de texte hors ligne pour NumWorks (application externe)
// Remplace src/main.cpp du modele epsilon-sample-app-cpp.
//
// Mettre USE_STORAGE a 1 une fois storage.c / storage.h ajoutes au projet
// pour activer la sauvegarde automatique.

#include "eadkpp.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Informations lues par nwlink pour fabriquer le fichier .nwa
extern "C" const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Texte";
extern "C" const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#define USE_STORAGE 1

#if USE_STORAGE
#include "storage.h"
#endif

#define FILE_NAME "texte.py"   // visible aussi dans l'application Python

#define MAX 4000     // nombre maximum de caracteres
#define COLS 45      // caracteres par ligne a l'ecran (police petite)
#define CW 7         // largeur d'un caractere (pixels)
#define CH 14        // hauteur d'une ligne (pixels)
#define ROWS 16      // lignes de texte visibles (1 ligne de plus pour l'etat)

static const eadk_color_t WHITE = 0xFFFF;
static const eadk_color_t BLACK = 0x0000;
static const eadk_color_t GRAY = 0x39E7;

static char text[MAX + 1];
static int len = 0;       // taille du texte
static int pos = 0;       // position du curseur
static int top = 0;       // premiere ligne affichee
static bool saved_ok = false;

// Codes des evenements du clavier (EADK)
enum {
  EV_LEFT = 0, EV_UP = 1, EV_DOWN = 2, EV_RIGHT = 3, EV_OK = 4, EV_BACK = 5,
  EV_BACKSPACE = 17, EV_EXE = 52
};

// Convertit un evenement en caractere (0 si ce n'est pas un caractere)
static char eventToChar(uint16_t e) {
  if (e >= 126 && e <= 142) return 'a' + (e - 126);   // a..q
  if (e >= 144 && e <= 148) return 'r' + (e - 144);   // r..v
  if (e >= 150 && e <= 153) return 'w' + (e - 150);   // w..z
  if (e >= 180 && e <= 196) return 'A' + (e - 180);   // A..Q
  if (e >= 198 && e <= 202) return 'R' + (e - 198);   // R..V
  if (e >= 204 && e <= 207) return 'W' + (e - 204);   // W..Z
  switch (e) {
    case 48: return '0'; case 42: return '1'; case 43: return '2';
    case 44: return '3'; case 36: return '4'; case 37: return '5';
    case 38: return '6'; case 30: return '7'; case 31: return '8';
    case 32: return '9';
    case 154: return ' ';  case 156: return '?'; case 157: return '!';
    case 22: return ',';   case 122: return ':'; case 123: return ';';
    case 124: return '"';  case 125: return '%'; case 45: return '+';
    case 46: return '-';   case 39: return '*';  case 40: return '/';
    case 49: return '.';   case 81: return '=';  case 76: return '_';
    case 83: return '>';   case 82: return '<';  case 33: return '(';
    case 34: return ')';   case 74: return '{';  case 75: return '}';
    case 72: return '[';   case 73: return ']';
  }
  return 0;
}

// --- Sauvegarde ---------------------------------------------------------

static char filebuf[MAX + 3];

static void load() {
#if USE_STORAGE
  size_t l = 0;
  const char * c = extapp_fileRead(FILE_NAME, &l);
  if (c != NULL && l > 1) {
    const char * s = c + 1;  // le premier octet est le drapeau "autoimport"
    int i = 0;
    while (i < MAX && s[i] != 0) { text[i] = s[i]; i++; }
    len = i;
    pos = len;
    saved_ok = true;
  }
#endif
}

static void save() {
#if USE_STORAGE
  filebuf[0] = 0;  // 0 = pas d'import automatique en Python
  memcpy(filebuf + 1, text, len);
  filebuf[len + 1] = 0;
  saved_ok = extapp_fileWrite(FILE_NAME, filebuf, len + 2);
#endif
}

// --- Affichage ----------------------------------------------------------

static void locate(int idx, int * row, int * col) {
  int r = 0, c = 0;
  for (int i = 0; i < idx; i++) {
    if (text[i] == '\n') { r++; c = 0; }
    else { c++; if (c == COLS) { r++; c = 0; } }
  }
  *row = r;
  *col = c;
}

static int indexAt(int row, int col) {
  int r = 0, c = 0, i = 0;
  while (i < len && r < row) {
    if (text[i] == '\n') { r++; c = 0; }
    else { c++; if (c == COLS) { r++; c = 0; } }
    i++;
  }
  if (r < row) return len;
  int start = i;
  while (i < len && (i - start) < col && text[i] != '\n') i++;
  return i;
}

static void drawRow(char * line, int n, int r) {
  if (r < top || r >= top + ROWS) return;
  line[n] = 0;
  eadk_point_t p = {0, (uint16_t)((r - top + 1) * CH)};
  eadk_display_draw_string(line, p, false, BLACK, WHITE);
}

static int numToStr(int v, char * out) {
  char tmp[12];
  int n = 0, k = 0;
  if (v == 0) tmp[n++] = '0';
  while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
  while (n > 0) out[k++] = tmp[--n];
  return k;
}

static void draw() {
  int crow, ccol;
  locate(pos, &crow, &ccol);
  if (crow < top) top = crow;
  if (crow >= top + ROWS) top = crow - ROWS + 1;

  eadk_rect_t full = {0, 0, 320, 240};
  eadk_display_push_rect_uniform(full, WHITE);

  // barre d'etat
  eadk_rect_t bar = {0, 0, 320, CH};
  eadk_display_push_rect_uniform(bar, GRAY);
  char st[48];
  int k = 0;
  st[k++] = 'L'; st[k++] = 'n'; st[k++] = ' ';
  k += numToStr(crow + 1, st + k);
  st[k++] = ' '; st[k++] = ' ';
  k += numToStr(len, st + k);
  st[k++] = '/';
  k += numToStr(MAX, st + k);
  const char * etat = saved_ok ? "  [sauve]" : "  [non sauve]";
  for (int i = 0; etat[i] != 0; i++) st[k++] = etat[i];
  st[k] = 0;
  eadk_point_t sp = {2, 0};
  eadk_display_draw_string(st, sp, false, WHITE, GRAY);

  // texte
  char line[COLS + 1];
  int r = 0, n = 0;
  for (int i = 0; i <= len; i++) {
    if (i == len) {
      drawRow(line, n, r);
    } else if (text[i] == '\n') {
      drawRow(line, n, r);
      r++; n = 0;
    } else {
      line[n++] = text[i];
      if (n == COLS) { drawRow(line, n, r); r++; n = 0; }
    }
  }

  // curseur
  eadk_rect_t cur = {(uint16_t)(ccol * CW), (uint16_t)((crow - top + 1) * CH),
                     2, CH};
  eadk_display_push_rect_uniform(cur, BLACK);
}

// --- Edition ------------------------------------------------------------

static bool insertChar(char c) {
  if (len >= MAX) return false;
  memmove(text + pos + 1, text + pos, len - pos);
  text[pos] = c;
  pos++;
  len++;
  return true;
}

static bool deleteBefore() {
  if (pos == 0) return false;
  memmove(text + pos - 1, text + pos, len - pos);
  pos--;
  len--;
  return true;
}

int main(int argc, char * argv[]) {
  load();
  bool dirty = false;
  int edits = 0;
  draw();

  while (true) {
    int32_t timeout = 1500;  // ms : sans touche, on sauvegarde
    uint16_t e = (uint16_t)eadk_event_get(&timeout);
    bool changed = false;
    bool moved = false;

    if (e == EV_BACK) {
      save();
      return 0;
    } else if (e == EV_LEFT) {
      if (pos > 0) { pos--; moved = true; }
    } else if (e == EV_RIGHT) {
      if (pos < len) { pos++; moved = true; }
    } else if (e == EV_UP || e == EV_DOWN) {
      int row, col;
      locate(pos, &row, &col);
      int target = (e == EV_UP) ? row - 1 : row + 1;
      if (target >= 0) { pos = indexAt(target, col); moved = true; }
    } else if (e == EV_BACKSPACE) {
      changed = deleteBefore();
    } else if (e == EV_EXE || e == EV_OK) {
      changed = insertChar('\n');
    } else {
      char c = eventToChar(e);
      if (c != 0) changed = insertChar(c);
    }

    if (changed) {
      dirty = true;
      saved_ok = false;
      edits++;
    }

    // sauvegarde apres une pause, ou toutes les 20 modifications
    if (dirty && (!changed || edits >= 20)) {
      save();
      dirty = false;
      edits = 0;
      changed = true;  // pour redessiner l'etat
    }

    if (changed || moved) draw();
  }
  return 0;
}
