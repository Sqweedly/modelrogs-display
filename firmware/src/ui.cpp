#include "ui.h"
#include "fonts/inter_fonts.h"

TFT_eSPI tft;

static const uint8_t* const FONT_DATA[] = { F11, F13, F16, F18, F32, F52, F72, F26 };
static int8_t currentFont = -1;

void uiBegin() {
  tft.init();

  // TFT_eSPI règle les tensions et le gamma de l'écran pour d'autres modèles
  // de dalle : sur le SmallTV, les noirs deviennent gris clair. On réinitialise
  // la dalle puis on applique la même séquence minimale que l'ancien firmware
  // (Adafruit_ST7789) : réglages d'usine de la dalle conservés.
  tft.writecommand(0x01);      // SWRESET : registres remis aux valeurs d'usine
  delay(150);
  tft.writecommand(0x11);      // SLPOUT
  delay(120);
  tft.writecommand(0x3A);      // COLMOD : couleurs 16 bits
  tft.writedata(0x55);
  delay(10);
#ifdef TFT_INVERSION_OFF
  tft.writecommand(0x20);      // INVOFF
#else
  tft.writecommand(0x21);      // INVON (comme l'ancien firmware)
#endif
  tft.writecommand(0x13);      // NORON
  delay(10);
  tft.writecommand(0x29);      // DISPON
  delay(10);

  // Rotation 0 de TFT_eSPI = rotation 2 d'Adafruit (orientation de l'ancien firmware)
  tft.setRotation(0);
  tft.setTextWrap(false, false);
  tft.fillScreen(Col::BG);
}

void useFont(FontId f) {
  if (currentFont == (int8_t)f) return;
  tft.loadFont(FONT_DATA[f]);  // lit les métriques depuis la flash (PROGMEM)
  currentFont = f;
}

int drawText(const String& s, int x, int y, FontId f, uint16_t fg, uint16_t bg, uint8_t datum) {
  useFont(f);
  tft.setTextColor(fg, bg);    // fond connu : lissage calculé contre bg
  tft.setTextDatum(datum);
  return tft.drawString(s, x, y);
}

int textW(const String& s, FontId f) {
  useFont(f);
  return tft.textWidth(s);
}

static int32_t lire32(const uint8_t* p) {
  return (int32_t)(((uint32_t)pgm_read_byte(p) << 24) | ((uint32_t)pgm_read_byte(p + 1) << 16) |
                   ((uint32_t)pgm_read_byte(p + 2) << 8) | pgm_read_byte(p + 3));
}

bool glyphBox(FontId f, uint16_t code, GlyphBox& g) {
  const uint8_t* a = FONT_DATA[f];
  int n = lire32(a);
  for (int i = 0; i < n; i++) {
    const uint8_t* r = a + 24 + i * 28;
    if (lire32(r) != code) continue;
    g.h = lire32(r + 4); g.w = lire32(r + 8); g.adv = lire32(r + 12);
    g.dy = lire32(r + 16); g.dx = lire32(r + 20);
    return true;
  }
  return false;
}

int fontAscent(FontId f) {
  // Même règle que TFT_eSPI (maxAscent) : plus grand dY des glyphes imprimables
  const uint8_t* a = FONT_DATA[f];
  int n = lire32(a), asc = 0;
  for (int i = 0; i < n; i++) {
    const uint8_t* r = a + 24 + i * 28;
    int32_t u = lire32(r);
    if ((u > 0x20 && u < 0x7F) || u > 0xA0) asc = max<int>(asc, lire32(r + 16));
  }
  return asc;
}

