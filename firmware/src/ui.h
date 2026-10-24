#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// =====================================================
// Charte graphique (flat design, fond sombre)
// =====================================================

constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

namespace Col {
  // Couleurs saturées : la dalle du SmallTV affadit les teintes pastel.
  constexpr uint16_t BG       = RGB(0x00, 0x00, 0x00);   // noir pur : meilleur contraste
  constexpr uint16_t CARD     = RGB(0x1C, 0x21, 0x2C);
  constexpr uint16_t TXT      = RGB(0xFF, 0xFF, 0xFF);
  constexpr uint16_t TXT2     = RGB(0xE4, 0xE8, 0xEF);
  constexpr uint16_t MUTED    = RGB(0xB4, 0xBC, 0xCB);   // textes secondaires, lisibles sur noir
  constexpr uint16_t MUTED2   = RGB(0xA0, 0xA8, 0xB8);
  constexpr uint16_t DIM      = RGB(0x40, 0x48, 0x58);
  constexpr uint16_t SEP      = RGB(0x70, 0x78, 0x8A);

  constexpr uint16_t VIOLET   = RGB(0x7C, 0x4D, 0xFF);   // page Heure
  constexpr uint16_t AMBRE    = RGB(0xFF, 0xA0, 0x00);   // page Météo / instable
  constexpr uint16_t BLURPLE  = RGB(0x58, 0x65, 0xF2);   // page Discord (couleur Discord)
  constexpr uint16_t PLEX     = RGB(0xFF, 0xC4, 0x00);   // page Plex
  constexpr uint16_t ETH      = RGB(0x7B, 0x8C, 0xFF);   // accent Ethereum
  constexpr uint16_t XRP      = RGB(0x00, 0xC2, 0xE8);   // accent XRP
  constexpr uint16_t VERT     = RGB(0x00, 0xE0, 0x6A);   // page Supervision / OK
  constexpr uint16_t BTC      = RGB(0xFF, 0x8C, 0x00);   // page Bitcoin
  constexpr uint16_t ROUGE    = RGB(0xFF, 0x1F, 0x2E);   // panne
  constexpr uint16_t ROUGE_TX = RGB(0xFF, 0x4D, 0x58);
  constexpr uint16_t BLEU     = RGB(0x1E, 0x90, 0xFF);   // maintenance / min
  constexpr uint16_t CORAIL   = RGB(0xFF, 0x55, 0x2E);

  constexpr uint16_t ALERTE   = RGB(0xA8, 0x00, 0x10);   // rouge profond
  constexpr uint16_t ALERTE2  = RGB(0x6E, 0x00, 0x0A);

  constexpr uint16_t NUAGE    = RGB(0xF0, 0xF4, 0xFA);
  constexpr uint16_t NUAGE2   = RGB(0xC8, 0xD2, 0xE0);
  constexpr uint16_t NUAGE3   = RGB(0x8C, 0x99, 0xAC);
  constexpr uint16_t PLUIE    = RGB(0x1E, 0x8C, 0xFF);
  constexpr uint16_t LUNE     = RGB(0xFF, 0xD8, 0x4A);
  constexpr uint16_t VERT_BG  = RGB(0x00, 0x42, 0x1E);
  constexpr uint16_t ROUGE_BG = RGB(0x4E, 0x00, 0x08);
  constexpr uint16_t SOMBRE   = RGB(0x00, 0x00, 0x00);
}

// Marges : le boîtier masque 3 pixels en haut et en bas de l'écran.
constexpr int MARGE_HAUT = 3;
constexpr int MARGE_BAS  = 3;

extern TFT_eSPI tft;

// Polices lissées (Inter) — voir fonts/inter_fonts.h
enum FontId : uint8_t { F_11, F_13, F_16, F_18, F_32, F_52, F_68 };

void uiBegin();
void useFont(FontId f);

// Texte : datum TFT_eSPI (TL_DATUM, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM,
// L_BASELINE...). Renvoie la largeur dessinée.
int drawText(const String& s, int x, int y, FontId f, uint16_t fg, uint16_t bg,
             uint8_t datum = TL_DATUM);
int textW(const String& s, FontId f);

// Dimensions d'un glyphe, lues dans la police (format VLW) :
// dx = décalage de l'encre depuis la plume, dy = hauteur de l'encre au-dessus
// de la ligne de base, w/h = taille de l'encre, adv = avance de la plume.
struct GlyphBox { int dx, dy, w, h, adv; };
bool glyphBox(FontId f, uint16_t code, GlyphBox& g);
// Hauteur au-dessus de la ligne de base utilisée par TFT_eSPI pour placer le
// texte (TL_DATUM : ligne de base = y + fontAscent).
int fontAscent(FontId f);
// Tronque avec « … » pour tenir dans maxW pixels.
String fitText(const String& s, FontId f, int maxW);

void card(int x, int y, int w, int h, int r, uint16_t color, uint16_t bg = Col::BG);
void outlinedCard(int x, int y, int w, int h, int r, uint16_t border, uint16_t fill,
                  uint16_t bg = Col::BG);
void pill(int x, int y, const String& s, FontId f, uint16_t fg, uint16_t fill,
          uint16_t bg, bool alignRight = false);

void pageDots(uint8_t active, uint8_t count, uint16_t color);
void staleMark(bool stale, uint16_t bg = Col::BG);

// Icônes flat dessinées par le code (aucun bitmap en flash).
// Codes météo identiques à gateway/app/sources/meteo.py
void weatherIcon(uint8_t code, int cx, int cy, float scale, uint16_t bg);
void halfSun(int cx, int cy, uint16_t color, uint16_t bg);
void headsetIcon(int cx, int cy, uint16_t color, uint16_t bg);
void warningIcon(int cx, int cy, uint16_t color, uint16_t bg);
