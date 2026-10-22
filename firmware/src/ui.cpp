#include "ui.h"
#include "fonts/inter_fonts.h"

TFT_eSPI tft;

static const uint8_t* const FONT_DATA[] = { F11, F13, F16, F18, F32, F52, F68 };
static int8_t currentFont = -1;

void uiBegin() {
  tft.init();
  tft.setRotation(2);          // même orientation que le firmware d'origine
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

static void dropLastUtf8(String& s) {
  int n = s.length();
  while (n > 0 && (s[n - 1] & 0xC0) == 0x80) n--;   // octets de continuation
  if (n > 0) n--;
  s.remove(n);
}

String fitText(const String& s, FontId f, int maxW) {
  if (textW(s, f) <= maxW) return s;
  String t = s;
  while (t.length() > 0) {
    dropLastUtf8(t);
    t.trim();
    if (textW(t + "…", f) <= maxW) return t + "…";
  }
  return "";
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
  tft.fillRect(0, 225, 240, 12, Col::BG);
  if (count < 2) return;
  int total = (count - 1) * 10 + 14;
  int x = 120 - total / 2;
  for (uint8_t i = 0; i < count; i++) {
    int w = (i == active) ? 14 : 5;
    tft.fillSmoothRoundRect(x, 228, w, 5, 2, i == active ? color : Col::DIM, Col::BG);
    x += w + 5;
  }
}

void staleMark(bool stale, uint16_t bg) {
  // Petit point ambre en haut à droite : donnée ancienne (source injoignable).
  tft.fillSmoothCircle(231, 8, 3, stale ? Col::AMBRE : bg, bg);
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

void weatherIcon(uint8_t code, int cx, int cy, float scale, uint16_t bg) {
  Pen p{ cx - 38 * scale, cy - 30 * scale, scale, bg };
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
