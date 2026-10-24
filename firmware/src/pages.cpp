#include "pages.h"
#include "app.h"
#include "config.h"
#include "net.h"
#include "ui.h"

#include <ArduinoJson.h>

using namespace Col;

// ---------------------------------------------------------------- outils

static void errorPage(const char* title, const String& detail) {
  drawText(title, 14, 15, F_16, TXT, BG);
  card(14, 82, 212, 74, 12, CARD);
  drawText("Données indisponibles", 120, 100, F_13, AMBRE, CARD, TC_DATUM);
  drawText(fitText(detail, F_11, 190), 120, 124, F_11, MUTED, CARD, TC_DATUM);
}

static String firstChar(const char* s) {
  if (!s[0]) return "?";
  int n = 1;
  while (s[n] && (s[n] & 0xC0) == 0x80) n++;
  String c = String(s).substring(0, n);
  if (n == 1) c.toUpperCase();
  return c;
}

void drawSplash(const String& line1, const String& line2, const String& line3) {
  tft.fillScreen(BG);
  drawText("MODELROGS", 120, 58, F_32, TXT, BG, TC_DATUM);
  drawText("Display", 120, 94, F_16, VIOLET, BG, TC_DATUM);
  int h = line3.length() ? 78 : 60;
  card(16, 134, 208, h, 12, CARD);
  drawText(fitText(line1, F_13, 192), 120, 146, F_13, TXT, CARD, TC_DATUM);
  drawText(fitText(line2, F_11, 192), 120, 168, F_11, MUTED, CARD, TC_DATUM);
  if (line3.length()) drawText(fitText(line3, F_11, 192), 120, 188, F_11, MUTED, CARD, TC_DATUM);
  drawText("v" FW_VERSION, 120, 221, F_11, DIM, BG, TC_DATUM);
}

// ----------------------------------------------------------------- Heure

static int  lastClockMinute = -1;
static char lastDate[32] = "";

// Page Heure : occupe toute la hauteur utile (3 px masqués en haut et en bas).
static const int Y_JOUR = 20, Y_CHIFFRES = 41, H_CHIFFRES = 60, Y_DATE = 111, Y_CARTES = 142;

static void drawClockDigits() {
  tft.fillRect(0, Y_CHIFFRES - 2, 240, H_CHIFFRES + 4, BG);
  if (!timeValid()) {
    drawText("--:--", 120, Y_CHIFFRES + 14, F_32, MUTED, BG, TC_DATUM);
    lastClockMinute = -1;
    return;
  }
  uint32_t t = localEpoch();
  int minuteOfDay = (t % 86400UL) / 60;
  char hh[3], mm[3];
  snprintf(hh, sizeof(hh), "%02d", minuteOfDay / 60);
  snprintf(mm, sizeof(mm), "%02d", minuteOfDay % 60);
  // Chaque chiffre est placé d'après son encre réelle : « : » à égale distance
  // des heures et des minutes, et centré sur la hauteur des chiffres.
  const char ch[5] = { hh[0], hh[1], ':', mm[0], mm[1] };
  GlyphBox g[5];
  for (int i = 0; i < 5; i++) glyphBox(F_76, (uint8_t)ch[i], g[i]);
  int hhL = g[0].dx, hhR = g[0].adv + g[1].dx + g[1].w;   // encre des heures (depuis la plume)
  int mmL = g[3].dx, mmR = g[3].adv + g[4].dx + g[4].w;   // encre des minutes
  // Écart encre chiffre <-> « : » : 9 px, réduit pour les heures larges
  // (« 00:00 ») afin que l'ensemble tienne dans 232 px (plume jamais à x = 0).
  int ECART = (232 - (hhR - hhL) - (mmR - mmL) - g[2].w) / 2;
  ECART = ECART > 9 ? 9 : (ECART < 3 ? 3 : ECART);
  int total = (hhR - hhL) + ECART + g[2].w + ECART + (mmR - mmL);
  int encre = 120 - total / 2;                            // bord gauche de l'encre
  int xH = encre - hhL;
  int xC = encre + (hhR - hhL) + ECART - g[2].dx;
  int xM = encre + (hhR - hhL) + ECART + g[2].w + ECART - mmL;
  // Centre vertical : (haut + bas) de l'encre, mesuré depuis la ligne de base
  int dyC = ((g[2].dy * 2 - g[2].h) - (g[0].dy * 2 - g[0].h)) / 2;
  drawText(String(ch[0]), xH, Y_CHIFFRES, F_76, TXT, BG);
  drawText(String(ch[1]), xH + g[0].adv, Y_CHIFFRES, F_76, TXT, BG);
  drawText(":", xC, Y_CHIFFRES + dyC, F_76, VIOLET, BG);
  drawText(String(ch[3]), xM, Y_CHIFFRES, F_76, TXT, BG);
  drawText(String(ch[4]), xM + g[3].adv, Y_CHIFFRES, F_76, TXT, BG);
  lastClockMinute = minuteOfDay;
}

