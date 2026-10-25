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

// Page Heure : en-tête (jour à gauche, date JJ/MM/AA à droite, alignés sur
// les bords des chiffres x 24..216), heure en 68 px, météo du jour en bas.
// Bloc météo posé en bas de l'écran (bas de l'encre en y 214, juste au-dessus
// des points) : icône ajustée à son empreinte, centrée en x 72 ; température
// et min / max centrés en x 168 et sur la hauteur réelle de l'icône.
// L'heure est centrée entre l'en-tête et le haut du bloc météo.
static const int X_G = 24, X_D = 216;
static const int Y_ENTETE = 14, FIN_ENTETE = 34;      // encre de l'en-tête : y 18..33
static const int METEO_BAS = 215;                      // bas (exclu) de l'encre météo
static const int ICONE_X = 24, ICONE_W = 96, ICONE_H = 80, TEMP_CX = 168, ECART_TMM = 12;
static const int SANS_METEO = 222;                     // haut des points de pagination
static int yChiffres = 0;                              // haut de l'encre des chiffres

// Place l'heure au centre de l'espace entre l'en-tête et `haut`.
static void placerChiffres(int haut) {
  GlyphBox g;
  glyphBox(F_68, '0', g);
  yChiffres = FIN_ENTETE + (haut - FIN_ENTETE - g.h + 1) / 2;
}

// Date locale JJ/MM/AA calculée à partir de l'horloge (jours depuis 1970 ->
// année, mois, jour : algorithme « civil_from_days »).
static void dateCourte(char* out, size_t n) {
  int32_t z = (int32_t)(localEpoch() / 86400UL) + 719468;
  int32_t era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  int32_t y = (int32_t)yoe + era * 400 + (m <= 2);
  snprintf(out, n, "%02u/%02u/%02d", (unsigned)d, (unsigned)m, (int)(y % 100));
}

