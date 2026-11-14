// Personnages (3.1.0) : voir persos.h. Dessins originaux, uniquement avec les
// primitives lissées de TFT_eSPI (cercles, rectangles arrondis, traits larges,
// triangles) : aucune image.
#include "persos.h"

#include <math.h>

#include "app.h"
#include "ui.h"

using namespace Col;

WxState gWx;

// ------------------------------------------------------------------ états

uint8_t nimbusEtat(uint8_t icone) {
  // Icônes de la passerelle (meteo.py) : 0 soleil, 1 éclaircies, 2 nuageux,
  // 3 brouillard, 4 pluie, 5 neige, 6 orage, 7 lune, 8 lune et nuages
  switch (icone) {
    case 2: case 3: return NIMBUS_COUVERT;
    case 4:         return NIMBUS_PLUIE;
    case 5:         return NIMBUS_NEIGE;
    case 6:         return NIMBUS_ORAGE;
    default:        return NIMBUS_BEAU;   // 0, 1, 7, 8 (et code inconnu)
  }
}

static uint8_t bandePousse(int t) {
  if (t <= 5) return POUSSE_GLACIAL;
  if (t <= 17) return POUSSE_FRAIS;
  if (t <= 33) return POUSSE_AGREABLE;
  return POUSSE_CANICULE;
}

// Seuils d'entrée : 5 °C et moins, 6 à 17, 18 à 33, 34 et plus (dès le seuil).
// Pour revenir vers un état plus tempéré, il faut 1 °C de plus : canicule
// gardée à 33, glacial gardé à 6, agréable gardé à 17 (pas de va-et-vient).
uint8_t pousseEtat(int t, uint8_t precedent) {
  if (precedent == POUSSE_CANICULE && t == 33) return precedent;
  if (precedent == POUSSE_GLACIAL && t == 6) return precedent;
  if (precedent == POUSSE_AGREABLE && t == 17) return precedent;
  return bandePousse(t);
}

uint8_t voltaEtat(uint8_t pct) {
  if (pct < 20) return VOLTA_ROUGE;
  if (pct < 35) return VOLTA_ORANGE;   // 3.5.0 : 35 % (40 avant)
  if (pct <= 80) return VOLTA_VERT;
  return VOLTA_CONTENTE;
}

// ------------------------------------------------- repère et primitives
// Personnage dessiné en coordonnées « nominales » (centre x = 120, pieds à
// y = 210), à l'échelle S, la ligne nominale y = 210 posée à y = OY. Chaque
// personnage choisit S et OY pour occuper l'écran au-dessus du texte (y < 210).

static float S = 0.88f, OY = 196;
static constexpr float OX = 120;
static void repere(float s, float oy) { S = s; OY = oy; }
static constexpr float RAD = 3.14159265f / 180.f;
static float X(float x) { return OX + (x - 120) * S; }
static float Y(float y) { return OY + (y - 210) * S; }
static float D(float v) { return v * S; }
static void C(float x, float y, float r, uint16_t c, uint16_t bg) { tft.fillSmoothCircle(X(x), Y(y), fmaxf(1, D(r)), c, bg); }
static void RR(float x, float y, float w, float h, float r, uint16_t c, uint16_t bg) {
  tft.fillSmoothRoundRect(X(x), Y(y), D(w), D(h), D(r), c, bg);
}
static void L(float ax, float ay, float bx, float by, float w, uint16_t c, uint16_t bg) {
  tft.drawWideLine(X(ax), Y(ay), X(bx), Y(by), fmaxf(1.5f, D(w)), c, bg);
}
static void T(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c) {
  tft.fillTriangle(X(ax), Y(ay), X(bx), Y(by), X(cx), Y(cy), c);
}
// Arc en traits ; angles en degrés, 0 = haut, sens horaire
static void A(float cx, float cy, float r, float a0, float a1, float w, uint16_t c, uint16_t bg) {
  const int n = (int)(fabsf(a1 - a0) / 10) + 3;
  float px = cx + r * sinf(a0 * RAD), py = cy - r * cosf(a0 * RAD);
  for (int k = 1; k <= n; k++) {
    const float a = a0 + (a1 - a0) * k / n, x = cx + r * sinf(a * RAD), y = cy - r * cosf(a * RAD);
    L(px, py, x, y, w, c, bg);
    px = x;
    py = y;
  }
}