// Carte lever / coucher : icône et libellé en haut, heure en grand dessous.
static void sunCard(int x, const char* label, const char* value, uint16_t color) {
  const int w = 103, h = 75;
  card(x, Y_CARTES, w, h, 12, CARD);
  halfSun(x + 18, Y_CARTES + 15, color, CARD);
  drawText(label, x + 34, Y_CARTES + 10, F_13, MUTED, CARD);
  drawText(value, x + w / 2, Y_CARTES + 34, F_32, TXT, CARD, TC_DATUM);
}

void drawHeure(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  String jour = gTime.jour;
  jour.toUpperCase();
  drawText(jour, 120, Y_JOUR, F_13, MUTED, BG, TC_DATUM);
  drawClockDigits();
  drawText(gTime.date, 120, Y_DATE, F_18, TXT2, BG, TC_DATUM);
  if (gTime.lever[0] && gTime.coucher[0]) {
    sunCard(14, "Lever", gTime.lever, AMBRE);
    sunCard(123, "Coucher", gTime.coucher, CORAIL);
  }
  strncpy(lastDate, gTime.date, sizeof(lastDate) - 1);
  pageDots(dotIndex, dotCount, VIOLET);
}

bool clockTick() {
  if (strcmp(lastDate, gTime.date) != 0) return true;   // nouveau jour
  int m = localMinuteOfDay();
  if (m != lastClockMinute) drawClockDigits();
  return false;
}

// ----------------------------------------------------------------- Météo

void drawMeteo(uint8_t dotIndex, uint8_t dotCount) {
  JsonDocument doc;
  int code = gwGet("/meteo?ville=" + urlEncode(cfg.ville), doc);
  tft.fillScreen(BG);
  if (code != 200) {
    errorPage("Météo", code == -5 ? String(doc["err"] | "") : String(gwErrorText(code)));
    pageDots(dotIndex, dotCount, AMBRE);
    return;
  }

  drawText(fitText(doc["v"] | "", F_16, 140), 14, 15, F_16, TXT, BG);
  drawText("Aujourd'hui", 226, 18, F_11, MUTED, BG, TR_DATUM);

  weatherIcon(doc["i"] | 2, 54, 80, 1.1f, BG);

  String temp = String((int)(doc["t"] | 0)) + "°";
  drawText(temp, 104, 44, F_52, TXT, BG);
  drawText(fitText(doc["l"] | "", F_13, 124), 106, 100, F_13, TXT2, BG);
  int x = 106;
  x += drawText(String((int)(doc["mn"] | 0)) + "°", x, 119, F_13, BLEU, BG);
  x += drawText(" / ", x, 119, F_13, SEP, BG);
  drawText(String((int)(doc["mx"] | 0)) + "°", x, 119, F_13, AMBRE, BG);

  uint8_t k = 0;
  for (JsonObject j : doc["j"].as<JsonArray>()) {
    if (k >= 3) break;
    int cx = 14 + k * 73;
    card(cx, 143, 66, 74, 10, CARD);
    drawText(j["d"] | "", cx + 33, 149, F_11, MUTED, CARD, TC_DATUM);
    weatherIcon(j["i"] | 2, cx + 33, 180, 0.42f, CARD);
    // Même ordre et mêmes couleurs que le jour actuel : min (bleu) / max (ambre)
    String mn = String((int)(j["mn"] | 0)) + "°";
    String sep = "/";
    String mx = String((int)(j["mx"] | 0)) + "°";
    const int esp = 3;
    int w = textW(mn, F_13) + textW(sep, F_13) + textW(mx, F_13) + 2 * esp;
    int tx = cx + 33 - w / 2;
    tx += drawText(mn, tx, 197, F_13, BLEU, CARD) + esp;
    tx += drawText(sep, tx, 197, F_13, SEP, CARD) + esp;
    drawText(mx, tx, 197, F_13, AMBRE, CARD);
    k++;
  }

  staleMark(doc["p"] | 0);
  pageDots(dotIndex, dotCount, AMBRE);
}

