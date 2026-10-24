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

void drawDiscord(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  if (!gDisc.ok) {
    errorPage("Discord", gDisc.err);
    pageDots(dotIndex, dotCount, BLURPLE);
    return;
  }

  card(14, 15, 34, 34, 10, BLURPLE);
  headsetIcon(31, 31, TXT, BLURPLE);
  drawText("Salon vocal", 56, 16, F_11, MUTED, BG);
  drawText(fitText(gDisc.canal, F_16, 168), 56, 31, F_16, TXT, BG);

  if (gDisc.n == 0) {
    tft.fillSmoothCircle(19, 69, 4, DIM, BG);
    drawText("Personne en ligne", 30, 60, F_18, MUTED, BG);
  } else {
    tft.fillSmoothCircle(19, 69, 4, VERT, BG);
    drawText(String(gDisc.n) + " en ligne", 30, 60, F_18, TXT, BG);
  }

  uint8_t shown = gDisc.count <= 4 && gDisc.n <= 4 ? gDisc.count : 3;
  for (uint8_t i = 0; i < shown; i++) {
    int y = 87 + i * 34;
    card(14, y, 212, 30, 10, CARD);
    uint16_t av = AVATAR[i % 5];
    tft.fillSmoothCircle(33, y + 15, 11, av, CARD);
    drawText(firstChar(gDisc.names[i]), 33, y + 16, F_13, SOMBRE, av, MC_DATUM);
    int maxW = gDisc.isNew[i] ? 110 : 160;
    drawText(fitText(gDisc.names[i], F_13, maxW), 52, y + 16, F_13, TXT, CARD, ML_DATUM);
    if (gDisc.isNew[i]) pill(218, y + 5, "arrivé", F_11, TXT, BLURPLE, CARD, true);
  }
  if (gDisc.n > shown) {
    drawText("+" + String(gDisc.n - shown) + " autre(s)", 120, 194, F_13, MUTED, BG, TC_DATUM);
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
  if (!supConfigured()) {
    errorPage("Supervision", "Aucun service sélectionné");
    pageDots(dotIndex, dotCount, VERT);
    return;
  }
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

void drawBtc(uint8_t dotIndex, uint8_t dotCount) {
  JsonDocument doc;
  int code = gwGet(modulePath(PG_BTC), doc);
  tft.fillScreen(BG);
  if (code != 200) {
    errorPage("Bitcoin", code == -5 ? String(doc["err"] | "") : String(gwErrorText(code)));
    pageDots(dotIndex, dotCount, BTC);
    return;
  }

  // Pastille « ₿ »
  tft.fillSmoothCircle(29, 31, 15, BTC, BG);
  drawText("B", 30, 32, F_18, SOMBRE, BTC, MC_DATUM);
  tft.fillRect(27, 18, 2, 4, SOMBRE);
  tft.fillRect(31, 18, 2, 4, SOMBRE);
  tft.fillRect(27, 41, 2, 4, SOMBRE);
  tft.fillRect(31, 41, 2, 4, SOMBRE);
  drawText("Bitcoin", 52, 16, F_16, TXT, BG);
  drawText("BTC · 7 jours", 52, 35, F_11, MUTED, BG);

  drawText(fitText(doc["px"] | "", F_32, 212), 14, 54, F_32, TXT, BG);

  bool u24 = doc["u24"] | 1, u7 = doc["u7"] | 1;
  String p1 = String("24 h ") + (doc["v24"] | "");
  String p2 = String("7 j ") + (doc["v7"] | "");
  pill(14, 91, p1, F_11, u24 ? VERT : ROUGE_TX, u24 ? VERT_BG : ROUGE_BG, BG);
  pill(14 + textW(p1, F_11) + 20, 91, p2, F_11, u7 ? VERT : ROUGE_TX, u7 ? VERT_BG : ROUGE_BG, BG);

  // Courbe : points normalisés 0..100 envoyés par la passerelle
  JsonArray pts = doc["pts"].as<JsonArray>();
  uint8_t n = min<size_t>(pts.size(), 60);
  if (n >= 2) {
    uint8_t v[60];
    for (uint8_t i = 0; i < n; i++) v[i] = constrain((int)pts[i], 0, 100);
    const int X0 = 14, X1 = 220, TOP = 116, BOT = 204;   // courbe sur toute la largeur
    const float BASE = 200, ECH = 0.84f;   // 0 -> y=200, 100 -> y=116
    uint16_t line = u7 ? VERT : ROUGE;
    auto yAt = [&](float idx) {
      uint8_t i = min<int>((int)idx, n - 2);
      float f = idx - i;
      float a = BASE - v[i] * ECH, b = BASE - v[i + 1] * ECH;
      return a + (b - a) * f;
    };
    // Aire sous la courbe : dégradé vertical (opaque en haut, transparent en bas)
    for (int x = X0; x <= X1; x++) {
      float idx = (float)(x - X0) * (n - 1) / (X1 - X0);
      for (int y = (int)ceilf(yAt(idx)); y < BOT; y += 2) {
        uint8_t alpha = 90 * (BOT - y) / (BOT - TOP);
        tft.drawFastVLine(x, y, 2, tft.alphaBlend(alpha, line, BG));
      }
    }
    // Ligne
    for (uint8_t i = 0; i + 1 < n; i++) {
      float xa = X0 + (float)i * (X1 - X0) / (n - 1);
      float xb = X0 + (float)(i + 1) * (X1 - X0) / (n - 1);
      tft.drawWideLine(xa, BASE - v[i] * ECH, xb, BASE - v[i + 1] * ECH, 2.5f, line, BG);
    }
    int ye = (int)lroundf(BASE - v[n - 1] * ECH);
    uint16_t halo = tft.alphaBlend(64, line, BG);
    tft.fillSmoothCircle(X1, ye, 6, halo, BG);
    tft.fillSmoothCircle(X1, ye, 3, line, halo);
  }

  drawText(doc["d0"] | "", 14, 206, F_11, MUTED2, BG);
  drawText(doc["d1"] | "", 226, 206, F_11, MUTED2, BG, TR_DATUM);

  staleMark(doc["p"] | 0);
  pageDots(dotIndex, dotCount, BTC);
}
