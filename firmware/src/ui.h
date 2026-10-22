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
  constexpr uint16_t BG       = RGB(0x0E, 0x11, 0x16);
  constexpr uint16_t CARD     = RGB(0x1A, 0x1F, 0x29);
  constexpr uint16_t TXT      = RGB(0xF2, 0xF4, 0xF8);
  constexpr uint16_t TXT2     = RGB(0xC9, 0xCF, 0xDA);
  constexpr uint16_t MUTED    = RGB(0x9A, 0xA3, 0xB5);
  constexpr uint16_t MUTED2   = RGB(0x8A, 0x93, 0xA6);
  constexpr uint16_t DIM      = RGB(0x3A, 0x42, 0x52);
  constexpr uint16_t SEP      = RGB(0x5C, 0x65, 0x78);

  constexpr uint16_t VIOLET   = RGB(0x8B, 0x7C, 0xFF);   // page Heure
  constexpr uint16_t AMBRE    = RGB(0xFF, 0xB5, 0x47);   // page Météo / instable
  constexpr uint16_t BLURPLE  = RGB(0x58, 0x65, 0xF2);   // page Discord
  constexpr uint16_t VERT     = RGB(0x3D, 0xD6, 0x8C);   // page Supervision / OK
  constexpr uint16_t BTC      = RGB(0xF7, 0x93, 0x1A);   // page Bitcoin
  constexpr uint16_t ROUGE    = RGB(0xFF, 0x6B, 0x70);   // panne
  constexpr uint16_t ROUGE_TX = RGB(0xFF, 0x8A, 0x8E);
  constexpr uint16_t BLEU     = RGB(0x6C, 0xB6, 0xFF);   // maintenance / min
  constexpr uint16_t CORAIL   = RGB(0xFF, 0x7A, 0x59);

  constexpr uint16_t ALERTE   = RGB(0xC4, 0x33, 0x3A);
  constexpr uint16_t ALERTE2  = RGB(0x9E, 0x29, 0x30);

  constexpr uint16_t NUAGE    = RGB(0xE6, 0xEB, 0xF2);
  constexpr uint16_t NUAGE2   = RGB(0xC9, 0xD2, 0xDE);
  constexpr uint16_t NUAGE3   = RGB(0x9A, 0xA5, 0xB5);
  constexpr uint16_t PLUIE    = RGB(0x4D, 0xA3, 0xFF);
  constexpr uint16_t LUNE     = RGB(0xFF, 0xE0, 0x8A);
  constexpr uint16_t VERT_BG  = RGB(0x16, 0x3A, 0x2A);
  constexpr uint16_t ROUGE_BG = RGB(0x40, 0x1C, 0x20);
  constexpr uint16_t SOMBRE   = RGB(0x14, 0x14, 0x13);
}

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