// --------------------------------------------------------------- Discord

static const uint16_t AVATAR[] = { VIOLET, VERT, AMBRE, BLEU, CORAIL };

// Image de la passerelle, ou à défaut un remplaçant de même forme (couleur + initiale).
static void imageOuInitiale(const char* jeton, int x, int y, int w, int h, int rayon, uint16_t fond,
                            uint16_t couleur, const char* nom, FontId f) {
  if (gwImage(jeton, x, y, w, h, fond, rayon)) return;
  tft.fillRect(x, y, w, h, fond);
  tft.fillSmoothRoundRect(x, y, w, h, rayon, couleur, fond);
  drawText(firstChar(nom), x + w / 2, y + h / 2 + 1, f, SOMBRE, couleur, MC_DATUM);
}

// Petites icônes d'état vocal (14 x 14), tracées par le code
static void micCoupe(int x, int y, uint16_t c, uint16_t bg) {
  tft.fillSmoothRoundRect(x + 4, y, 6, 9, 3, c, bg);
  tft.drawSmoothArc(x + 7, y + 6, 6, 5, 0, 90, c, bg, false);     // support en U (0° = bas)
  tft.drawSmoothArc(x + 7, y + 6, 6, 5, 270, 360, c, bg, false);
  tft.drawWideLine(x + 7, y + 12, x + 7, y + 14, 1.6f, c, bg);
  tft.drawWideLine(x + 1, y + 14, x + 13, y, 3.4f, bg, bg);
  tft.drawWideLine(x + 1, y + 14, x + 13, y, 1.7f, c, bg);
}

static void casqueCoupe(int x, int y, uint16_t c, uint16_t bg) {
  tft.drawSmoothArc(x + 7, y + 8, 7, 5, 90, 270, c, bg, true);
  tft.fillSmoothRoundRect(x, y + 7, 4, 7, 1, c, bg);
  tft.fillSmoothRoundRect(x + 10, y + 7, 4, 7, 1, c, bg);
  tft.drawWideLine(x, y + 14, x + 14, y, 3.4f, bg, bg);
  tft.drawWideLine(x, y + 14, x + 14, y, 1.7f, c, bg);
}

static void camera(int x, int y, uint16_t c, uint16_t bg) {
  tft.fillSmoothRoundRect(x, y + 3, 10, 9, 2, c, bg);
  tft.fillTriangle(x + 10, y + 7, x + 15, y + 3, x + 15, y + 11, c);
}

static int etiquetteLive(int xDroite, int y, uint16_t bg) {
  int w = textW("LIVE", F_11) + 10;
  tft.fillSmoothRoundRect(xDroite - w, y, w, 17, 4, ROUGE, bg);
  drawText("LIVE", xDroite - w / 2, y + 9, F_11, TXT, ROUGE, MC_DATUM);
  return w;
}