// Raccourcit un texte avec « … » pour tenir dans maxW pixels, sans aucune
// réservation de mémoire : essais dans un tampon fixe sur la pile, longueur
// trouvée par dichotomie (la largeur croît avec le nombre de caractères).
String fitText(const String& s, FontId f, int maxW) {
  useFont(f);
  const char* src = s.c_str();
  if (tft.textWidth(src) <= maxW) return s;

  static const char ELLIPSE[] = "\xE2\x80\xA6";   // « … » en UTF-8
  char buf[72];
  int n = min<int>(s.length(), sizeof(buf) - sizeof(ELLIPSE));

  // Prépare dans buf le préfixe de longueur `cut` (sans espaces finaux) + « … ».
  auto essai = [&](int cut) -> int {
    while (cut > 0 && (src[cut] & 0xC0) == 0x80) cut--;   // début d'un caractère UTF-8
    int fin = cut;
    while (fin > 0 && src[fin - 1] == ' ') fin--;
    memcpy(buf, src, fin);
    memcpy(buf + fin, ELLIPSE, sizeof(ELLIPSE));          // copie aussi le \0 final
    return cut;
  };

  int bas = 0, haut = n, meilleur = -1;   // 0 caractère = « … » seul
  while (bas <= haut) {
    int milieu = (bas + haut) / 2;
    int cut = essai(milieu);
    if (tft.textWidth(buf) <= maxW) {
      meilleur = cut;
      bas = milieu + 1;
    } else {
      haut = milieu - 1;
    }
  }
  if (meilleur < 0) return "";
  essai(meilleur);
  return String(buf);
}

void card(int x, int y, int w, int h, int r, uint16_t color, uint16_t bg) {
  tft.fillSmoothRoundRect(x, y, w, h, r, color, bg);
}

void outlinedCard(int x, int y, int w, int h, int r, uint16_t border, uint16_t fill, uint16_t bg) {
  tft.fillSmoothRoundRect(x, y, w, h, r, border, bg);
  tft.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, fill, border);
}

void pill(int x, int y, const String& s, FontId f, uint16_t fg, uint16_t fill, uint16_t bg,
          bool alignRight) {
  int w = textW(s, f) + 14;
  int h = (f == F_11) ? 19 : 22;
  if (alignRight) x -= w;
  tft.fillSmoothRoundRect(x, y, w, h, h / 2 - 2, fill, bg);
  drawText(s, x + w / 2, y + h / 2 + 1, f, fg, fill, MC_DATUM);
}

void pageDots(uint8_t active, uint8_t count, uint16_t color) {
  const int y = 240 - MARGE_BAS - 12;   // points de 5 px, au-dessus de la zone masquée
  tft.fillRect(0, y - 3, 240, 11, Col::BG);
  if (count < 2) return;
  int total = (count - 1) * 10 + 14;
  int x = 120 - total / 2;
  for (uint8_t i = 0; i < count; i++) {
    int w = (i == active) ? 14 : 5;
    tft.fillSmoothRoundRect(x, y, w, 5, 2, i == active ? color : Col::DIM, Col::BG);
    x += w + 5;
  }
}

void staleMark(bool stale, uint16_t bg) {
  // Petit point ambre en haut à droite : donnée ancienne (source injoignable).
  tft.fillSmoothCircle(231, 8 + MARGE_HAUT, 3, stale ? Col::AMBRE : bg, bg);
}

// -----------------------------------------------------------------
// Icônes météo — dessinées dans un repère 76 x 60 (comme la maquette)
// -----------------------------------------------------------------