// ------------------------------------------------------- traits du visage

static constexpr uint16_t NOIR = RGB(0x22, 0x1E, 0x2A), BLANC = RGB(0xFF, 0xFF, 0xFF);
static constexpr uint16_t ROSE = RGB(0xFF, 0x70, 0x80), BOUCHE = RGB(0x5A, 0x20, 0x28);
static constexpr uint16_t BLEU_GOUTTE = RGB(0x7C, 0xC4, 0xFF), JAUNE = RGB(0xFF, 0xE0, 0x40);

static void oeil(float x, float y, float w, float h, uint16_t c, uint16_t fd) {
  RR(x - w / 2, y - h / 2, w, h, w / 2, c, fd);
  C(x + w / 5, y - h / 4, w / 4 + 1, c == NOIR ? BLANC : RGB(0xE0, 0xFF, 0xFF), c);
}
static void oeilFerme(float x, float y, uint16_t c, uint16_t fd) { A(x, y - 5, 10, 105, 255, 3.5f, c, fd); }
static void oeilJoie(float x, float y, uint16_t c, uint16_t fd) { A(x, y + 5, 9, 285, 435, 3.5f, c, fd); }
static void oeilLourd(float x, float y, uint16_t c, uint16_t fd) {
  L(x - 8, y, x + 8, y, 3.5f, c, fd);
  A(x, y - 1, 7, 100, 260, 3, c, fd);
}
static void oeilPlisse(float x, float y, int s, uint16_t c, uint16_t fd) {   // « > » ou « < »
  L(x - 7 * s, y - 6, x + 6 * s, y, 3.5f, c, fd);
  L(x + 6 * s, y, x - 7 * s, y + 6, 3.5f, c, fd);
}
static void sourcilsInquiets(float xg, float xd, float y, uint16_t c, uint16_t fd) {
  L(xg - 10, y + 2, xg + 7, y - 6, 3.5f, c, fd);
  L(xd + 10, y + 2, xd - 7, y - 6, 3.5f, c, fd);
}
static void sourcilsFaches(float xg, float xd, float y, uint16_t c, uint16_t fd) {
  L(xg - 10, y - 6, xg + 8, y + 2, 4.5f, c, fd);
  L(xd + 10, y - 6, xd - 8, y + 2, 4.5f, c, fd);
}
static void joues(float xg, float xd, float y, uint16_t c, uint16_t fd, float r = 8) {
  C(xg, y, r, c, fd);
  C(xd, y, r, c, fd);
}
static void sourire(float x, float y, uint16_t c, uint16_t fd) { A(x, y - 6, 11, 115, 245, 3.5f, c, fd); }
static void moue(float x, float y, uint16_t c, uint16_t fd) { A(x, y + 14, 10, 300, 420, 3.5f, c, fd); }
static void bouchePlate(float x, float y, uint16_t c, uint16_t fd) { L(x - 7, y, x + 7, y + 1, 3.5f, c, fd); }
static void boucheOuverte(float x, float y, uint16_t c, uint16_t fd) {
  const uint16_t in = c == NOIR ? BOUCHE : c;
  C(x, y - 2, 12, in, fd);
  tft.fillRect(X(x - 13), Y(y - 15), D(27), D(13), fd);   // moitié haute effacée : « D » couché
  C(x, y + 5, 5, ROSE, in);
}
static void dents(float x, float y, uint16_t c, uint16_t fd) {   // dents qui claquent : zigzag
  float px = x - 12, py = y + 4;
  for (int k = 1; k <= 6; k++) {
    const float xx = x - 12 + k * 4, yy = y + ((k % 2) ? 0 : 6);
    L(px, py, xx, yy, 2.5f, c, fd);
    px = xx;
    py = yy;
  }
}
static void langue(float x, float y, uint16_t fd) {
  RR(x - 10, y - 2, 20, 14, 7, BOUCHE, fd);
  C(x + 2, y + 11, 6, ROSE, fd);
}
static void goutte(float x, float y) {
  T(x, y - 14, x - 6, y - 2, x + 6, y - 2, BLEU_GOUTTE);
  C(x, y, 7, BLEU_GOUTTE, BG);
}