void drawDiscord(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  if (!gDisc.ok) {
    errorPage("Discord", gDisc.err);
    pageDots(dotIndex, dotCount, BLURPLE);
    return;
  }

  // En-tête : icône du serveur, nom du serveur, « N en vocal · salon »
  if (!gwImage(gDisc.icone, 14, 12, 34, 34, BG, 10)) {
    card(14, 12, 34, 34, 10, BLURPLE);
    headsetIcon(31, 28, TXT, BLURPLE);
  }
  drawText(fitText(gDisc.serveur[0] ? gDisc.serveur : "Discord", F_16, 168), 58, 12, F_16, TXT, BG);
  tft.fillSmoothCircle(62, 40, 3, gDisc.n ? VERT : DIM, BG);
  String ligne = gDisc.n ? String(gDisc.n) + " en vocal · " : String("Personne · ");
  drawText(fitText(ligne + gDisc.canal, F_11, 156), 70, 33, F_11, MUTED, BG);

  if (gDisc.n <= 4 && gDisc.count == gDisc.n) {
    // --- 1 à 4 personnes : une carte chacune (avatar, nom, durée, état)
    for (uint8_t i = 0; i < gDisc.count; i++) {
      int y = 60 + i * 41;
      card(14, y, 212, 36, 10, CARD);
      imageOuInitiale(gDisc.avatar[i], 19, y + 5, 26, 26, 13, CARD, AVATAR[i % 5], gDisc.names[i], F_13);
      uint8_t e = gDisc.etat[i];
      int droite = 216;                       // bord droit des indicateurs
      if (gDisc.isNew[i]) {
        pill(droite, y + 9, "arrivé", F_11, TXT, BLURPLE, CARD, true);
        droite -= textW("arrivé", F_11) + 20;
      } else if (e & DISC_LIVE) {
        droite -= etiquetteLive(droite, y + 10, CARD) + 6;
      }
      if (!gDisc.isNew[i]) {
        if (e & DISC_SOURD)       { casqueCoupe(droite - 14, y + 11, ROUGE_TX, CARD); droite -= 20; }
        else if (e & DISC_MUET)   { micCoupe(droite - 14, y + 11, ROUGE_TX, CARD); droite -= 20; }
        if ((e & DISC_CAMERA) && !(e & DISC_LIVE)) { camera(droite - 15, y + 11, MUTED, CARD); droite -= 21; }
      }
      int maxW = droite - 52 - 6;
      drawText(fitText(gDisc.names[i], F_13, maxW), 52, y + 4, F_13, TXT, CARD);
      if (gDisc.depuis[i][0]) drawText(gDisc.depuis[i], 52, y + 21, F_11, MUTED, CARD);
    }
  } else {
    // --- 5 personnes et plus : grille de 4 x 2 avatars (le 8e devient « +N »)
    uint8_t places = gDisc.n > MAX_DISC_NAMES ? MAX_DISC_NAMES - 1 : min<uint8_t>(gDisc.count, MAX_DISC_NAMES);
    for (uint8_t i = 0; i < places; i++) {
      int cx = 40 + (i % 4) * 53, cy = 86 + (i / 4) * 74;
      imageOuInitiale(gDisc.avatar[i], cx - 21, cy - 21, 42, 42, 21, BG, AVATAR[i % 5], gDisc.names[i], F_18);
      if (gDisc.isNew[i]) tft.drawSmoothArc(cx, cy, 25, 23, 0, 360, BLURPLE, BG, false);
      uint8_t e = gDisc.etat[i];
      int bx = cx + 12, by = cy + 12;         // pastille d'état, en bas à droite de l'avatar
      if (e & DISC_LIVE) {
        tft.fillSmoothCircle(bx, by, 9, BG, BG);
        tft.fillSmoothCircle(bx, by, 7, ROUGE, BG);
        tft.fillSmoothCircle(bx, by, 2, TXT, ROUGE);
      } else if (e & (DISC_SOURD | DISC_MUET)) {
        tft.fillSmoothCircle(bx, by, 10, BG, BG);
        if (e & DISC_SOURD) casqueCoupe(bx - 7, by - 7, ROUGE_TX, BG);
        else micCoupe(bx - 7, by - 7, ROUGE_TX, BG);
      }
      drawText(fitText(gDisc.names[i], F_11, 50), cx, cy + 27, F_11, TXT, BG, TC_DATUM);
    }
    if (gDisc.n > places) {
      int cx = 40 + (places % 4) * 53, cy = 86 + (places / 4) * 74;
      tft.fillSmoothCircle(cx, cy, 21, CARD, BG);
      drawText("+" + String(gDisc.n - places), cx, cy + 1, F_16, TXT, CARD, MC_DATUM);
    }
  }

  staleMark(gDisc.stale);
  pageDots(dotIndex, dotCount, BLURPLE);
}

// ----------------------------------------------------------- Supervision

static uint16_t stateColor(uint8_t st) {
  switch (st) {
    case SUP_PANNE:    return ROUGE;
    case SUP_INSTABLE: return AMBRE;
    case SUP_MAINT:    return BLEU;
    default:           return VERT;
  }
}

static const char* stateLabel(uint8_t st) {
  switch (st) {
    case SUP_PANNE:    return "panne";
    case SUP_INSTABLE: return "instable";
    case SUP_MAINT:    return "maint.";
    default:           return "ok";
  }
}