namespace {
struct Pen {
  float x0, y0, s;
  uint16_t bg;
  int X(float x) const { return (int)lroundf(x0 + x * s); }
  int Y(float y) const { return (int)lroundf(y0 + y * s); }
  int R(float r) const { return max(1, (int)lroundf(r * s)); }
};

void sun(const Pen& p, float x, float y, float r, bool halo = true) {
  uint16_t haloCol = tft.alphaBlend(56, Col::AMBRE, p.bg);
  if (halo) tft.fillSmoothCircle(p.X(x), p.Y(y), p.R(r * 1.43f), haloCol, p.bg);
  tft.fillSmoothCircle(p.X(x), p.Y(y), p.R(r), Col::AMBRE, halo ? haloCol : p.bg);
}

// Nuage : 3 cercles + un rectangle arrondi. (dx, dy) décale le nuage.
// knockUnder >= 0 : le nuage recouvre une autre forme de cette couleur ; on
// découpe d'abord sa silhouette élargie (détourage flat design) pour que le
// lissage du nuage se fasse contre le vrai fond.
void cloud(const Pen& p, float dx, float dy, uint16_t color, int32_t knockUnder = -1) {
  const float c[3][3] = { {26, 40, 12}, {40, 33, 14}, {53, 41, 10} };
  int rx = p.X(14 + dx), ry = p.Y(38 + dy), rw = p.R(49), rh = p.R(14), rr = p.R(7);
  if (knockUnder >= 0) {
    // Découpe sans lissage : invisible sur le fond, nette sur la forme recouverte.
    int k = max(1, (int)lroundf(2.0f * p.s));
    for (auto& q : c) tft.fillCircle(p.X(q[0] + dx), p.Y(q[1] + dy), p.R(q[2]) + k, p.bg);
    tft.fillRect(rx + rr, ry - k, rw - 2 * rr, rh + 2 * k, p.bg);
    tft.fillCircle(rx + rr, ry + rh / 2, rh / 2 + k, p.bg);
    tft.fillCircle(rx + rw - rr, ry + rh / 2, rh / 2 + k, p.bg);
  }
  // 1) contours lissés
  for (auto& q : c) tft.fillSmoothCircle(p.X(q[0] + dx), p.Y(q[1] + dy), p.R(q[2]), color, p.bg);
  tft.fillSmoothRoundRect(rx, ry, rw, rh, rr, color, p.bg);
  // 2) intérieur plein (efface les liserés entre formes qui se chevauchent)
  for (auto& q : c) tft.fillCircle(p.X(q[0] + dx), p.Y(q[1] + dy), p.R(q[2]) - 1, color);
  tft.fillRect(rx + rr, ry + 1, rw - 2 * rr, rh - 2, color);
}

void moon(const Pen& p, float x, float y, float r, uint16_t under) {
  tft.fillSmoothCircle(p.X(x), p.Y(y), p.R(r), Col::LUNE, under);
  tft.fillSmoothCircle(p.X(x + r * 0.55f), p.Y(y - r * 0.4f), p.R(r * 0.85f), under, Col::LUNE);
}
}  // namespace

// Empreinte réelle de chaque icône dans son repère 76 x 60 (gauche, haut,
// droite, bas), mesurée dans le simulateur : le soleil est rond et compact,
// le nuage large, la pluie haute... Sert à ajuster et centrer chaque icône.
static const float EMPREINTE[10][4] = {
  { 16.7f, 8.7f, 59.7f, 51.7f },   // 0 soleil
  { 14.0f, 2.0f, 66.3f, 52.3f },   // 1 éclaircies
  { 10.0f, 10.0f, 73.3f, 52.3f },  // 2 nuageux
  { 13.7f, 7.0f, 63.3f, 57.7f },   // 3 brouillard
  { 14.0f, 7.0f, 63.3f, 58.0f },   // 4 pluie
  { 14.0f, 7.0f, 63.3f, 57.7f },   // 5 neige
  { 14.0f, 7.0f, 63.3f, 62.3f },   // 6 orage
  { 22.0f, 10.0f, 60.7f, 46.3f },  // 7 nuit claire
  { 14.0f, 4.7f, 66.3f, 52.3f },   // 8 nuit peu nuageuse
  { 14.0f, 15.0f, 63.3f, 48.3f },  // autre code : nuage
};

static void dessinerIcone(uint8_t code, float x0, float y0, float scale, uint16_t bg);

void weatherIcon(uint8_t code, int cx, int cy, float scale, uint16_t bg) {
  dessinerIcone(code, cx - 38 * scale, cy - 30 * scale, scale, bg);
}

float weatherIconScale(uint8_t code, int w, int h, int* encreW, int* encreH) {
  const float* e = EMPREINTE[code < 9 ? code : 9];
  float s = min((float)w / (e[2] - e[0]), (float)h / (e[3] - e[1]));
  if (encreW) *encreW = (int)lroundf(s * (e[2] - e[0]));
  if (encreH) *encreH = (int)lroundf(s * (e[3] - e[1]));
  return s;
}

void weatherIconFit(uint8_t code, int x, int y, int w, int h, uint16_t bg, float scaleMax) {
  const float* e = EMPREINTE[code < 9 ? code : 9];
  float s = min((float)w / (e[2] - e[0]), (float)h / (e[3] - e[1]));
  if (scaleMax > 0) s = min(s, scaleMax);
  // L'empreinte (et non le repère 76 x 60) est centrée dans la zone
  float x0 = x + w / 2.0f - s * (e[0] + e[2]) / 2;
  float y0 = y + h / 2.0f - s * (e[1] + e[3]) / 2;
  dessinerIcone(code, x0, y0, s, bg);
}

