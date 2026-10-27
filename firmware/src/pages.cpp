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

// Encre d'un texte UTF-8 : hauteur au-dessus et au-dessous de la ligne de base.
static void encreTexte(const String& t, FontId f, int& dessus, int& dessous) {
  for (unsigned i = 0; i < t.length();) {
    uint8_t c = t[i];
    uint16_t u = c;
    int n = c < 0x80 ? 1 : (c < 0xE0 ? 2 : 3);
    if (n == 2 && i + 1 < t.length()) u = ((c & 0x1F) << 6) | (t[i + 1] & 0x3F);
    if (n == 3 && i + 2 < t.length()) u = ((c & 0x0F) << 12) | ((t[i + 1] & 0x3F) << 6) | (t[i + 2] & 0x3F);
    i += n;
    GlyphBox g;
    if (u == ' ' || !glyphBox(f, u, g)) continue;
    dessus = max(dessus, g.dy);
    dessous = max(dessous, g.h - g.dy);
  }
}

// Encre horizontale d'un texte UTF-8, depuis la plume : [gauche, droite[.
static void encreLargeur(const String& t, FontId f, int& gauche, int& droite) {
  int plume = 0;
  gauche = 1 << 14; droite = -(1 << 14);
  for (unsigned i = 0; i < t.length();) {
    uint8_t c = t[i];
    uint16_t u = c;
    int n = c < 0x80 ? 1 : (c < 0xE0 ? 2 : 3);
    if (n == 2 && i + 1 < t.length()) u = ((c & 0x1F) << 6) | (t[i + 1] & 0x3F);
    if (n == 3 && i + 2 < t.length()) u = ((c & 0x0F) << 12) | ((t[i + 1] & 0x3F) << 6) | (t[i + 2] & 0x3F);
    i += n;
    GlyphBox g;
    if (!glyphBox(f, u, g)) continue;
    if (u != ' ' && g.w > 0) { gauche = min(gauche, plume + g.dx); droite = max(droite, plume + g.dx + g.w); }
    plume += g.adv;
  }
  if (gauche > droite) gauche = droite = 0;
}

// Page Météo : 5 blocs identiques (jour, icône, min / max), 2 en haut et 3 en
// bas. Chaque rangée est répartie d'après la largeur réelle de ses jours, avec
// des espaces égaux (bords compris). En hauteur : même espace en haut et entre les rangées, 6 px au-dessus
// des points de pagination ; l'icône prend toute la hauteur qui reste.
static const int METEO_PTS = 225 - 6;                    // bas (exclu) de l'encre
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

  JsonArray js = doc["j"].as<JsonArray>();
  const uint8_t nj = min<size_t>(js.size(), 5);
  // Largeur d'un bloc : 3 blocs et 4 espaces de 8 px au moins sur la rangée du bas
  const int BW = (240 - 4 * 8) / 3;
  // Températures : 14 px, ou 12 px si l'une d'elles ne tient pas dans le bloc
  FontId ft = F_13;
  for (uint8_t k = 0; k < nj; k++) {
    JsonObject j = js[k];
    String t = String((int)(j["mn"] | 0)) + "°/" + String((int)(j["mx"] | 0)) + "°";
    if (textW(t, F_13) + 8 > BW) ft = F_11;
  }
  // Encre réelle des jours et des températures affichés
  int jD = 0, jB = 0, tD = 0, tB = 0;
  for (uint8_t k = 0; k < nj; k++) {
    JsonObject j = js[k];
    encreTexte(j["d"] | "", F_13, jD, jB);
    encreTexte(String((int)(j["mn"] | 0)) + "°/" + String((int)(j["mx"] | 0)) + "°", ft, tD, tB);
  }
  const int gJI = 7, gIT = 7, e = 12;                    // jour -> icône -> températures ; espaces
  const int hB = (METEO_PTS - 3 - 2 * e) / 2;            // hauteur d'un bloc
  const int IH = hB - (jD + jB) - gJI - gIT - (tD + tB); // zone de l'icône
  // Chaque rangée est répartie d'après l'encre réelle de ses jours (nom, icône,
  // températures), avec des espaces égaux entre les jours et sur les bords : une
  // icône étroite (brouillard) à côté d'une large (nuages) ne décale plus la rangée.
  // Encre de chaque jour, relative à son centre cx : [gch, drt]
  int gch[5] = { 0 }, drt[5] = { 0 };
  for (uint8_t k = 0; k < nj; k++) {
    JsonObject j = js[k];
    int iw = 0;
    weatherIconScale(j["i"] | 2, BW, IH, &iw, nullptr);
    int g = -iw / 2, d = iw - iw / 2;                        // icône : centrée sur son encre
    int a, b;
    String jour = j["d"] | "";
    encreLargeur(jour, F_13, a, b);                          // nom : centré sur sa chasse
    int w0 = textW(jour, F_13);
    g = min(g, a - w0 / 2); d = max(d, b - w0 / 2);
    // min / max : même construction que minMaxCentre
    String mn = String((int)(j["mn"] | 0)) + "°", mx = String((int)(j["mx"] | 0)) + "°";
    int wa = textW(mn, ft), ws = textW("/", ft), wb = textW(mx, ft), wt = wa + ws + wb + 8;
    encreLargeur(mn, ft, a, b);
    g = min(g, a - wt / 2);
    encreLargeur(mx, ft, a, b);
    d = max(d, wa + ws + 8 + b - wt / 2);
    gch[k] = g; drt[k] = d;
  }
  int cxs[5] = { 0 };
  for (uint8_t r = 0; r < 2; r++) {
    const uint8_t k0 = r ? 2 : 0, k1 = min<uint8_t>(r ? 5 : 2, nj);
    if (k0 >= k1) continue;
    int somme = 0;
    for (uint8_t k = k0; k < k1; k++) somme += drt[k] - gch[k];
    const float g = (240.0f - somme) / (k1 - k0 + 1);
    float x = g;
    for (uint8_t k = k0; k < k1; k++) {
      cxs[k] = (int)lroundf(x - gch[k]);
      x += (drt[k] - gch[k]) + g;
    }
  }
  for (uint8_t k = 0; k < nj; k++) {
    JsonObject j = js[k];
    const int rang = k < 2 ? 0 : 1, cx = cxs[k];
    const int y = 3 + e + rang * (hB + e);                // haut de l'encre du bloc
    drawText(j["d"] | "", cx, y - (fontAscent(F_13) - jD), F_13, TXT2, BG, TC_DATUM);
    const int yI = y + jD + jB + gJI;
    weatherIconFit(j["i"] | 2, cx - BW / 2, yI, BW, IH, BG);
    minMaxCentre(j, cx, yI + IH + gIT - (fontAscent(ft) - tD), ft, BG);
  }

  staleMark(doc["p"] | 0);
  pageDots(dotIndex, dotCount, AMBRE);
}