void drawSupervision(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  if (!gSup.ok) {
    errorPage("Supervision", gSup.err);
    pageDots(dotIndex, dotCount, VERT);
    return;
  }

  drawText("Supervision", 14, 15, F_16, TXT, BG);
  drawText(String(gSup.tot) + (gSup.tot > 1 ? " services" : " service"), 14, 35, F_11, MUTED, BG);

  // Bulle de synthèse en haut à droite, alignée sur le titre (comme en 2.3).
  // Si le texte complet toucherait « Supervision », seul le problème est gardé.
  String resume, court;
  uint16_t pillCol;
  if (gSup.pa > 0) {
    resume = court = String(gSup.pa) + " en panne";
    pillCol = ROUGE;
  } else if (gSup.in > 0) {
    court = String(gSup.in) + " instable";
    resume = String(gSup.okn) + " OK · " + court;
    pillCol = AMBRE;
  } else if (gSup.ma > 0) {
    court = String(gSup.ma) + " maint.";
    resume = String(gSup.okn) + " OK · " + court;
    pillCol = BLEU;
  } else {
    resume = court = "Tout est OK";
    pillCol = VERT;
  }
  const int finTitre = 14 + textW("Supervision", F_16) + 8;
  if (226 - (textW(resume, F_11) + 14) < finTitre) resume = court;
  pill(226, 15, resume, F_11, SOMBRE, pillCol, BG, true);

  if (!gSup.resume) {
    // Tuiles (jusqu'à 12 services), les problèmes en premier.
    uint8_t n = gSup.count;
    uint8_t rows = (n + 1) / 2;
    const int top = 59, avail = 158, gap = 6;
    int h = rows ? min(32, (avail - (rows - 1) * gap) / rows) : 32;
    int r = min(10, h / 2);
    for (uint8_t i = 0; i < n; i++) {
      int x = 14 + (i % 2) * 109;
      int y = top + (i / 2) * (h + gap);
      uint8_t st = gSup.st[i];
      uint16_t c = stateColor(st);
      if (st == SUP_OK) card(x, y, 103, h, r, CARD);
      else outlinedCard(x, y, 103, h, r, c, CARD);
      tft.fillSmoothCircle(x + 12, y + h / 2, 4, c, CARD);
      drawText(fitText(gSup.names[i], F_13, 75), x + 22, y + h / 2 + 1, F_13, TXT, CARD, ML_DATUM);
    }
  } else {
    // Résumé (plus de 12 services) : compteur + services en défaut.
    uint16_t cc = gSup.pa ? ROUGE : (gSup.in ? AMBRE : VERT);
    String big = String(gSup.okn);
    String small = "/ " + String(gSup.tot) + " OK";
    int wB = textW(big, F_52), wS = textW(small, F_18);
    int x = 120 - (wB + 8 + wS) / 2;
    drawText(big, x, 106, F_52, cc, BG, L_BASELINE);
    drawText(small, x + wB + 8, 106, F_18, TXT2, BG, L_BASELINE);

    uint8_t shown = min<uint8_t>(gSup.count, 3);
    for (uint8_t i = 0; i < shown; i++) {
      int y = 117 + i * 30;
      uint8_t st = gSup.st[i];
      uint16_t c = stateColor(st);
      outlinedCard(14, y, 212, 26, 9, c, CARD);
      tft.fillSmoothCircle(26, y + 13, 4, c, CARD);
      drawText(fitText(gSup.names[i], F_13, 130), 36, y + 14, F_13, TXT, CARD, ML_DATUM);
      drawText(stateLabel(st), 218, y + 14, F_11, st == SUP_PANNE ? ROUGE_TX : c, CARD, MR_DATUM);
    }
    uint8_t more = (gSup.count - shown) + gSup.extra;
    if (more) drawText("+" + String(more) + " autre(s)", 120, 206, F_11, MUTED, BG, TC_DATUM);
    if (gSup.count == 0) drawText("Tout fonctionne", 120, 148, F_16, VERT, BG, TC_DATUM);
  }

  staleMark(gSup.stale);
  pageDots(dotIndex, dotCount, VERT);
}

// ---------------------------------------------------------------- Alerte

void drawAlert() {
  tft.fillScreen(ALERTE);
  warningIcon(120, 51, TXT, ALERTE);
  drawText("PANNE", 120, 84, F_32, TXT, ALERTE, TC_DATUM);
  drawText(fitText(gSup.alName, F_18, 216), 120, 125, F_18, TXT, ALERTE, TC_DATUM);

  String depuis;
  if (gSup.alMin == 0) depuis = "Hors ligne depuis moins d'1 min";
  else if (gSup.alMin < 60) depuis = "Hors ligne depuis " + String(gSup.alMin) + " min";
  else {
    char buf[40];
    snprintf(buf, sizeof(buf), "Hors ligne depuis %u h %02u", gSup.alMin / 60, gSup.alMin % 60);
    depuis = buf;
  }
  drawText(depuis, 120, 155, F_13, TXT, ALERTE, TC_DATUM);

  card(16, 183, 208, 36, 10, ALERTE2, ALERTE);
  String bilan = String(gSup.alCount) + (gSup.alCount > 1 ? " services sur " : " service sur ")
               + String(gSup.tot) + " en panne";
  drawText(bilan, 120, 202, F_13, TXT, ALERTE2, MC_DATUM);
}