// ------------------------------------------------ décor (coordonnées écran)

static void flocon(float x, float y, float r, uint16_t c) {
  for (int i = 0; i < 3; i++) {
    const float a = i * 60 * RAD;
    tft.drawWideLine(x - r * sinf(a), y - r * cosf(a), x + r * sinf(a), y + r * cosf(a), 2, c, BG);
  }
}
static void eclair(float x, float y, float s, uint16_t c) {
  tft.fillTriangle(x + 4 * s, y, x - 6 * s, y + 14 * s, x + 2 * s, y + 14 * s, c);
  tft.fillTriangle(x - 1 * s, y + 11 * s, x + 7 * s, y + 11 * s, x - 5 * s, y + 28 * s, c);
}
static void etincelle(float x, float y, float r) {
  tft.fillTriangle(x - r, y, x + r, y, x, y - r * 2, JAUNE);
  tft.fillTriangle(x - r, y, x + r, y, x, y + r * 2, JAUNE);
  tft.fillTriangle(x, y - r, x, y + r, x - r * 2, y, JAUNE);
  tft.fillTriangle(x, y - r, x, y + r, x + r * 2, y, JAUNE);
}
// Soleil et ondes de chaleur : coordonnées nominales (suivent l'échelle)
static void soleil(float cx, float cy) {
  const uint16_t j = RGB(0xFF, 0xC8, 0x30);
  for (int i = 0; i < 8; i++) {
    const float a = i * 45 * RAD;
    L(cx + 15 * sinf(a), cy - 15 * cosf(a), cx + 20 * sinf(a), cy - 20 * cosf(a), 3, j, BG);
  }
  C(cx, cy, 10, j, BG);
}
static void ondesChaleur(float x0, float y0) {
  const uint16_t o = RGB(0xFF, 0x8A, 0x3C);
  for (int k = 0; k < 3; k++) {
    float px = x0 + k * 16, py = y0;
    for (int j = 1; j <= 8; j++) {
      const float yy = y0 - j * 3, xx = x0 + k * 16 + 3.5f * sinf(j * 0.9f);
      L(px, py, xx, yy, 2.5f, o, BG);
      px = xx;
      py = yy;
    }
  }
}

// Ligne en bas de l'écran, dans la plus grande police où elle tient
static void ligneBas(const String& t, uint16_t c) {
  const FontId POLICES[3] = { F_18, F_16, F_13 };
  FontId f = F_13;
  for (FontId p : POLICES) if (textW(t, p) <= 232) { f = p; break; }
  drawText(t, 120, 212, f, c, BG, TC_DATUM);
}

// ------------------------------------------------------------- Nimbus