// --------------------------------------------------------------- Discord

static const uint16_t AVATAR[] = { VIOLET, VERT, AMBRE, BLEU, CORAIL };

// Pastilles d'état vocal, dessinées à l'échelle : R = rayon extérieur de la
// pastille (anneau de fond, disque rouge, symbole blanc barré). u = R / 11 :
// à R = 11 (avatar de 60 px), le symbole fait environ 12 px.
enum EtatPastille : uint8_t { PASTILLE_MUET, PASTILLE_SOURD, PASTILLE_LIVE };

static void pastilleEtat(int bx, int by, int R, EtatPastille quoi) {
  const float u = R / 11.0f;
  const int rIn = R - max(2, (int)lroundf(1.8f * u));    // anneau de fond autour du disque rouge
  tft.fillSmoothCircle(bx, by, R, BG, BG);
  tft.fillSmoothCircle(bx, by, rIn, ROUGE, BG);
  if (quoi == PASTILLE_LIVE) {
    tft.fillSmoothCircle(bx, by, max(2, (int)lroundf(3.2f * u)), TXT, ROUGE);
    return;
  }
  const float ep = max(1.5f, 1.5f * u);                  // épaisseur des traits
  if (quoi == PASTILLE_MUET) {
    // Micro : capsule pleine, support en U, pied
    const float cw = 4.0f * u, ch = 6.6f * u, yc = by - 5.0f * u;
    tft.fillSmoothRoundRect((int)lroundf(bx - cw / 2), (int)lroundf(yc), max(3, (int)lroundf(cw)),
                            max(5, (int)lroundf(ch)), max(1, (int)lroundf(cw / 2)), TXT, ROUGE);
    const int ra = max(3, (int)lroundf(4.6f * u)), yU = (int)lroundf(by - 0.6f * u);
    const int ri = max(1, ra - max(1, (int)lroundf(ep)));
    tft.drawSmoothArc(bx, yU, ra, ri, 270, 360, TXT, ROUGE, true);   // 0° = bas : moitié basse
    tft.drawSmoothArc(bx, yU, ra, ri, 0, 90, TXT, ROUGE, true);
    tft.drawWideLine(bx, yU + ra - ep / 2, bx, by + 5.6f * u, ep, TXT, ROUGE);
  } else {
    // Casque : arceau (moitié haute) et deux écouteurs
    const int ra = max(4, (int)lroundf(5.4f * u)), yA = (int)lroundf(by + 0.8f * u);
    const int ri = max(1, ra - max(1, (int)lroundf(ep)));
    tft.drawSmoothArc(bx, yA, ra, ri, 90, 270, TXT, ROUGE, true);
    const int ew = max(3, (int)lroundf(3.0f * u)), eh = max(4, (int)lroundf(4.6f * u));
    tft.fillSmoothRoundRect(bx - ra, yA - 1, ew, eh, max(1, ew / 3), TXT, ROUGE);
    tft.fillSmoothRoundRect(bx + ra - ew + 1, yA - 1, ew, eh, max(1, ew / 3), TXT, ROUGE);
  }
  // Barre (haut gauche -> bas droite), séparée du symbole par un fin liseré rouge
  const float d = 5.6f * u;
  tft.drawWideLine(bx - d, by - d, bx + d, by + d, ep + 2.0f * max(1.0f, 0.9f * u), ROUGE, ROUGE);
  tft.drawWideLine(bx - d, by - d, bx + d, by + d, ep, TXT, ROUGE);
}

// Pseudo de 8 caractères au plus : au-delà, 7 caractères + « … » (UTF-8 respecté).
static String pseudoCourt(const char* s) {
  int car = 0, coupe = 0;
  for (int i = 0; s[i]; i++) {
    if ((s[i] & 0xC0) == 0x80) continue;               // octet de suite UTF-8
    if (car == 7) coupe = i;
    car++;
  }
  if (car <= 8) return String(s);
  return String(s).substring(0, coupe) + "…";
}

// Avatar rond de diamètre d centré en (cx, cy), avec l'état vocal en pastille
// en bas à droite et un anneau blurple pour une arrivée récente.
static void avatarRond(uint8_t i, int cx, int cy, int d) {
  const int r = d / 2;
  const char* nom = gDisc.names[i];
  if (!gwImage(gDisc.avatar[i], cx - r, cy - r, d, d, BG, r)) {
    uint16_t c = AVATAR[i % 5];
    tft.fillSmoothCircle(cx, cy, r, c, BG);
    String ini = firstChar(nom);
    bool grand = d >= 80 && ini.length() == 1 && isalnum((unsigned char)ini[0]);
    drawText(ini, cx, cy + 1, grand ? F_32 : F_18, SOMBRE, c, MC_DATUM);
  }
  if (gDisc.isNew[i]) tft.drawSmoothArc(cx, cy, r + 4, r + 2, 0, 360, BLURPLE, BG, false);
  uint8_t e = gDisc.etat[i];
  const int k = (int)lroundf(d * 0.354f);              // point du cercle à 45°, en bas à droite
  const int bx = cx + k, by = cy + k;
  // Pastille proportionnelle à l'avatar : rayon 12 px (avatar de 60 px) à 26 px (142 px)
  const int R = max(12, (int)lroundf(d * 0.18f));
  if (e & DISC_LIVE) pastilleEtat(bx, by, R, PASTILLE_LIVE);
  else if (e & DISC_SOURD) pastilleEtat(bx, by, R, PASTILLE_SOURD);
  else if (e & DISC_MUET) pastilleEtat(bx, by, R, PASTILLE_MUET);
}

// Zone des avatars : du bas de l'icône du serveur (y 47) jusqu'à 6 px au-dessus
// des points de pagination (y 225). Espaces égaux au-dessus et entre les lignes.
static const int DISC_HAUT = 47, DISC_BAS = 225 - 6;