// --------------------------------------------------------------- Bitcoin

// ---------------------------------------------------------------- Crypto

static uint16_t accentCrypto(const char* symbole) {
  if (strcmp(symbole, "BTC") == 0) return BTC;
  if (strcmp(symbole, "ETH") == 0) return ETH;
  if (strcmp(symbole, "XRP") == 0) return XRP;
  return VIOLET;
}

// Courbe de points normalisés 0..100, dans le cadre [x0, x1] x [haut, bas].
static void courbe(JsonArray pts, int x0, int x1, int haut, int bas, uint16_t ligne, uint16_t bg,
                   bool aire, float epaisseur, bool pointFinal) {
  uint8_t n = min<size_t>(pts.size(), 60);
  if (n < 2) return;
  uint8_t v[60];
  for (uint8_t i = 0; i < n; i++) v[i] = constrain((int)pts[i], 0, 100);
  const float base = bas - 4, ech = (bas - 4 - haut) / 100.0f;
  auto yAt = [&](float idx) {
    uint8_t i = min<int>((int)idx, n - 2);
    float f = idx - i;
    float a = base - v[i] * ech, b = base - v[i + 1] * ech;
    return a + (b - a) * f;
  };
  if (aire) {   // aire sous la courbe : dégradé vertical, opaque en haut
    for (int x = x0; x <= x1; x++) {
      float idx = (float)(x - x0) * (n - 1) / (x1 - x0);
      for (int y = (int)ceilf(yAt(idx)); y < bas; y += 2) {
        uint8_t alpha = 90 * (bas - y) / (bas - haut);
        tft.drawFastVLine(x, y, 2, tft.alphaBlend(alpha, ligne, bg));
      }
    }
  }
  for (uint8_t i = 0; i + 1 < n; i++) {
    float xa = x0 + (float)i * (x1 - x0) / (n - 1);
    float xb = x0 + (float)(i + 1) * (x1 - x0) / (n - 1);
    tft.drawWideLine(xa, base - v[i] * ech, xb, base - v[i + 1] * ech, epaisseur, ligne, bg);
  }
  if (pointFinal) {
    int ye = (int)lroundf(base - v[n - 1] * ech);
    uint16_t halo = tft.alphaBlend(64, ligne, bg);
    tft.fillSmoothCircle(x1, ye, 6, halo, bg);
    tft.fillSmoothCircle(x1, ye, 3, ligne, halo);
  }
}

// Logo officiel (CoinGecko, via la passerelle) ou pastille de couleur avec l'initiale.
static void logoCrypto(JsonObject c, int x, int y, int d, uint16_t fond) {
  const char* sym = c["s"] | "?";
  if (gwImage(c["i"] | "", x, y, d, d, fond, d / 2, true)) return;
  uint16_t a = accentCrypto(sym);
  tft.fillSmoothCircle(x + d / 2, y + d / 2, d / 2, a, fond);
  drawText(firstChar(sym), x + d / 2, y + d / 2 + 1, d >= 28 ? F_18 : F_13, SOMBRE, a, MC_DATUM);
}

static int variations(JsonObject c, int x, int y, uint16_t bg) {
  bool u24 = c["u24"] | 1, u7 = c["u7"] | 1;
  String p1 = String("24 h ") + (c["v24"] | "");
  String p2 = String("7 j ") + (c["v7"] | "");
  pill(x, y, p1, F_11, u24 ? VERT : ROUGE_TX, u24 ? VERT_BG : ROUGE_BG, bg);
  int x2 = x + textW(p1, F_11) + 20;
  pill(x2, y, p2, F_11, u7 ? VERT : ROUGE_TX, u7 ? VERT_BG : ROUGE_BG, bg);
  return x2 + textW(p2, F_11) + 14;
}