void drawNimbus() {
  const uint8_t e = nimbusEtat(gWx.i);
  tft.fillScreen(BG);
  uint16_t c, om;
  switch (e) {
    case NIMBUS_BEAU:    c = RGB(0xF4, 0xF8, 0xFF); om = RGB(0xB8, 0xC8, 0xE8); break;
    case NIMBUS_COUVERT: c = RGB(0xCC, 0xD2, 0xDE); om = RGB(0x98, 0xA2, 0xB4); break;
    case NIMBUS_ORAGE:   c = RGB(0x5E, 0x66, 0x7A); om = RGB(0x3C, 0x42, 0x52); break;
    default:             c = RGB(0x9C, 0xA6, 0xBA); om = RGB(0x6E, 0x78, 0x8C); break;   // pluie, neige
  }
  repere(1.32f, 203);                    // nuage : 20 à 220 px de large
  if (e == NIMBUS_BEAU) soleil(178, 80);  // derrière le nuage, en haut à droite
  if (e == NIMBUS_COUVERT)   // traînées de brume
    for (int k = 0; k < 3; k++) L(32 + k * 10, 150 + k * 14, 62 + k * 10, 150 + k * 14, 3, RGB(0x50, 0x56, 0x64), BG);
  if (e == NIMBUS_PLUIE || e == NIMBUS_ORAGE)
    for (int k = 0; k < 7; k++) {
      const float x = 62 + k * 20, y = 176 + (k % 2) * 12;
      L(x, y, x - 4, y + 14, 3.5f, RGB(0x5A, 0x9C, 0xE8), BG);
    }
  if (e == NIMBUS_NEIGE)
    for (int k = 0; k < 6; k++) flocon(X(66 + k * 22), Y(182 + (k % 2) * 14), D(6), RGB(0xE8, 0xF4, 0xFF));
  // Corps : boules d'ombre, puis boules claires (bord lissé sur le clair : pas de liseré)
  const uint8_t B[5][3] = { { 78, 136, 34 }, { 162, 136, 34 }, { 104, 104, 38 }, { 144, 100, 36 }, { 120, 140, 48 } };
  for (auto& p : B) C(p[0], p[1] + 4, p[2], om, BG);
  for (auto& p : B) C(p[0] - 2, p[1], p[2] - 2, c, c);
  RR(46, 136, 148, 40, 20, c, c);
  if (e == NIMBUS_BEAU) C(96, 82, 8, BLANC, c);

  const float xg = 100, xd = 140, ey = 130, my = 154;
  switch (e) {
    case NIMBUS_BEAU:
      oeil(xg, ey, 17, 26, NOIR, c);
      oeil(xd, ey, 17, 26, NOIR, c);
      joues(82, 158, 150, tft.alphaBlend(110, RGB(0xFF, 0x70, 0x90), c), c, 9);
      sourire(120, my, NOIR, c);
      break;
    case NIMBUS_COUVERT:   // boude, regard en coin
      oeilLourd(xg, ey, NOIR, c);
      oeilLourd(xd, ey, NOIR, c);
      C(xg - 4, ey + 3, 3, NOIR, c);
      C(xd - 4, ey + 3, 3, NOIR, c);
      bouchePlate(116, my + 2, NOIR, c);
      break;
    case NIMBUS_PLUIE:
    case NIMBUS_NEIGE:
      oeilFerme(xg, ey, NOIR, c);
      oeilFerme(xd, ey, NOIR, c);
      sourcilsInquiets(xg, xd, ey - 16, NOIR, c);
      moue(120, my, NOIR, c);
      if (e == NIMBUS_PLUIE) {   // larme
        T(xd + 4, ey + 2, xd, ey + 12, xd + 8, ey + 12, BLEU_GOUTTE);
        C(xd + 4, ey + 14, 4, BLEU_GOUTTE, c);
      }
      break;
    default:   // orage
      oeil(xg, ey + 2, 13, 16, NOIR, c);
      oeil(xd, ey + 2, 13, 16, NOIR, c);
      sourcilsFaches(xg, xd, ey - 12, NOIR, c);
      dents(120, my, NOIR, c);
      eclair(X(82), Y(170), D(1.1f), RGB(0xFF, 0xD8, 0x30));   // sous le nuage, dans la pluie
      eclair(X(152), Y(168), D(1.0f), RGB(0xFF, 0xD8, 0x30));
      break;
  }
  // Libellé de la passerelle ; ciel sans nuage (soleil, lune) : « Beau temps »
  ligneBas(gWx.i == 0 || gWx.i == 7 ? String(F("Beau temps")) : String(gWx.l), RGB(0xC8, 0xD0, 0xDC));
}

// ------------------------------------------------------------- Pousse

static uint8_t pousseDernier = POUSSE_AUCUN;