static void dessinerIcone(uint8_t code, float x0, float y0, float scale, uint16_t bg) {
  Pen p{ x0, y0, scale, bg };
  float lw = max(1.6f, 3.2f * scale);
  switch (code) {
    case 0:  // soleil
      sun(p, 38, 30, 15);
      break;
    case 1:  // éclaircies
      sun(p, 46, 22, 14);
      cloud(p, 0, 0, Col::NUAGE, Col::AMBRE);
      break;
    case 2:  // nuageux
      cloud(p, 10, -9, Col::NUAGE3);
      cloud(p, -4, 0, Col::NUAGE2, Col::NUAGE3);
      break;
    case 3:  // brouillard
      cloud(p, 0, -12, Col::NUAGE2);
      tft.drawWideLine(p.X(16), p.Y(46), p.X(60), p.Y(46), lw * 1.4f, Col::NUAGE3, bg);
      tft.drawWideLine(p.X(24), p.Y(55), p.X(54), p.Y(55), lw * 1.4f, Col::NUAGE3, bg);
      break;
    case 4:  // pluie
      cloud(p, 0, -12, Col::NUAGE2);
      for (int i = 0; i < 3; i++) {
        float x = 26 + i * 12;
        tft.drawWideLine(p.X(x), p.Y(46), p.X(x - 3), p.Y(56), lw, Col::PLUIE, bg);
      }
      break;
    case 5:  // neige
      cloud(p, 0, -12, Col::NUAGE2);
      for (int i = 0; i < 3; i++) {
        tft.fillSmoothCircle(p.X(26 + i * 12), p.Y(i == 1 ? 54 : 48), p.R(3.2f), Col::TXT, bg);
      }
      break;
    case 6:  // orage
      cloud(p, 0, -12, Col::NUAGE3);
      tft.fillTriangle(p.X(43), p.Y(36), p.X(30), p.Y(50), p.X(40), p.Y(50), Col::AMBRE);
      tft.fillTriangle(p.X(36), p.Y(48), p.X(46), p.Y(48), p.X(32), p.Y(62), Col::AMBRE);
      break;
    case 7:  // nuit claire
      moon(p, 38, 30, 16, bg);
      break;
    case 8:  // nuit peu nuageuse
      moon(p, 48, 21, 13, bg);
      cloud(p, 0, 0, Col::NUAGE, Col::LUNE);
      break;
    default:
      cloud(p, 0, -4, Col::NUAGE2);
      break;
  }
}

void halfSun(int cx, int cy, uint16_t color, uint16_t bg) {
  tft.fillSmoothCircle(cx, cy + 3, 7, color, bg);
  tft.fillRect(cx - 9, cy + 4, 19, 9, bg);
  tft.drawWideLine(cx - 8, cy + 6, cx + 8, cy + 6, 2.4f, color, bg);
}

void headsetIcon(int cx, int cy, uint16_t color, uint16_t bg) {
  // Arc supérieur (0° = bas, sens horaire : 90..270 = moitié haute)
  tft.drawSmoothArc(cx, cy + 1, 8, 6, 90, 270, color, bg, true);
  tft.fillSmoothRoundRect(cx - 9, cy, 5, 9, 2, color, bg);
  tft.fillSmoothRoundRect(cx + 4, cy, 5, 9, 2, color, bg);
}

void warningIcon(int cx, int cy, uint16_t color, uint16_t bg) {
  int tx = cx, ty = cy - 21, lx = cx - 25, ly = cy + 20, rx = cx + 25, ry = cy + 20;
  // Bords épais lissés (angles arrondis) PUIS remplissage par-dessus
  tft.drawWideLine(tx, ty, lx, ly, 6, color, bg);
  tft.drawWideLine(lx, ly, rx, ry, 6, color, bg);
  tft.drawWideLine(rx, ry, tx, ty, 6, color, bg);
  tft.fillTriangle(tx, ty, lx, ly, rx, ry, color);
  tft.fillSmoothRoundRect(cx - 3, cy - 9, 6, 17, 3, bg, color);
  tft.fillSmoothCircle(cx, cy + 13, 3, bg, color);
}