// La mise en page dépend du nombre de cryptos choisies :
//   1 -> grande page (prix, variations, courbe pleine largeur)
//   2 -> deux moitiés (prix, variations, courbe)
//   3 -> trois lignes (prix, variation 7 jours, mini-courbe)
void drawBtc(uint8_t dotIndex, uint8_t dotCount) {
  JsonDocument doc;
  int code = gwGet(modulePath(PG_BTC), doc);
  tft.fillScreen(BG);
  if (code != 200) {
    errorPage("Crypto", code == -5 ? String(doc["err"] | "") : String(gwErrorText(code)));
    pageDots(dotIndex, dotCount, BTC);
    return;
  }
  JsonArray cs = doc["c"].as<JsonArray>();
  uint8_t n = min<size_t>(cs.size(), 3);
  uint16_t accent = accentCrypto(cs[0]["s"] | "");

  if (n <= 1) {
    JsonObject c = cs[0];
    logoCrypto(c, 14, 16, 30, BG);
    drawText(fitText(c["n"] | "", F_16, 170), 52, 16, F_16, TXT, BG);
    drawText(String(c["s"] | "") + " · 7 jours", 52, 35, F_11, MUTED, BG);
    drawText(fitText(c["px"] | "", F_32, 212), 14, 54, F_32, TXT, BG);
    variations(c, 14, 91, BG);
    courbe(c["pts"], 14, 220, 116, 204, (c["u7"] | 1) ? VERT : ROUGE, BG, true, 2.5f, true);
    drawText(doc["d0"] | "", 14, 206, F_11, MUTED2, BG);
    drawText(doc["d1"] | "", 226, 206, F_11, MUTED2, BG, TR_DATUM);

  } else if (n == 2) {
    for (uint8_t k = 0; k < 2; k++) {
      JsonObject c = cs[k];
      int y = k ? 118 : 10;
      logoCrypto(c, 14, y + 2, 24, BG);
      int wPrix = drawText(c["px"] | "", 226, y + 4, F_18, TXT, BG, TR_DATUM);
      drawText(fitText(c["n"] | "", F_13, 212 - 30 - wPrix - 8), 44, y + 7, F_13, TXT, BG);
      variations(c, 14, y + 33, BG);
      courbe(c["pts"], 14, 220, y + 58, y + 96, (c["u7"] | 1) ? VERT : ROUGE, BG, true, 2.0f, true);
    }
    tft.drawFastHLine(14, 113, 212, DIM);

  } else {
    drawText("Cryptos", 14, 15, F_16, TXT, BG);
    drawText("7 jours", 14, 35, F_11, MUTED, BG);
    drawText(doc["d1"] | "", 226, 18, F_11, MUTED2, BG, TR_DATUM);
    for (uint8_t k = 0; k < 3; k++) {
      JsonObject c = cs[k];
      int y = 58 + k * 54;
      card(14, y, 212, 48, 12, CARD);
      logoCrypto(c, 22, y + 12, 24, CARD);
      drawText(c["px"] | "", 54, y + 7, F_18, TXT, CARD);
      int w = drawText(c["s"] | "", 54, y + 29, F_11, MUTED, CARD);
      bool u7 = c["u7"] | 1;
      drawText(c["v7"] | "", 54 + w + 8, y + 29, F_11, u7 ? VERT : ROUGE_TX, CARD);
      courbe(c["pts"], 150, 214, y + 9, y + 41, u7 ? VERT : ROUGE, CARD, false, 2.0f, false);
    }
  }

  staleMark(doc["p"] | 0);
  pageDots(dotIndex, dotCount, accent);
}

// ------------------------------------------------------------------ Plex

static void plexBarre(int x, int y, int w, int h, uint16_t pg, uint16_t bg) {
  tft.fillSmoothRoundRect(x, y, w, h, h / 2, DIM, bg);
  int plein = max<int>(h, (long)w * pg / 1000);
  tft.fillSmoothRoundRect(x, y, plein, h, h / 2, PLEX, DIM);
}

// Coupe un titre en deux lignes au dernier espace qui tient dans maxW.
static void deuxLignes(const char* titre, FontId f, int maxW, String& l1, String& l2) {
  String t = titre;
  if (textW(t, f) <= maxW) { l1 = t; l2 = ""; return; }
  int coupe = -1;
  for (int i = 0; i < (int)t.length(); i++) {
    if (t[i] == ' ' && textW(t.substring(0, i), f) <= maxW) coupe = i;
  }
  if (coupe <= 0) { l1 = fitText(t, f, maxW); l2 = ""; return; }
  l1 = t.substring(0, coupe);
  l2 = fitText(t.substring(coupe + 1), f, maxW);
}

static const int PLEX_Y_BARRE = 188;