void drawPousse() {
  const uint8_t e = pousseEtat(gWx.t, pousseDernier);
  pousseDernier = e;
  tft.fillScreen(BG);
  repere(1.13f, 208);                    // de la grande plante (haut) aux pieds
  const uint16_t corps = RGB(0x8E, 0xE0, 0xB8), om = RGB(0x5E, 0xB8, 0x90), cl = RGB(0xC8, 0xF6, 0xDC);
  const uint16_t fe = RGB(0x3C, 0xA8, 0x5C), fe2 = RGB(0x58, 0xC4, 0x70), givre = RGB(0xA8, 0xD8, 0xFF);
  if (e == POUSSE_GLACIAL) {
    const uint8_t G[7][2] = { { 22, 34 }, { 40, 84 }, { 20, 186 }, { 214, 70 }, { 222, 184 }, { 70, 24 }, { 180, 30 } };
    for (auto& p : G) flocon(p[0], p[1], 5, RGB(0xDC, 0xEE, 0xFF));
  }
  RR(90, 190, 26, 18, 9, om, BG);   // pieds
  RR(124, 190, 26, 18, 9, om, BG);
  // La plante évolue avec la température : 1 feuille, 2 feuilles, fleur, grande plante
  if (e == POUSSE_GLACIAL) {
    L(120, 78, 121, 66, 4, fe, BG);
    C(127, 64, 7, fe2, BG);
  } else if (e == POUSSE_FRAIS) {
    L(120, 78, 123, 58, 4, fe, BG);
    C(111, 58, 9, fe, BG);
    C(134, 54, 10, fe2, BG);
  } else {
    const bool fanee = e == POUSSE_CANICULE;
    L(120, 80, 122, fanee ? 34 : 46, 5, fe, BG);
    for (int k = 0; k < (fanee ? 6 : 4); k++) {
      const float y = 72 - k * 8, s = (k % 2) ? 1 : -1, dy = fanee ? 8 : -2;
      L(121, y, 121 + s * 18, y + dy, 3, fe, BG);
      C(121 + s * 22, y + dy + (fanee ? 4 : 0), 9, (k % 2) ? fe2 : fe, BG);
    }
    if (!fanee) {   // fleur
      const uint16_t f = RGB(0xFF, 0x9A, 0xC0);
      for (int i = 0; i < 5; i++) {
        const float a = i * 72 * RAD;
        C(122 + 8 * sinf(a), 44 - 8 * cosf(a), 6, f, BG);
      }
      C(122, 44, 5, RGB(0xFF, 0xD8, 0x40), f);
    }
  }
  C(120, 140, 64, om, BG);
  C(116, 135, 59, corps, om);
  C(88, 104, 9, cl, corps);
  C(76, 120, 4, cl, corps);

  const float xg = 98, xd = 140, ey = 136, my = 158;
  uint16_t ct = RGB(0xC8, 0xD0, 0xDC);
  switch (e) {
    case POUSSE_GLACIAL:
      oeilPlisse(xg, ey, 1, NOIR, corps);
      oeilPlisse(xd, ey, -1, NOIR, corps);
      joues(82, 156, 156, givre, corps);
      dents(119, my, NOIR, corps);
      C(106, 76, 9, RGB(0xF4, 0xFA, 0xFF), BG);   // neige sur la tête
      C(122, 72, 11, RGB(0xF4, 0xFA, 0xFF), BG);
      C(136, 77, 8, RGB(0xF4, 0xFA, 0xFF), BG);
      for (int k = 0; k < 2; k++) {               // frissons
        A(46 - k * 7, 140, 12 + k * 4, 220, 320, 2.5f, givre, BG);
        A(194 + k * 7, 140, 12 + k * 4, 40, 140, 2.5f, givre, BG);
      }
      ct = givre;
      break;
    case POUSSE_FRAIS:
      oeil(xg, ey, 14, 22, NOIR, corps);
      oeil(xd, ey, 14, 22, NOIR, corps);
      joues(82, 156, 156, tft.alphaBlend(90, RGB(0xFF, 0x70, 0x90), corps), corps);
      sourire(119, my - 2, NOIR, corps);
      break;
    case POUSSE_AGREABLE:
      oeilJoie(xg, ey, NOIR, corps);
      oeilJoie(xd, ey, NOIR, corps);
      joues(82, 156, 156, tft.alphaBlend(130, RGB(0xFF, 0x70, 0x90), corps), corps, 10);
      boucheOuverte(119, my, NOIR, corps);
      ct = corps;
      break;
    default:   // canicule
      oeilLourd(xg, ey, NOIR, corps);
      oeilLourd(xd, ey, NOIR, corps);
      joues(82, 156, 156, RGB(0xFF, 0x4A, 0x4A), corps, 10);
      langue(119, my, corps);
      goutte(60, 116);
      goutte(182, 100);
      ondesChaleur(30, 72);              // en haut à gauche, hors du corps
      ct = RGB(0xFF, 0x8A, 0x3C);
      break;
  }
  String t = String((int)gWx.t) + F("° · ");
  switch (e) {
    case POUSSE_GLACIAL:  t += F("glacial"); break;
    case POUSSE_FRAIS:    t += F("frais"); break;
    case POUSSE_AGREABLE: t += F("agréable"); break;
    default:              t += F("canicule"); break;
  }
  ligneBas(t, ct);
}