static void drawClockDigits() {
  GlyphBox g0;
  glyphBox(F_68, '0', g0);
  const int Y_CHIFFRES = yChiffres - (fontAscent(F_68) - g0.dy);   // ligne de texte
  tft.fillRect(0, yChiffres - 3, 240, g0.h + 6, BG);
  if (!timeValid()) {
    drawText("--:--", 120, yChiffres + 10, F_32, MUTED, BG, TC_DATUM);
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
  for (int i = 0; i < 5; i++) glyphBox(F_68, (uint8_t)ch[i], g[i]);
  int hhL = g[0].dx, hhR = g[0].adv + g[1].dx + g[1].w;   // encre des heures (depuis la plume)
  int mmL = g[3].dx, mmR = g[3].adv + g[4].dx + g[4].w;   // encre des minutes
  // Écart encre chiffre <-> « : » : 9 px, réduit pour les heures larges
  // (« 00:00 ») afin que l'ensemble tienne dans 232 px.
  int ECART = (232 - (hhR - hhL) - (mmR - mmL) - g[2].w) / 2;
  ECART = ECART > 9 ? 9 : (ECART < 3 ? 3 : ECART);
  int total = (hhR - hhL) + ECART + g[2].w + ECART + (mmR - mmL);
  int encre = 120 - total / 2;                            // bord gauche de l'encre
  int xH = encre - hhL;
  int xC = encre + (hhR - hhL) + ECART - g[2].dx;
  int xM = encre + (hhR - hhL) + ECART + g[2].w + ECART - mmL;
  // Centre vertical : (haut + bas) de l'encre, mesuré depuis la ligne de base
  int dyC = ((g[2].dy * 2 - g[2].h) - (g[0].dy * 2 - g[0].h)) / 2;
  drawText(String(ch[0]), xH, Y_CHIFFRES, F_68, TXT, BG);
  drawText(String(ch[1]), xH + g[0].adv, Y_CHIFFRES, F_68, TXT, BG);
  drawText(":", xC, Y_CHIFFRES + dyC, F_68, VIOLET, BG);
  drawText(String(ch[3]), xM, Y_CHIFFRES, F_68, TXT, BG);
  drawText(String(ch[4]), xM + g[3].adv, Y_CHIFFRES, F_68, TXT, BG);
  lastClockMinute = minuteOfDay;
}

// Météo du jour (sans carte ni libellé : l'icône suffit). Renvoie le haut de
// l'encre du bloc, ou SANS_METEO si la passerelle ne répond pas.
static int carteMeteoDuJour(JsonDocument& doc, bool ok) {
  if (!ok) return SANS_METEO;
  uint8_t code = doc["i"] | 2;
  String t = String((int)(doc["t"] | 0)) + "°";
  String a = String((int)(doc["mn"] | 0)) + "°", s = " / ", b = String((int)(doc["mx"] | 0)) + "°";
  int wMM = textW(a, F_18) + textW(s, F_18) + textW(b, F_18);
  int wT = max(textW(t, F_52), wMM);
  // Hauteurs d'encre réelles : chiffres du 52 px ; min / max avec la barre « / »,
  // qui monte et descend plus que les chiffres.
  GlyphBox gT, gM, gS;
  glyphBox(F_52, '0', gT);
  glyphBox(F_18, '0', gM);
  glyphBox(F_18, '/', gS);
  const int mHaut = max(gM.dy, gS.dy);                                   // au-dessus de la ligne de base
  const int mBas = max(gM.h - gM.dy, gS.h - gS.dy);                      // au-dessous
  const int hTexte = gT.h + ECART_TMM + mHaut + mBas;
  // Colonne de texte collée au bord droit (x 216), 96 px au moins ; l'icône prend le reste
  const int colW = max(wT, 2 * (X_D - TEMP_CX));
  const int cx = X_D - colW / 2;
  const int icW = min(ICONE_W, X_D - wT - 16 - ICONE_X);   // 96 px (centre x 72) sauf texte large
  int iw, ih;
  weatherIconScale(code, icW, ICONE_H, &iw, &ih);
  const int hBloc = max(ih, hTexte);
  const int haut = METEO_BAS - hBloc;
  // Centre commun de l'icône et du texte (en demi-pixels pour rester exact)
  const int cy2 = 2 * haut + hBloc;
  weatherIconFit(code, ICONE_X, (cy2 - ICONE_H) / 2, icW, ICONE_H, BG);
  int hautT = (cy2 - hTexte) / 2;                           // haut de l'encre de la température
  drawText(t, cx, hautT - (fontAscent(F_52) - gT.dy), F_52, TXT, BG, TC_DATUM);
  int x = cx - wMM / 2, y = hautT + gT.h + ECART_TMM + mHaut - gM.dy - (fontAscent(F_18) - gM.dy);
  x += drawText(a, x, y, F_18, BLEU, BG);
  x += drawText(s, x, y, F_18, SEP, BG);
  drawText(b, x, y, F_18, AMBRE, BG);
  staleMark(doc["p"] | 0);
  return haut;
}

void drawHeure(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  String jour = gTime.jour;
  jour.toUpperCase();
  drawText(jour, X_G, Y_ENTETE, F_18, TXT2, BG);
  if (timeValid()) {
    char d[12];
    dateCourte(d, sizeof(d));
    drawText(d, X_D, Y_ENTETE, F_18, TXT2, BG, TR_DATUM);
  }
  JsonDocument doc;
  bool ok = gwGet("/meteo?ville=" + urlEncode(cfg.ville), doc) == 200;   // sinon l'horloge reste seule
  placerChiffres(carteMeteoDuJour(doc, ok));
  drawClockDigits();
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

// Haut des deux rangées de blocs : 15 px d'espace en haut, entre les rangées et en bas.
static const int Y_METEO_HAUT = 15, Y_METEO_BAS = 118;

// « 10° / 15° » centré en cx : min (bleu) / max (ambre), même ordre partout.
static void minMaxCentre(JsonObject j, int cx, int y, FontId f, uint16_t bg) {
  String a = String((int)(j["mn"] | 0)) + "°", s = "/", b = String((int)(j["mx"] | 0)) + "°";
  const int e = 4;
  int w = textW(a, f) + textW(s, f) + textW(b, f) + 2 * e, x = cx - w / 2;
  x += drawText(a, x, y, f, BLEU, bg) + e;
  x += drawText(s, x, y, f, SEP, bg) + e;
  drawText(b, x, y, f, AMBRE, bg);
}

void drawMeteo(uint8_t dotIndex, uint8_t dotCount) {
  JsonDocument doc;
  int code = gwGet("/meteo?ville=" + urlEncode(cfg.ville), doc);
  tft.fillScreen(BG);
  if (code != 200) {
    errorPage("Météo", code == -5 ? String(doc["err"] | "") : String(gwErrorText(code)));
    pageDots(dotIndex, dotCount, AMBRE);
    return;
  }

  // 5 jours, sans ville ni météo du jour (affichée sur la page Heure) :
  // les deux prochains jours en grand, les trois suivants dessous.
  // Températures : 14 px, ou 12 px si l'une d'elles ne laisse pas 8 px entre deux blocs
  FontId ft = F_13;
  for (JsonObject j : doc["j"].as<JsonArray>()) {
    String t = String((int)(j["mn"] | 0)) + "°/" + String((int)(j["mx"] | 0)) + "°";
    if (textW(t, F_13) + 8 > 72 - 8) ft = F_11;
  }
  uint8_t k = 0;
  for (JsonObject j : doc["j"].as<JsonArray>()) {
    if (k >= 5) break;
    // 5 blocs identiques (jour, icône, min / max), sans cartes ni traits :
    // 3 en haut, 2 en bas, centrés sur l'écran (72 px entre les centres).
    // L'icône est ajustée à son empreinte dans une zone fixe de 66 x 52 px.
    // Encre d'un bloc : jour (y+3..y+14), icône (y+20..y+71), min / max (y+78..y+90).
    int cx = k < 3 ? 48 + k * 72 : 84 + (k - 3) * 72;
    int y = k < 3 ? Y_METEO_HAUT : Y_METEO_BAS;
    drawText(j["d"] | "", cx, y, F_13, TXT2, BG, TC_DATUM);
    weatherIconFit(j["i"] | 2, cx - 33, y + 20, 66, 52, BG);
    minMaxCentre(j, cx, y + 74 + (ft == F_11 ? 1 : 0), ft, BG);
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

// Variations en colonnes, sans bulle : intitulé au-dessus (24 h, 7 j, 30 j),
// valeur dessous, les deux dans la couleur de la variation (vert / rouge).
static void colonnesVariations(JsonObject c, int x0, int x1, int y, FontId fl, FontId fv, int dy,
                               bool avec30) {
  static const char* const LAB[3] = { "24 h", "7 j", "30 j" };
  static const char* const VAL[3] = { "v24", "v7", "v30" };
  static const char* const UP[3]  = { "u24", "u7", "u30" };
  const int n = avec30 ? 3 : 2, lc = (x1 - x0) / n;
  for (int i = 0; i < n; i++) {
    int cx = x0 + lc * i + lc / 2;
    uint16_t coul = (c[UP[i]] | 1) ? VERT : ROUGE_TX;
    drawText(LAB[i], cx, y, fl, coul, BG, TC_DATUM);
    drawText(fitText(c[VAL[i]] | "", fv, lc - 2), cx, y + dy, fv, coul, BG, TC_DATUM);
  }
}

// Texte de chiffres dont l'encre est centrée verticalement sur cy (hauteur d'un « 0 »)
static int drawTextCentreV(const String& t, int x, int cy, FontId f, uint16_t coul) {
  GlyphBox g;
  glyphBox(f, '0', g);
  int y = cy - g.h / 2 - (fontAscent(f) - g.dy);
  return drawText(t, x, y, f, coul, BG);
}

// Hauteur de l'encre des prix au-dessus de la ligne de base : celle du
// caractère le plus haut réellement affiché (« $ » dépasse les chiffres).
static int hautPrix(JsonArray cs, uint8_t n, FontId f) {
  int h = 0;
  for (uint8_t k = 0; k < n; k++) {
    const char* p = cs[k]["px"] | "";
    while (*p) {
      uint16_t u = (uint8_t)*p++;
      if ((u & 0xE0) == 0xC0 && *p) { u = ((u & 0x1F) << 6) | (*p++ & 0x3F); }
      else if ((u & 0xF0) == 0xE0 && p[0] && p[1]) { u = ((u & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
      GlyphBox g;
      if (u != ' ' && glyphBox(f, u, g)) h = max(h, g.dy);
    }
  }
  return h;
}

// « 24 h +1,9 % » : intitulé et valeur dans la couleur de la variation
static void groupeVariation(JsonObject c, const char* lab, const char* val, const char* up, int x, int y, FontId f) {
  drawText(String(lab) + (c[val] | ""), x, y, f, (c[up] | 1) ? VERT : ROUGE_TX, BG);
}

static int largeurVariation(JsonArray cs, uint8_t n, const char* lab, const char* val, FontId f) {
  int m = 0;
  for (uint8_t k = 0; k < n; k++) m = max<int>(m, textW(String(lab) + (cs[k][val] | ""), f));
  return m;
}

// Une crypto sur 30 jours : page unique (1 crypto choisie) ou page dédiée
// (variation sur 7 jours de +/- 5 % ou plus). Logo et prix sur une ligne,
// trois variations, puis la plus grande courbe possible.
static void pageCrypto30(JsonObject c, uint8_t dotIndex, uint8_t dotCount, bool perime) {
  tft.fillScreen(BG);
  // Logo + prix centrés ensemble sur l'écran ; prix centré sur la hauteur du logo
  String px = fitText(c["px"] | "", F_32, 212 - 36 - 12);
  int x0 = 120 - (36 + 12 + textW(px, F_32)) / 2;
  logoCrypto(c, x0, 10, 36, BG);
  drawTextCentreV(px, x0 + 36 + 12, 10 + 18, F_32, TXT);
  colonnesVariations(c, 14, 226, 54, F_11, F_16, 15, true);
  // Courbe sur 30 jours, avec « 30 J » en filigrane dans le graphique :
  // dégradé, puis filigrane, puis la ligne qui repasse devant.
  const int haut = 94, bas = 218, yF = 160;
  uint16_t ligne = (c["u30"] | 1) ? VERT : ROUGE;
  courbe(c["p30"], 14, 220, haut, bas, ligne, BG, true, 2.5f, false);
  uint16_t fondF = tft.alphaBlend(90 * (bas - (yF + 15)) / (bas - haut), ligne, BG);
  drawText("30 J", 120, yF, F_32, tft.alphaBlend(70, TXT, fondF), fondF, TC_DATUM);
  courbe(c["p30"], 14, 220, haut, bas, ligne, BG, false, 2.5f, true);
  staleMark(perime);
  pageDots(dotIndex, dotCount, accentCrypto(c["s"] | ""));
}

// Pages dédiées en attente (identifiants CoinGecko), affichées après l'écran
// « prix en vedette » par btcEcranSuivant().
static char    dediees[3][16];
static uint8_t nbDediees = 0, prochaineDediee = 0;

// « Prix en vedette » à 2 cryptos (place disponible : une moitié d'écran
// chacune). Par crypto :
//  - ligne 1 : logo de 56 px et prix (32, 26 ou 18 px selon la largeur),
//    centrés ensemble horizontalement, le prix centré sur la hauteur du logo ;
//  - ligne 2 : « 24 h … » et « 7 j … » centrés sur l'écran, dans la plus
//    grande police qui tient (16, 13 ou 11 px), colonnes communes aux deux.
// Les deux blocs et les trois espaces (haut, milieu, bas) sont égaux.
static void vedetteDeux(JsonArray cs, uint8_t dotIndex, uint8_t dotCount, bool perime) {
  const int L = 56, gapH = 12, gapLV = 12, ecartVar = 16;
  FontId fp = F_18;
  for (FontId f : { F_32, F_26, F_18 }) {
    int wp = max(textW(cs[0]["px"] | "", f), textW(cs[1]["px"] | "", f));
    fp = f;
    if (L + gapH + wp <= 212) break;
  }
  FontId fv = F_11;
  int w24 = 0, w7 = 0;
  for (FontId f : { F_16, F_13, F_11 }) {
    w24 = largeurVariation(cs, 2, "24 h ", "v24", f);
    w7 = largeurVariation(cs, 2, "7 j ", "v7", f);
    fv = f;
    if (w24 + ecartVar + w7 <= 212) break;
  }
  GlyphBox gv, gj;
  glyphBox(fv, '0', gv);
  glyphBox(fv, 'j', gj);
  const int desc = max(0, gj.h - gj.dy);
  const int hBloc = L + gapLV + gv.h + desc;            // encre d'un bloc
  const int espace = (222 - 2 * hBloc) / 3, reste = (222 - 2 * hBloc) - 3 * espace;
  const int hp = hautPrix(cs, 2, fp);
  int wpMax = 0;
  for (uint8_t k = 0; k < 2; k++) wpMax = max<int>(wpMax, textW(cs[k]["px"] | "", fp));
  const int x0 = 120 - (L + gapH + min(wpMax, 212 - L - gapH)) / 2;   // logo + prix centrés
  const int x24 = 120 - (w24 + ecartVar + w7) / 2, x7 = x24 + w24 + ecartVar;

  tft.fillScreen(BG);
  for (uint8_t k = 0; k < 2; k++) {
    JsonObject c = cs[k];
    int haut = 3 + reste / 2 + espace + k * (hBloc + espace);
    logoCrypto(c, x0, haut, L, BG);
    // prix : encre (hp px au-dessus de la ligne de base) centrée sur le logo
    int base = haut + L / 2 + hp / 2;
    drawText(fitText(c["px"] | "", fp, 212 - L - gapH), x0 + L + gapH, base - fontAscent(fp), fp, TXT, BG);
    int yv = haut + L + gapLV + gv.h - fontAscent(fv);  // ligne de base des variations
    groupeVariation(c, "24 h ", "v24", "u24", x24, yv, fv);
    groupeVariation(c, "7 j ", "v7", "u7", x7, yv, fv);
    if ((c["x"] | 0) && nbDediees < 3) {
      strncpy(dediees[nbDediees], c["id"] | "", sizeof(dediees[0]) - 1);
      dediees[nbDediees][sizeof(dediees[0]) - 1] = '\0';
      if (dediees[nbDediees][0]) nbDediees++;
    }
  }
  staleMark(perime);
  pageDots(dotIndex, dotCount, accentCrypto(cs[0]["s"] | ""));
}

// Crypto :
//   1 crypto     -> page sur 30 jours
//   2 ou 3       -> « prix en vedette » (logo, prix, variations 24 h et 7 j),
//                   puis une page dédiée par crypto dont |7 j| >= 5 %
void drawBtc(uint8_t dotIndex, uint8_t dotCount) {
  nbDediees = prochaineDediee = 0;
  JsonDocument doc;
  int code = gwGet(modulePath(PG_BTC), doc);
  if (code != 200) {
    tft.fillScreen(BG);
    errorPage("Crypto", code == -5 ? String(doc["err"] | "") : String(gwErrorText(code)));
    pageDots(dotIndex, dotCount, BTC);
    return;
  }
  JsonArray cs = doc["c"].as<JsonArray>();
  uint8_t n = min<size_t>(cs.size(), 3);
  bool perime = doc["p"] | 0;
  if (n <= 1) {
    pageCrypto30(cs[0], dotIndex, dotCount, perime);
    return;
  }

  if (n == 2) {
    vedetteDeux(cs, dotIndex, dotCount, perime);
    return;
  }

  // « Prix en vedette ». Chaque ligne = logo + bloc de texte (prix, puis
  // « 24 h … » et « 7 j … » sur une ligne). Le diamètre du logo est égal à la
  // hauteur du bloc de texte (haut du prix -> bas des chiffres des variations) :
  // logo et texte ont le même haut et le même bas. Polices : les plus grandes
  // qui tiennent pour toutes les cryptos de l'écran. Le groupe logo + texte est
  // centré horizontalement ; les lignes sont réparties avec des espaces égaux.
  static const FontId PRIX[3] = { F_32, F_26, F_18 };
  static const FontId VAR[3]  = { F_16, F_13, F_11 };
  const int gapH = 12, ecartVar = 12, gapPV = n == 2 ? 12 : 9;
  FontId fp = F_18, fv = F_11;
  int d = 0, wTexte = 0, w24 = 0, w7 = 0;
  bool trouve = false;
  for (int ip = 0; ip < 3 && !trouve; ip++) {
    for (int iv = (n == 2 ? 0 : 1); iv < 3 && !trouve; iv++) {
      GlyphBox gv;
      glyphBox(VAR[iv], '0', gv);
      int dd = hautPrix(cs, n, PRIX[ip]) + gapPV + gv.h;
      int wp = 0;
      for (uint8_t k = 0; k < n; k++) wp = max<int>(wp, textW(cs[k]["px"] | "", PRIX[ip]));
      int a = largeurVariation(cs, n, "24 h ", "v24", VAR[iv]), b = largeurVariation(cs, n, "7 j ", "v7", VAR[iv]);
      int wt = max(wp, a + ecartVar + b);
      if (dd + gapH + wt <= 212) {
        fp = PRIX[ip]; fv = VAR[iv]; d = dd; wTexte = wt; w24 = a; w7 = b; trouve = true;
      }
    }
  }
  if (!trouve) {                     // dernier recours : plus petites polices, prix tronqués
    GlyphBox gv;
    glyphBox(F_11, '0', gv);
    d = hautPrix(cs, n, F_18) + gapPV + gv.h;
    w24 = largeurVariation(cs, n, "24 h ", "v24", F_11);
    w7 = largeurVariation(cs, n, "7 j ", "v7", F_11);
    wTexte = 212 - d - gapH;
  }
  GlyphBox gv;
  glyphBox(fv, '0', gv);
  const int hp = hautPrix(cs, n, fp);               // encre du prix au-dessus de la ligne de base
  const int x0 = 120 - (d + gapH + wTexte) / 2;     // groupe centré sur l'écran
  const int xt = x0 + d + gapH;                      // colonne du texte
  GlyphBox gj;
  glyphBox(fv, 'j', gj);
  const int desc = max(0, gj.h - gj.dy);             // descente du « j » de « 7 j »
  const int espaceV = (222 - n * (d + desc)) / (n + 1);   // espaces visuels égaux (y 3..224)
  const int reste = (222 - n * (d + desc)) - espaceV * (n + 1);   // arrondi : réparti haut / bas

  tft.fillScreen(BG);
  for (uint8_t k = 0; k < n; k++) {
    JsonObject c = cs[k];
    int haut = 3 + reste / 2 + espaceV + k * (d + desc + espaceV);   // haut de l'encre de la ligne
    logoCrypto(c, x0, haut, d, BG);
    // Ligne de base du prix à haut + hp ; celle des variations au bas du logo
    drawText(fitText(c["px"] | "", fp, wTexte), xt, haut + hp - fontAscent(fp), fp, TXT, BG);
    int yv = haut + d - fontAscent(fv);
    groupeVariation(c, "24 h ", "v24", "u24", xt, yv, fv);
    groupeVariation(c, "7 j ", "v7", "u7", xt + w24 + ecartVar, yv, fv);
    if ((c["x"] | 0) && nbDediees < 3) {
      strncpy(dediees[nbDediees], c["id"] | "", sizeof(dediees[0]) - 1);
      dediees[nbDediees][sizeof(dediees[0]) - 1] = '\0';
      if (dediees[nbDediees][0]) nbDediees++;
    }
  }
  staleMark(perime);
  pageDots(dotIndex, dotCount, accentCrypto(cs[0]["s"] | ""));
}

bool btcEcranSuivant(uint8_t dotIndex, uint8_t dotCount) {
  while (prochaineDediee < nbDediees) {
    const char* id = dediees[prochaineDediee++];
    String devise = modParam(PG_BTC, "devise");
    JsonDocument doc;
    if (gwGet(String("/btc/30j?id=") + id + "&devise=" + (devise.length() ? devise : String("eur")), doc) != 200) {
      continue;                 // indisponible : page suivante
    }
    pageCrypto30(doc["c"][0], dotIndex, dotCount, doc["p"] | 0);
    return true;
  }
  return false;
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