static void plexProgresUne() {
  const PlexState::Ligne& l = gPlex.l[0];
  tft.fillRect(14, PLEX_Y_BARRE - 2, 213, 30, BG);
  plexBarre(14, PLEX_Y_BARRE, 212, 6, l.pg, BG);
  drawText(l.pos, 14, PLEX_Y_BARRE + 12, F_11, TXT, BG);
  drawText(l.reste, 226, PLEX_Y_BARRE + 12, F_11, MUTED, BG, TR_DATUM);
}

static void plexProgresListe() {
  for (uint8_t i = 0; i < gPlex.count; i++) {
    int y = 60 + i * 54;
    tft.fillRect(82, y + 29, 136, 9, CARD);
    plexBarre(84, y + 31, 132, 5, gPlex.l[i].pg, CARD);
  }
}

void plexProgress() {
  if (!gPlex.ok || gPlex.count == 0) return;
  if (gPlex.n == 1) plexProgresUne();
  else plexProgresListe();
}

void drawPlex(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  if (!gPlex.ok || gPlex.count == 0) {
    errorPage("Plex", "Aucune lecture en cours");
    pageDots(dotIndex, dotCount, PLEX);
    return;
  }

  drawText("Plex", 14, 15, F_16, TXT, BG);
  String bulle = String(gPlex.n) + (gPlex.n > 1 ? " lectures" : " lecture");
  pill(226, 15, bulle, F_11, SOMBRE, PLEX, BG, true);

  if (gPlex.n == 1) {
    // --- Une lecture : affiche, titre, qui regarde, mode, progression
    const PlexState::Ligne& l = gPlex.l[0];
    drawText(l.pause ? "En pause" : "Lecture en cours", 14, 35, F_11, MUTED, BG);
    if (!gwImage(l.affiche, 14, 58, 78, 117, BG, 8)) {
      card(14, 58, 78, 117, 8, CARD);
      tft.fillTriangle(44, 101, 44, 131, 68, 116, PLEX);   // « lecture »
    }
    const int x = 104, w = 122;
    String l1, l2;
    deuxLignes(l.titre, F_18, w, l1, l2);
    drawText(l1, x, 58, F_18, TXT, BG);
    int y = 80;
    if (l2.length()) { drawText(l2, x, 80, F_18, TXT, BG); y = 102; }
    drawText(fitText(l.sous, F_11, w), x, y + 4, F_11, MUTED, BG);
    // Qui regarde
    String u = fitText(l.user, F_11, w - 30);
    int wu = textW(u, F_11) + 26;
    card(x, 130, wu, 19, 9, CARD);
    tft.fillSmoothCircle(x + 10, 139, 3, PLEX, CARD);
    drawText(u, x + 18, 140, F_11, PLEX, CARD, ML_DATUM);
    // Mode de lecture
    if (l.pause)   pill(x, 155, "En pause", F_11, TXT, DIM, BG);
    else if (l.tc) pill(x, 155, "Transcodage", F_11, SOMBRE, AMBRE, BG);
    else           pill(x, 155, "Direct", F_11, SOMBRE, VERT, BG);
    plexProgresUne();

  } else {
    // --- Plusieurs lectures : 3 lignes au plus, sans affiche
    String sous = gPlex.tc ? "dont " + String(gPlex.tc) + (gPlex.tc > 1 ? " transcodages" : " transcodage")
                           : String("Toutes en lecture directe");
    if (gPlex.n > gPlex.count) sous = "3 premières · " + sous;
    drawText(fitText(sous, F_11, 212), 14, 35, F_11, MUTED, BG);
    for (uint8_t i = 0; i < gPlex.count; i++) {
      const PlexState::Ligne& l = gPlex.l[i];
      int y = 60 + i * 54;
      card(14, y, 212, 46, 10, CARD);
      const char* mode = l.pause ? "Pause" : (l.tc ? "Transcod." : "Direct");
      uint16_t cm = l.pause ? MUTED : (l.tc ? AMBRE : VERT);
      int wm = drawText(mode, 216, y + 8, F_11, cm, CARD, TR_DATUM);
      drawText(fitText(l.titre, F_13, 192 - wm - 8), 24, y + 7, F_13, TXT, CARD);
      drawText(fitText(l.user, F_11, 54), 24, y + 27, F_11, PLEX, CARD);
    }
    plexProgresListe();
  }

  staleMark(gPlex.stale);
  pageDots(dotIndex, dotCount, PLEX);
}