// -------------------------------------------------------------- Volta

void drawVolta() {
  const uint8_t e = voltaEtat(gTesla.pct);
  const bool charge = gTesla.etat == TESLA_CHARGE;
  tft.fillScreen(BG);
  repere(1.16f, 205);                    // de la borne aux pieds, bras levés compris
  const uint16_t JAUGE[4] = { RGB(0xF0, 0x44, 0x34), RGB(0xFF, 0x9C, 0x2C), RGB(0x4C, 0xD0, 0x64), RGB(0x3C, 0xE0, 0x70) };
  const uint16_t jauge = JAUGE[e];
  const uint16_t boit = RGB(0xF2, 0xF0, 0xEA), om = RGB(0xB8, 0xB4, 0xAA), vitre = RGB(0x1E, 0x26, 0x32);
  const uint16_t borne = RGB(0xD4, 0xA0, 0x40);
  if (charge) {   // câble branché
    const uint16_t cb = RGB(0x70, 0x78, 0x88);
    float px = X(150), py = Y(190);
    for (int j = 1; j <= 6; j++) {
      const float x = X(150 + j * 7), y = Y(196 + 8 * sinf(j * 0.8f));
      tft.drawWideLine(px, py, x, y, D(5), cb, BG);
      px = x;
      py = y;
    }
    RR(190, 190, 16, 12, 4, cb, BG);
  }
  RR(98, 196, 18, 12, 6, om, BG);   // pieds
  RR(124, 196, 18, 12, 6, om, BG);
  if (charge || e == VOLTA_CONTENTE) {   // bras levés
    L(76, 120, 50, 84, 6, om, BG); L(164, 120, 190, 84, 6, om, BG);
    C(48, 80, 8, boit, BG); C(192, 80, 8, boit, BG);
  } else if (e == VOLTA_ROUGE) {         // bras ballants
    L(76, 130, 60, 168, 6, om, BG); L(164, 130, 180, 168, 6, om, BG);
    C(58, 172, 7, boit, BG); C(182, 172, 7, boit, BG);
  } else {
    L(76, 128, 54, 150, 6, om, BG); L(164, 128, 186, 150, 6, om, BG);
    C(52, 152, 8, boit, BG); C(188, 152, 8, boit, BG);
  }
  RR(104, 38, 32, 16, 6, borne, BG);    // borne +
  RR(70, 48, 100, 152, 30, om, BG);
  RR(73, 48, 94, 148, 28, boit, om);
  RR(84, 132, 72, 58, 14, vitre, boit);  // jauge : le ventre
  const float hj = 50.f * min<uint8_t>(gTesla.pct, 100) / 100.f;
  if (hj >= 2) RR(88, 186 - hj, 64, hj, min(10.f, hj / 2), jauge, vitre);
  if (charge) eclair(X(120) - D(2), Y(140), D(1.5f), JAUNE);
  C(92, 62, 6, BLANC, boit);

  const float xg = 102, xd = 138, ey = 92, my = 114;
  const uint16_t rose = tft.alphaBlend(120, RGB(0xFF, 0x70, 0x90), boit);
  if (charge || e == VOLTA_CONTENTE) {
    oeilJoie(xg, ey, NOIR, boit); oeilJoie(xd, ey, NOIR, boit);
    joues(88, 152, 108, rose, boit);
    boucheOuverte(120, my, NOIR, boit);
    if (charge) { etincelle(X(40), Y(60), 4); etincelle(X(200), Y(46), 3); etincelle(X(46), Y(120), 3); }
  } else if (e == VOLTA_ROUGE) {
    oeilLourd(xg, ey, NOIR, boit); oeilLourd(xd, ey, NOIR, boit);
    moue(120, my, NOIR, boit);
    goutte(170, 70);
  } else if (e == VOLTA_ORANGE) {
    oeil(xg, ey, 13, 20, NOIR, boit); oeil(xd, ey, 13, 20, NOIR, boit);
    bouchePlate(120, my + 2, NOIR, boit);
  } else {
    oeil(xg, ey, 14, 22, NOIR, boit); oeil(xd, ey, 14, 22, NOIR, boit);
    joues(88, 152, 108, rose, boit);
    sourire(120, my, NOIR, boit);
  }
  String t = String((int)gTesla.pct) + F(" %");
  if (gTesla.ligne[0]) {   // « Autonomie 222 km » -> « 222 km »
    const char* l = gTesla.ligne;
    if (strncmp(l, "Autonomie ", 10) == 0) l += 10;
    t += F(" · ");
    t += l;
  }
  ligneBas(t, jauge);
}