void drawDiscord(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  if (!gDisc.ok) {
    errorPage("Discord", gDisc.err);
    pageDots(dotIndex, dotCount, BLURPLE);
    return;
  }

  // En-tête : icône du serveur (40 px) dans le coin haut gauche ; nom du salon
  // centré sur l'espace restant à droite de l'icône et sur la hauteur de l'icône.
  const int IC = 40, IX = 5, IY = 5, gIN = 8;
  const int xNom = IX + IC + gIN, wNom = 240 - IX - xNom;
  const char* salon = gDisc.canal[0] ? gDisc.canal : "Discord";
  const FontId fN = textW(salon, F_18) <= wNom ? F_18 : F_16;
  String nomSalon = fitText(salon, fN, wNom);
  if (!gwImage(gDisc.icone, IX, IY, IC, IC, BG, 11)) {
    card(IX, IY, IC, IC, 11, BLURPLE);
    headsetIcon(IX + IC / 2, IY + IC / 2, TXT, BLURPLE);
  }
  {
    GlyphBox gH;
    glyphBox(fN, 'H', gH);
    // Centré sur la hauteur des majuscules (les jambages éventuels dépassent dessous)
    int base = IY + IC / 2 + (gH.dy + 1) / 2;
    drawText(nomSalon, xNom + (wNom - textW(nomSalon, fN)) / 2, base - fontAscent(fN), fN, TXT, BG);
  }

  // Avatars : 6 au plus ; au-delà, 5 avatars et une pastille « +N ».
  const uint8_t recus = min<uint8_t>(gDisc.count, MAX_DISC_NAMES);
  uint8_t places = gDisc.n > 6 ? 5 : min<uint8_t>(recus, 6);
  if (places > recus) places = recus;
  const bool plus = gDisc.n > places;
  const uint8_t m = places + (plus ? 1 : 0);
  if (m == 0) {
    drawText("Personne en vocal", 120, (DISC_HAUT + DISC_BAS) / 2, F_16, MUTED, BG, MC_DATUM);
  } else {
    // Grille selon le nombre : 1 ; 2 colonnes ; 2 x 2 ; 3 colonnes x 2 lignes
    const int cols = m == 1 ? 1 : (m <= 4 ? 2 : 3);
    const int rows = (m + cols - 1) / cols;
    const int cellW = 212 / cols;
    // Police des pseudos : la plus grande de la série qui laisse tenir tous les pseudos
    static const FontId SERIE[4] = { F_18, F_16, F_13, F_11 };
    int iF = m == 1 ? 0 : (m == 2 ? 1 : 2);
    for (; iF < 3; iF++) {
      bool tient = true;
      for (uint8_t k = 0; k < places && tient; k++) tient = textW(pseudoCourt(gDisc.names[k]), SERIE[iF]) <= cellW - 6;
      if (tient) break;
    }
    const FontId fp = SERIE[iF];
    // Encre réelle des pseudos affichés (accents et jambages compris)
    int pDessus = 0, pDessous = 0;
    for (uint8_t k = 0; k < places; k++)
      encreTexte(fitText(pseudoCourt(gDisc.names[k]), fp, cellW - 4), fp, pDessus, pDessous);
    if (plus) encreTexte("autres", fp, pDessus, pDessous);
    const int hp = pDessus + pDessous;
    const int gp = m == 1 ? 10 : 7;                      // avatar -> pseudo
    const int H = DISC_BAS - DISC_HAUT;
    const int eMin = 6;
    // Largeur : 14 px au moins entre les avatars et sur les bords ; hauteur : tout
    // l'espace jusqu'à 6 px au-dessus des points, avec 6 px au moins entre les lignes.
    const int dL = cols == 1 ? 150 : (240 - (cols + 1) * 14) / cols;
    const int dH = (H - rows * (gp + hp) - rows * eMin) / rows;
    int d = min(dL, dH) & ~1;                            // diamètre pair : centre exact
    // Limité par la hauteur : espaces égaux au-dessus et entre les lignes, le bas
    // touche la zone. Limité par la largeur : bloc centré entre l'en-tête et les points.
    const bool parHauteur = dH <= dL;
    const int zone = parHauteur ? H : (225 - DISC_HAUT);
    const int libre = zone - rows * (d + gp + hp);
    const int parts = parHauteur ? rows : rows + 1;
    for (uint8_t k = 0; k < m; k++) {
      const int lig = k / cols, col = k % cols;
      const int dansLigne = min<int>(cols, m - lig * cols);   // dernière ligne incomplète : centrée
      // Espaces horizontaux égaux (bords compris) ; dernière ligne incomplète centrée
      const float pas = d + (240.0f - cols * d) / (cols + 1);
      const int cx = 120 + (int)lroundf((col - (dansLigne - 1) / 2.0f) * pas);
      const int top = DISC_HAUT + libre * (lig + 1) / parts + lig * (d + gp + hp);
      const int cy = top + d / 2;
      if (plus && k == m - 1) {
        tft.fillSmoothCircle(cx, cy, d / 2, CARD, BG);
        drawText("+" + String(gDisc.n - places), cx, cy + 1, d >= 80 ? F_32 : F_18, TXT, CARD, MC_DATUM);
        drawText("autres", cx, top + d + gp - (fontAscent(fp) - pDessus), fp, MUTED, BG, TC_DATUM);
        continue;
      }
      avatarRond(k, cx, cy, d);
      String nom = fitText(pseudoCourt(gDisc.names[k]), fp, cellW - 4);
      drawText(nom, cx, top + d + gp - (fontAscent(fp) - pDessus), fp, TXT, BG, TC_DATUM);
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


// Durée courte d'indisponibilité : « < 1 min », « 12 min », « 1 h 05 »,
// « 2 j 3 h » ; « > » devant quand le début est plus ancien que l'historique Kuma.
static String dureeCourte(uint32_t m, bool borne) {
  char b[16];
  if (m < 1) return borne ? String("> 1 min") : String("< 1 min");
  if (m < 60) snprintf(b, sizeof(b), "%lu min", (unsigned long)m);
  else if (m < 1440) snprintf(b, sizeof(b), "%lu h %02lu", (unsigned long)(m / 60), (unsigned long)(m % 60));
  else snprintf(b, sizeof(b), "%lu j %lu h", (unsigned long)(m / 1440), (unsigned long)(m % 1440 / 60));
  return (borne ? String("> ") : String("")) + b;
}

// Coche lissée (deux segments à bouts ronds), dessinée d'une seule forme
// d'après la distance au tracé : pas de défaut à la jonction.
static float distSeg(float px, float py, float ax, float ay, float bx, float by) {
  float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  float t = (vx * wx + vy * wy) / (vx * vx + vy * vy);
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  float dx = wx - t * vx, dy = wy - t * vy;
  return sqrtf(dx * dx + dy * dy);
}

static void coche(int cx, int cy, int taille, uint16_t fg, uint16_t bg) {
  const float k = taille / 40.0f, R = 3.4f * k;
  const float ax = cx - 15 * k, ay = cy + 1 * k, bx = cx - 5 * k, by = cy + 11 * k, ex = cx + 16 * k, ey = cy - 11 * k;
  for (int y = (int)(cy - 13 * k - R - 1); y <= (int)(cy + 12 * k + R + 1); y++) {
    for (int x = (int)(ax - R - 1); x <= (int)(ex + R + 1); x++) {
      float d = fminf(distSeg(x + 0.5f, y + 0.5f, ax, ay, bx, by), distSeg(x + 0.5f, y + 0.5f, bx, by, ex, ey));
      float a = R + 0.5f - d;
      if (a <= 0) continue;
      tft.drawPixel(x, y, a >= 1 ? fg : tft.alphaBlend((uint8_t)(a * 255), fg, bg));
    }
  }
}

// « Tout est OK » (page Supervision affichée à la demande sans aucun défaut) :
// grand disque vert avec coche au milieu, « TOUT EST OK » en haut, bulle
// « N services en ligne » en bas ; espaces égaux, l'icône prend le reste.
static void supervisionToutOk(uint8_t dotIndex, uint8_t dotCount) {
  GlyphBox gT;
  glyphBox(F_32, 'T', gT);
  const int hTitre = gT.h, hBulle = 34, e = 14;
  const int H = 225 - 6 - 3;                                // jusqu'à 6 px au-dessus des points
  const int hIcone = H - hTitre - hBulle - 4 * e;
  int y = 3 + e;
  drawText("TOUT EST OK", 120, y - (fontAscent(F_32) - gT.dy), F_32, TXT, BG, TC_DATUM);
  y += hTitre + e;
  const int r = hIcone / 2;
  tft.fillSmoothCircle(120, y + r, r, VERT, BG);
  coche(120, y + r, (int)(hIcone * 0.62f), SOMBRE, VERT);
  y += hIcone + e;
  String bilan = String(gSup.tot) + (gSup.tot > 1 ? " services en ligne" : " service en ligne");
  card(16, y, 208, hBulle, 12, CARD, BG);
  drawText(bilan, 120, y + hBulle / 2 + 1, F_16, TXT, CARD, MC_DATUM);
  staleMark(gSup.stale);
  pageDots(dotIndex, dotCount, VERT);
}

// Supervision : récapitulatif des services en défaut seulement (affichée juste
// après l'écran PANNE). En haut, le compteur « 1/8 KO » (services en défaut) ; dessous,
// une ligne par service en défaut (pannes d'abord), dont la hauteur s'adapte
// au nombre : de 1 à 6 lignes ; au-delà, 5 lignes et « + N autres ».
// Espaces égaux au-dessus, entre les éléments, 6 px au-dessus des points.
void drawSupervision(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  if (!gSup.ok) {
    errorPage("Supervision", gSup.err);
    pageDots(dotIndex, dotCount, VERT);
    return;
  }
  // Services en défaut (la passerelle les envoie en premier, pannes d'abord)
  uint8_t idx[MAX_SUP_ITEMS], nd = 0;
  for (uint8_t i = 0; i < gSup.count; i++)
    if (gSup.st[i] != SUP_OK) idx[nd++] = i;
  const int defauts = gSup.pa + gSup.in + gSup.ma;         // total réel (peut dépasser la liste)
  if (defauts == 0 || nd == 0) {                           // affichée à la demande, sans défaut
    supervisionToutOk(dotIndex, dotCount);
    return;
  }
  const int lignes = defauts > 6 ? 5 : min(nd, (uint8_t)6);
  const int autres = defauts - lignes;
  const bool ligneAutres = autres > 0;

  // Compteur « 1/8 KO » : services en défaut et « KO » en grand, dans la couleur
  // de la situation (rouge si panne, ambre si instable, bleu si maintenance)
  uint16_t cc = gSup.pa ? ROUGE : (gSup.in ? AMBRE : BLEU);
  String big = String(defauts), small = "/" + String(gSup.tot), ko = "KO";
  GlyphBox g52;
  glyphBox(F_52, '0', g52);
  const int hCompteur = g52.h;

  // Lignes : hauteur et police selon le nombre
  const int gapL = lignes <= 3 ? 8 : 6;
  GlyphBox gA;
  glyphBox(F_11, '0', gA);
  const int hAutres = ligneAutres ? gA.h : 0;
  const int ZONE_H = 225 - 6 - 3;
  const int eMin = 10;
  int hL = lignes ? (ZONE_H - hCompteur - hAutres - (lignes - 1) * gapL - (2 + (ligneAutres ? 1 : 0)) * eMin) / lignes : 0;
  hL = min(hL, 40);
  const FontId fe = hL >= 30 ? F_13 : F_11;
  // Police des noms : la plus grande permise par la hauteur des lignes, réduite
  // tant qu'un nom ne tient pas en entier (13 px au minimum, puis « … »)
  FontId fn = hL >= 34 ? F_18 : (hL >= 28 ? F_16 : F_13);
  {
    static const FontId SERIE[3] = { F_18, F_16, F_13 };
    int iF = fn == F_18 ? 0 : (fn == F_16 ? 1 : 2);
    int rdM = hL >= 34 ? 5 : 4;
    for (; iF < 2; iF++) {
      bool tient = true;
      for (int k = 0; k < lignes && tient; k++) {
        uint8_t st = gSup.st[idx[k]];
        int place = 226 - 12 - textW(dureeCourte(gSup.mins[idx[k]], gSup.borne[idx[k]]), fe) - 8 - (14 + 8 + 2 * rdM + 9);
        tient = textW(gSup.names[idx[k]], SERIE[iF]) <= place;
      }
      if (tient) break;
    }
    fn = SERIE[iF];
  }
  const int hListe = lignes * hL + (lignes - 1) * gapL + (ligneAutres ? gapL + hAutres : 0);
  // Peu de lignes (hauteur plafonnée) : compteur et liste groupés et centrés,
  // espaces égaux en haut, entre les deux et en bas ; sinon la liste descend
  // jusqu'à 6 px au-dessus des points.
  const int nEsp = hL >= 40 ? 3 : 2;
  const int e = min((ZONE_H - hCompteur - hListe) / nEsp, 24);
  const int decal = (ZONE_H - hCompteur - hListe - nEsp * e) / 2;   // reste centré

  int y = 3 + e + (nEsp == 3 ? decal : 0);
  {
    const int wB = textW(big, F_52), wS = textW(small, F_18), wK = textW(ko, F_52), e1 = 4, e2 = 12;
    int x = 120 - (wB + e1 + wS + e2 + wK) / 2;
    const int base = y + g52.dy;                           // ligne de base commune
    x += drawText(big, x, base - fontAscent(F_52), F_52, cc, BG) + e1;
    x += drawText(small, x, base - fontAscent(F_18), F_18, TXT2, BG) + e2;
    drawText(ko, x, base - fontAscent(F_52), F_52, cc, BG);
    y += hCompteur + e;
  }
  for (int k = 0; k < lignes; k++) {
    uint8_t i = idx[k];
    uint8_t st = gSup.st[i];
    uint16_t c = stateColor(st);
    int r = min(10, hL / 2);
    outlinedCard(14, y, 212, hL, r, c, CARD);
    int rd = hL >= 34 ? 5 : 4;
    tft.fillSmoothCircle(14 + 8 + rd + 4, y + hL / 2, rd, c, CARD);
    int xN = 14 + 8 + 2 * rd + 9;
    String lab = dureeCourte(gSup.mins[i], gSup.borne[i]);   // temps d'indisponibilité
    int wLab = textW(lab, fe);
    drawText(fitText(gSup.names[i], fn, 226 - 12 - wLab - 8 - xN), xN, y + hL / 2 + 1, fn, TXT, CARD, ML_DATUM);
    drawText(lab, 226 - 12, y + hL / 2 + 1, fe, st == SUP_PANNE ? ROUGE_TX : c, CARD, MR_DATUM);
    y += hL + gapL;
  }
  if (ligneAutres)
    drawText("+ " + String(autres) + (autres > 1 ? " autres services en défaut" : " autre service en défaut"),
             120, y - (fontAscent(F_11) - gA.dy), F_11, MUTED, BG, TC_DATUM);

  staleMark(gSup.stale);
  pageDots(dotIndex, dotCount, VERT);
}

// ---------------------------------------------------------------- Alerte

// Écran PANNE : « PANNE » en haut, grand triangle d'alerte au milieu, bulle
// « N services sur M en panne » en bas ; espaces égaux, l'icône prend le reste.
void drawAlert() {
  tft.fillScreen(ALERTE);
  GlyphBox gP;
  glyphBox(F_32, 'P', gP);
  const int hPanne = gP.h;
  const int hBulle = 38;
  const int e = 14;
  const int H = 240 - 3 - 3;                                // zone visible y 3..236
  const int hIcone = H - hPanne - hBulle - 4 * e;
  int y = 3 + e;
  drawText("PANNE", 120, y - (fontAscent(F_32) - gP.dy), F_32, TXT, ALERTE, TC_DATUM);
  y += hPanne + e;
  warningIconTaille(120, y + hIcone / 2, hIcone, TXT, ALERTE);
  y += hIcone + e;
  String bilan = String(gSup.alCount) + (gSup.alCount > 1 ? " services sur " : " service sur ")
               + String(gSup.tot) + " en panne";
  FontId fb = textW(bilan, F_16) <= 208 - 24 ? F_16 : F_13;
  card(16, y, 208, hBulle, 12, ALERTE2, ALERTE);
  drawText(bilan, 120, y + hBulle / 2 + 1, fb, TXT, ALERTE2, MC_DATUM);
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

// Couleur de la pastille de remplacement : fixe pour un même code
static uint16_t accentCode(const char* s) {
  static const uint16_t P[6] = { VIOLET, BLEU, CORAIL, XRP, ETH, AMBRE };
  uint32_t h = 0;
  while (*s) h = h * 31 + (uint8_t)*s++;
  return P[h % 6];
}

// Logo (via la passerelle) selon sa forme « f » :
//   0 logo rond (crypto, CoinGecko) ; 1 logo ou icône posé sur un disque gris
//   foncé (actions, matières premières) ; 2 image découpée en rond (drapeaux,
//   lingots). Sans image : pastille de couleur avec le code (4 caractères au
//   plus). Valeur introuvable (« e ») : disque gris avec « ? ».
static void logoCrypto(JsonObject c, int x, int y, int d, uint16_t fond) {
  const char* sym = c["s"] | "?";
  const int cx = x + d / 2, cy = y + d / 2;
  if (c["e"] | 0) {
    tft.fillSmoothCircle(cx, cy, d / 2, DIM, fond);
    drawText("?", cx, cy + 1, F_18, TXT, DIM, MC_DATUM);
    return;
  }
  const int forme = c["f"] | 0;
  if (forme == 1) {
    tft.fillSmoothCircle(cx, cy, d / 2, CARD, fond);
    const int di = (d * 64 / 100) & ~1;
    if (gwImage(c["i"] | "", cx - di / 2, cy - di / 2, di, di, CARD, 0, true)) return;
  } else if (gwImage(c["i"] | "", x, y, d, d, fond, d / 2, forme == 0)) {
    return;
  }
  uint16_t a = (strcmp(sym, "BTC") == 0 || strcmp(sym, "ETH") == 0 || strcmp(sym, "XRP") == 0)
               ? accentCrypto(sym) : accentCode(sym);
  tft.fillSmoothCircle(cx, cy, d / 2, a, fond);
  String t = String(sym).substring(0, 4);
  FontId f = F_11;
  for (FontId g : { F_18, F_16, F_13, F_11 }) { f = g; if (textW(t, g) <= d * 78 / 100) break; }
  drawText(t, cx, cy + 1, f, SOMBRE, a, MC_DATUM);
}

// Valeur sans cours : « Introuvable » (saisie inconnue) ou « Indisponible »
static const char* texteErreur(JsonObject c) { return (c["e"] | 0) == 2 ? "Indisponible" : "Introuvable"; }
static uint16_t couleurErreur(JsonObject c) { return (c["e"] | 0) == 2 ? AMBRE : ROUGE_TX; }
// Le code saisi s'écrit dans la police du prix, sauf en 26 px (chiffres seulement)
static FontId policeCode(FontId fp) { return fp == F_26 ? F_18 : fp; }

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
// basPrix : ce qui descend sous la ligne de base (virgule, « $ »).
static int hautPrix(JsonArray cs, uint8_t n, FontId f, int* basPrix = nullptr) {
  int h = 0, b = 0;
  for (uint8_t k = 0; k < n; k++) {
    const char* p = cs[k]["px"] | "";
    while (*p) {
      uint16_t u = (uint8_t)*p++;
      if ((u & 0xE0) == 0xC0 && *p) { u = ((u & 0x1F) << 6) | (*p++ & 0x3F); }
      else if ((u & 0xF0) == 0xE0 && p[0] && p[1]) { u = ((u & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
      GlyphBox g;
      if (u != ' ' && glyphBox(f, u, g)) { h = max(h, g.dy); b = max(b, g.h - g.dy); }
    }
  }
  if (basPrix) *basPrix = b;
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
static uint16_t accentPage(uint8_t page, JsonArray cs) {
  switch (page) {
    case PG_ACTIONS: return BLEU;
    case PG_DEVISES: return VERT;
    case PG_COMMO:   return AMBRE;
    default:         return accentCrypto(cs[0]["s"] | "");
  }
}

static void pageCrypto30(JsonObject c, uint8_t dotIndex, uint8_t dotCount, bool perime, uint16_t accent) {
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
  pageDots(dotIndex, dotCount, accent);
}

// Pages dédiées en attente (identifiants de la passerelle), affichées après
// l'écran « prix en vedette » par coursEcranSuivant().
static char    dediees[3][24];
static uint8_t nbDediees = 0, prochaineDediee = 0, pageDediees = PG_BTC;

static void noterDediee(JsonObject c) {
  if ((c["x"] | 0) && !(c["e"] | 0) && nbDediees < 3) {
    strncpy(dediees[nbDediees], c["id"] | "", sizeof(dediees[0]) - 1);
    dediees[nbDediees][sizeof(dediees[0]) - 1] = '\0';
    if (dediees[nbDediees][0]) nbDediees++;
  }
}

// « Prix en vedette » à 2 cryptos (place disponible : une moitié d'écran
// chacune). Par crypto :
//  - ligne 1 : logo de 56 px et prix (32, 26 ou 18 px selon la largeur),
//    centrés ensemble horizontalement, le prix centré sur la hauteur du logo ;
//  - ligne 2 : « 24 h … » et « 7 j … » centrés sur l'écran, dans la plus
//    grande police qui tient (16, 13 ou 11 px), colonnes communes aux deux.
// Les deux blocs et les trois espaces (haut, milieu, bas) sont égaux.
static void vedetteDeux(JsonArray cs, uint8_t dotIndex, uint8_t dotCount, bool perime, uint16_t accent) {
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
  // Espaces égaux au-dessus et entre les blocs ; 6 px au-dessus des points
  const int espace = (216 - 2 * hBloc) / 2, reste = (216 - 2 * hBloc) - 2 * espace;
  const int hp = hautPrix(cs, 2, fp);
  int wpMax = 0;
  for (uint8_t k = 0; k < 2; k++) wpMax = max<int>(wpMax, textW(cs[k]["px"] | "", fp));
  const int x0 = 120 - (L + gapH + min(wpMax, 212 - L - gapH)) / 2;   // logo + prix centrés
  const int x24 = 120 - (w24 + ecartVar + w7) / 2, x7 = x24 + w24 + ecartVar;

  tft.fillScreen(BG);
  for (uint8_t k = 0; k < 2; k++) {
    JsonObject c = cs[k];
    int haut = 3 + reste + espace + k * (hBloc + espace);
    logoCrypto(c, x0, haut, L, BG);
    // prix : encre (hp px au-dessus de la ligne de base) centrée sur le logo
    int base = haut + L / 2 + hp / 2;
    int yv = haut + L + gapLV + gv.h - fontAscent(fv);  // ligne de base des variations
    if (c["e"] | 0) {        // code saisi à la place du prix, erreur à la place des variations
      const FontId fc = policeCode(fp);
      drawText(fitText(c["s"] | "?", fc, 212 - L - gapH), x0 + L + gapH, base - fontAscent(fc), fc, TXT, BG);
      drawText(texteErreur(c), 120, yv, fv, couleurErreur(c), BG, TC_DATUM);
      continue;
    }
    drawText(fitText(c["px"] | "", fp, 212 - L - gapH), x0 + L + gapH, base - fontAscent(fp), fp, TXT, BG);
    groupeVariation(c, "24 h ", "v24", "u24", x24, yv, fv);
    groupeVariation(c, "7 j ", "v7", "u7", x7, yv, fv);
    noterDediee(c);
  }
  staleMark(perime);
  pageDots(dotIndex, dotCount, accent);
}

// Pages de cours (Crypto, Actions, Devises, Commodities), toutes en dollars :
//   1 valeur     -> page sur 30 jours
//   2 ou 3       -> « prix en vedette » (logo, prix, variations 24 h et 7 j),
//                   puis une page dédiée par valeur dont |7 j| >= 5 %
// Une valeur saisie inconnue reste à sa place : « ? », son code et
// « Introuvable » ; seule, elle donne une page d'erreur.
void drawCours(uint8_t page, uint8_t dotIndex, uint8_t dotCount) {
  nbDediees = prochaineDediee = 0;
  pageDediees = page;
  JsonDocument doc;
  int code = gwGet(modulePath(page), doc);
  if (code != 200 || doc["c"].as<JsonArray>().size() == 0) {
    tft.fillScreen(BG);
    errorPage(PAGE_NAMES[page], code == -5 ? String(doc["err"] | "")
                                : code == 200 ? String("Aucune valeur") : String(gwErrorText(code)));
    pageDots(dotIndex, dotCount, accentPage(page, doc["c"].as<JsonArray>()));
    return;
  }
  JsonArray cs = doc["c"].as<JsonArray>();
  uint8_t n = min<size_t>(cs.size(), 3);
  bool perime = doc["p"] | 0;
  const uint16_t accent = accentPage(page, cs);
  if (n <= 1) {
    if (cs[0]["e"] | 0) {
      tft.fillScreen(BG);
      errorPage(PAGE_NAMES[page], String(cs[0]["s"] | "?") + ((cs[0]["e"] | 0) == 2 ? " indisponible" : " introuvable"));
      pageDots(dotIndex, dotCount, accent);
      return;
    }
    pageCrypto30(cs[0], dotIndex, dotCount, perime, accent);
    return;
  }

  if (n == 2) {
    vedetteDeux(cs, dotIndex, dotCount, perime, accent);
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
  // gapPV : du bas de la virgule (ou du « $ ») au haut des variations
  const int gapH = 12, ecartVar = 12, gapPV = n == 2 ? 12 : 7;
  FontId fp = F_18, fv = F_11;
  int d = 0, wTexte = 0, w24 = 0, w7 = 0;
  bool trouve = false;
  for (int ip = 0; ip < 3 && !trouve; ip++) {
    for (int iv = (n == 2 ? 0 : 1); iv < 3 && !trouve; iv++) {
      GlyphBox gv;
      glyphBox(VAR[iv], '0', gv);
      int bp = 0;
      int dd = hautPrix(cs, n, PRIX[ip], &bp) + bp + gapPV + gv.h;
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
    int bp = 0;
    d = hautPrix(cs, n, F_18, &bp) + bp + gapPV + gv.h;
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
  // Espaces égaux au-dessus et entre les lignes ; la dernière descend jusqu'à
  // 6 px au-dessus des points de pagination (zone y 3..218).
  const int ZONE = 225 - 6 - 3;
  const int espaceV = (ZONE - n * (d + desc)) / n;
  const int reste = (ZONE - n * (d + desc)) - espaceV * n;

  tft.fillScreen(BG);
  for (uint8_t k = 0; k < n; k++) {
    JsonObject c = cs[k];
    int haut = 3 + reste + espaceV + k * (d + desc + espaceV);   // haut de l'encre de la ligne
    logoCrypto(c, x0, haut, d, BG);
    // Ligne de base du prix à haut + hp ; celle des variations au bas du logo
    int yv = haut + d - fontAscent(fv);
    if (c["e"] | 0) {
      const FontId fc = policeCode(fp);
      drawText(fitText(c["s"] | "?", fc, wTexte), xt, haut + hp - fontAscent(fc), fc, TXT, BG);
      drawText(texteErreur(c), xt, yv, fv, couleurErreur(c), BG);
      continue;
    }
    drawText(fitText(c["px"] | "", fp, wTexte), xt, haut + hp - fontAscent(fp), fp, TXT, BG);
    groupeVariation(c, "24 h ", "v24", "u24", xt, yv, fv);
    groupeVariation(c, "7 j ", "v7", "u7", xt + w24 + ecartVar, yv, fv);
    noterDediee(c);
  }
  staleMark(perime);
  pageDots(dotIndex, dotCount, accent);
}

bool coursEcranSuivant(uint8_t page, uint8_t dotIndex, uint8_t dotCount) {
  if (page != pageDediees) return false;
  while (prochaineDediee < nbDediees) {
    const char* id = dediees[prochaineDediee++];
    JsonDocument doc;
    if (gwGet(String("/") + PAGE_IDS[page] + "/30j?id=" + urlEncode(id), doc) != 200) {
      continue;                 // indisponible : page suivante
    }
    JsonArray cs = doc["c"].as<JsonArray>();
    pageCrypto30(cs[0], dotIndex, dotCount, doc["p"] | 0, accentPage(page, cs));
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

// Zones de progression de l'écran affiché : redessinées seules quand seule la
// progression change (sans retélécharger les affiches).
struct ZoneProgres {
  int x, y, w;          // barre
  int h;                // épaisseur de la barre
  int yT;               // ligne de texte des temps (-1 : pas de temps sous la barre)
  bool resteADroite;    // « reste … » à droite de la barre, sur sa ligne
};
static ZoneProgres zones[MAX_PLEX];
static uint8_t nZones = 0;

static void dessinerProgres(uint8_t i) {
  const ZoneProgres& z = zones[i];
  const PlexState::Ligne& l = gPlex.l[i];
  if (z.resteADroite) {
    int wr = textW(l.reste, F_11);
    tft.fillRect(z.x, z.y - 6, z.w + 1, z.h + 12, BG);
    plexBarre(z.x, z.y, z.w - wr - 8, z.h, l.pg, BG);
    drawText(l.reste, z.x + z.w, z.y + z.h / 2 + 1, F_11, MUTED, BG, MR_DATUM);
    return;
  }
  int bas = z.yT >= 0 ? z.yT + fontAscent(F_11) + 4 : z.y + z.h + 2;
  tft.fillRect(z.x, z.y - 2, z.w + 1, bas - (z.y - 2), BG);
  plexBarre(z.x, z.y, z.w, z.h, l.pg, BG);
  if (z.yT >= 0) {
    drawText(l.pos, z.x, z.yT, F_11, TXT, BG);
    drawText(l.reste, z.x + z.w, z.yT, F_11, MUTED, BG, TR_DATUM);
  }
}

void plexProgress() {
  if (!gPlex.ok || gPlex.count == 0) return;
  for (uint8_t i = 0; i < nZones && i < gPlex.count; i++) dessinerProgres(i);
}

// Affiche (ou remplaçant de même forme avec un triangle « lecture »)
static void plexAffiche(const PlexState::Ligne& l, int x, int y, int w, int h, int r) {
  if (gwImage(l.affiche, x, y, w, h, BG, r)) return;
  card(x, y, w, h, r, CARD);
  int cx = x + w / 2, cy = y + h / 2, t = max(5, w / 5);
  tft.fillTriangle(cx - t / 2, cy - t, cx - t / 2, cy + t, cx + t, cy, PLEX);
}

static const char* plexMode(const PlexState::Ligne& l) {
  return l.mode == 2 ? "Transcodage" : (l.mode == 1 ? "Flux direct" : "Direct");
}

// Haut et bas de l'encre d'une ligne de texte : pour aligner au pixel.
static int baseDepuisHaut(const String& t, FontId f, int hautEncre, int* basEncre = nullptr) {
  int d = 0, b = 0;
  encreTexte(t, f, d, b);
  GlyphBox g;
  glyphBox(f, 'H', g);
  d = max(d, (int)g.dy);
  if (basEncre) *basEncre = hautEncre + d + b;
  return hautEncre + d - fontAscent(f);                    // y de drawText (TL_DATUM)
}

// --- Une lecture --------------------------------------------------------------
// Affiche aussi grande que possible à gauche (proportions 2:3) ; à sa droite,
// qui regarde (en haut), puis la bibliothèque, l'épisode et le mode de lecture
// (alignés sur le bas de l'affiche). En bas, sur toute la largeur : le titre,
// la barre de progression, la position et le temps restant.
static void plexUne() {
  const PlexState::Ligne& l = gPlex.l[0];
  const int haut = 3 + 8, gAT = 9, gTB = 8, gBT = 6, hBarre = 6;
  int tD = 0, tB = 0;
  encreTexte(l.pos, F_11, tD, tB);
  encreTexte(l.reste, F_11, tD, tB);
  const int basTemps = 225 - 6;
  const int yT = basTemps - tB - fontAscent(F_11);
  const int yB = basTemps - tB - tD - gBT - hBarre;
  FontId ft = textW(l.titre, F_18) <= 212 ? F_18 : F_16;
  String titre = fitText(l.titre, ft, 212);
  int nD = 0, nB = 0;
  encreTexte(titre, ft, nD, nB);
  GlyphBox gH;
  glyphBox(ft, 'H', gH);
  nD = max(nD, (int)gH.dy);
  const int basTitre = yB - gTB, hautTitre = basTitre - nB - nD;
  drawText(titre, 14, basTitre - nB - fontAscent(ft), ft, TXT, BG);
  const int hA = hautTitre - gAT - haut, wA = hA * 2 / 3;
  plexAffiche(l, 14, haut, wA, hA, 10);
  const int xC = 14 + wA + 12, wC = 226 - xC;
  // Qui regarde : centré sur la colonne de droite, un peu sous le haut de l'affiche
  String u = fitText(l.user, F_16, wC);
  drawText(u, xC + (wC - textW(u, F_16)) / 2, baseDepuisHaut(u, F_16, haut + 14), F_16, PLEX, BG);
  const char* mode = plexMode(l);
  uint16_t cm = l.mode == 2 ? AMBRE : VERT;
  const FontId fP = (textW(mode, F_13) + 14 <= wC && textW("En pause", F_13) + 14 <= wC) ? F_13 : F_11;
  const int hP = fP == F_11 ? 19 : 22, gP = 6;
  int y = haut + hA;
  if (l.pause) { y -= hP; pill(xC, y, "En pause", fP, TXT, DIM, BG); y -= gP; }
  y -= hP;
  pill(xC, y, fitText(mode, fP, wC - 14), fP, SOMBRE, cm, BG);
  y -= 10;
  if (l.ep[0]) {
    int a = 0, b = 0;
    encreTexte(l.ep, F_13, a, b);
    drawText(l.ep, xC, y - b - fontAscent(F_13), F_13, MUTED, BG);
    y -= a + b + 7;
  }
  if (l.biblio[0]) {
    int a = 0, b = 0;
    String bib = fitText(l.biblio, F_16, wC);
    encreTexte(bib, F_16, a, b);
    drawText(bib, xC, y - b - fontAscent(F_16), F_16, TXT, BG);
  }
  zones[0] = { 14, yB, 212, hBarre, yT, false };
  nZones = 1;
}

// --- 2 ou 3 lectures (et plus) : une ligne par lecture -------------------------
// Hauteurs égales sur toute la hauteur utile, affiche à gauche.
//  - 2 lectures : qui regarde ; titre ; pastille d'état et épisode ;
//    en bas, barre, position et temps restant ;
//  - 3 lectures : qui regarde et pastille d'état ; titre ; barre et temps restant.
// Le haut du texte et le bas de la barre sont alignés sur l'affiche.
static FontId policeQuiTient(const char* t, FontId grande, FontId petite, int w) {
  return textW(t, grande) <= w ? grande : petite;
}

static void plexLignes(uint8_t n, int autres) {
  const int haut = 3 + 8, bas = 225 - 6 - (autres ? 16 : 0);
  const int g = n == 2 ? 12 : 9;
  const int hL = (bas - haut - (n - 1) * g) / n;
  const bool grand = n == 2;
  for (uint8_t i = 0; i < n; i++) {
    const PlexState::Ligne& l = gPlex.l[i];
    const int y0 = haut + i * (hL + g), yBas = y0 + hL;
    const int wA = hL * 2 / 3;
    plexAffiche(l, 14, y0, wA, hL, grand ? 8 : 6);
    const int xG = 14 + wA + 10;
    const int xD = 226, w = xD - xG;
    const char* t = l.pause ? "En pause" : plexMode(l);
    const uint16_t cP = l.pause ? DIM : (l.mode == 2 ? AMBRE : VERT), fP = l.pause ? TXT : SOMBRE;
    const int wPill = textW(t, F_11) + 14, hPill = 19;
    if (grand) {
      int basU = 0, basT = 0;
      String u = fitText(l.user, F_16, w);
      drawText(u, xG, baseDepuisHaut(u, F_16, y0, &basU), F_16, PLEX, BG);
      // Titre : 16 px sur une ligne, sinon 14 px sur une ligne, sinon 14 px sur deux
      FontId fT = policeQuiTient(l.titre, F_16, F_13, w);
      String t1 = l.titre, t2 = "";
      if (textW(t1, fT) > w) {
        int coupe = -1;
        for (int k = 0; k < (int)t1.length(); k++)
          if (t1[k] == ' ' && textW(t1.substring(0, k), fT) <= w) coupe = k;
        if (coupe > 0) { t2 = fitText(t1.substring(coupe + 1), fT, w); t1 = t1.substring(0, coupe); }
        else t1 = fitText(t1, fT, w);
      }
      drawText(t1, xG, baseDepuisHaut(t1, fT, basU + 9, &basT), fT, TXT, BG);
      if (t2.length()) drawText(t2, xG, baseDepuisHaut(t2, fT, basT + 5, &basT), fT, TXT, BG);
      // Pastille + épisode (« S2 · E5 », ou « S2E5 » si la place manque)
      const int yP = basT + (t2.length() ? 7 : 9);
      tft.fillSmoothRoundRect(xG, yP, wPill, hPill, hPill / 2 - 2, cP, BG);
      drawText(t, xG + wPill / 2, yP + hPill / 2 + 1, F_11, fP, cP, MC_DATUM);
      String info = l.ep;
      if (info.length()) {
        const int wI = w - wPill - 8;
        if (textW(info, F_13) > wI) {                    // « S2 · E5 » -> « S2E5 »
          char c[16];
          int j = 0;
          for (const char* q = l.ep; *q && j < 15; q++)
            if (*q != ' ' && (uint8_t)*q != 0xC2 && (uint8_t)*q != 0xB7) c[j++] = *q;
          c[j] = 0;
          info = c;
        }
        FontId fI = policeQuiTient(info.c_str(), F_13, F_11, wI);
        drawText(fitText(info, fI, wI), xG + wPill + 8, yP + hPill / 2 + 1, fI, MUTED, BG, ML_DATUM);
      }
      // Barre et temps alignés sur le bas de l'affiche
      int tD = 0, tB = 0;
      encreTexte(l.pos, F_11, tD, tB);
      encreTexte(l.reste, F_11, tD, tB);
      const int yT = yBas - tB - fontAscent(F_11);
      const int yB = yBas - tB - tD - 6 - 5;
      zones[i] = { xG, yB, w, 5, yT, false };
    } else {
      // Qui regarde (F_13) et pastille à droite, centrés sur la même ligne
      const int cy = y0 + hPill / 2;
      tft.fillSmoothRoundRect(xD - wPill, y0, wPill, hPill, hPill / 2 - 2, cP, BG);
      drawText(t, xD - wPill / 2, cy + 1, F_11, fP, cP, MC_DATUM);
      drawText(fitText(l.user, F_13, w - wPill - 8), xG, cy + 1, F_13, PLEX, BG, ML_DATUM);
      // Titre centré entre la ligne du haut et la barre
      FontId fT = policeQuiTient(l.titre, F_16, F_13, w);
      String ti = fitText(l.titre, fT, w);
      int d = 0, b = 0;
      encreTexte(ti, fT, d, b);
      GlyphBox gH;
      glyphBox(fT, 'H', gH);
      d = max(d, (int)gH.dy);
      const int yB = yBas - 5 - 3;                       // barre (le texte « reste » la déborde de 3 px)
      const int hautT = (y0 + hPill + yB - 3) / 2 - (d + b) / 2;
      drawText(ti, xG, hautT + d - fontAscent(fT), fT, TXT, BG);
      zones[i] = { xG, yB, w, 5, -1, true };
    }
  }
  nZones = n;
  if (autres)
    drawText("+ " + String(autres) + (autres > 1 ? " autres lectures" : " autre lecture"), 120,
             225 - 6 - 11, F_11, MUTED, BG, TC_DATUM);
}

void drawPlex(uint8_t dotIndex, uint8_t dotCount) {
  tft.fillScreen(BG);
  nZones = 0;
  if (!gPlex.ok || gPlex.count == 0) {
    errorPage("Plex", "Aucune lecture en cours");
    pageDots(dotIndex, dotCount, PLEX);
    return;
  }
  const uint8_t n = min<uint8_t>(gPlex.count, MAX_PLEX);
  const int autres = gPlex.n > n ? gPlex.n - n : 0;
  if (gPlex.n == 1 || n == 1) plexUne();
  else plexLignes(n, autres);
  plexProgress();
  staleMark(gPlex.stale);
  pageDots(dotIndex, dotCount, PLEX);
}