// ---------------------------------------------------------------- Bip

void drawBip() {   // affiché seulement pendant une panne (bloc PANNE, Supervision, Bip)
  tft.fillScreen(BG);
  repere(0.88f, 196);
  // Corps gris neutre ; écran noir, yeux et bouche rouges (ROUGE des pannes)
  const uint16_t mt = RGB(0x9A, 0x9C, 0xA0), om = RGB(0x66, 0x68, 0x6C);
  const uint16_t ec = RGB(0x12, 0x12, 0x14), yeux = ROUGE, rouge = ROUGE;
  for (int k = 0; k < 3; k++) {   // gyrophare
    A(120, 34, 18 + k * 9, 300, 345, 3, ROUGE, BG);
    A(120, 34, 18 + k * 9, 15, 60, 3, ROUGE, BG);
  }
  L(120, 54, 120, 38, 4, om, BG);
  C(120, 32, 9, rouge, BG);
  RR(90, 166, 60, 40, 14, om, BG);     // corps
  RR(94, 166, 52, 36, 12, mt, om);
  C(120, 182, 7, rouge, mt);
  RR(92, 200, 20, 10, 5, om, BG);      // pieds
  RR(128, 200, 20, 10, 5, om, BG);
  RR(48, 98, 16, 36, 8, om, BG);       // oreilles
  RR(176, 98, 16, 36, 8, om, BG);
  RR(56, 52, 128, 116, 34, om, BG);    // tête
  RR(58, 52, 122, 110, 32, mt, om);
  RR(72, 70, 94, 78, 22, ec, mt);      // écran
  RR(80, 76, 22, 6, 3, RGB(0x2A, 0x2A, 0x2E), ec);
  const float xg = 102, xd = 136, ey = 104, my = 128;
  C(xg, ey, 11, yeux, ec); C(xd, ey, 11, yeux, ec);   // yeux écarquillés
  C(xg, ey, 5, ec, yeux);  C(xd, ey, 5, ec, yeux);
  sourcilsInquiets(xg, xd, ey - 18, yeux, ec);
  C(119, my + 4, 7, yeux, ec);                         // bouche « O »
  C(119, my + 4, 4, ec, yeux);
  goutte(182, 84);
  const uint8_t n = gSup.pa;
  String t;
  if (n > 1) t = String(n) + F(" services en panne");
  else if (n == 1) t = F("1 service en panne");
  else t = F("Service en panne");
  ligneBas(t, ROUGE);   // même rouge que les services en panne (Supervision)
}
