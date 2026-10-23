// Imitation de TFT_eSPI 2.5.43 dans un tampon 240x240 (RGB565) :
// mêmes signatures, même positionnement du texte lissé (VLW), même
// mélange anti-crénelage contre la couleur de fond passée en paramètre.
#pragma once
#include <Arduino.h>
#include <vector>

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define L_BASELINE 9
#define C_BASELINE 10
#define R_BASELINE 11

class TFT_eSPI {
 public:
  uint16_t fb[240 * 240];
  void init() {}
  void setRotation(int) {}
  void writecommand(uint8_t) {}
  void writedata(uint8_t) {}
  void setTextWrap(bool, bool) {}
  void fillScreen(uint32_t c) { for (auto& p : fb) p = c; }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { fillRect(x, y, 1, h, c); }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
  void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg = 0x00FFFFFF);
  void fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color, uint32_t bg = 0x00FFFFFF);
  void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg = 0x00FFFFFF);
  void drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t a0, uint32_t a1, uint32_t fg, uint32_t bg, bool roundEnds = false);
  uint16_t alphaBlend(uint8_t a, uint16_t fg, uint16_t bg);

  void loadFont(const uint8_t* array);
  void setTextColor(uint16_t fg, uint16_t bg) { tfg = fg; tbg = bg; }
  void setTextDatum(uint8_t d) { datum = d; }
  int16_t drawString(const String& s, int32_t x, int32_t y);
  int16_t textWidth(const String& s);
  int unknownGlyphs = 0;   // glyphes absents rencontrés (doivent rester à 0)

 private:
  void px(int x, int y, uint16_t c) { if (x >= 0 && x < 240 && y >= 0 && y < 240) fb[y * 240 + x] = c; }
  void cover(int x, int y, float a, uint16_t c, uint32_t bg);
  struct G { uint16_t u; uint8_t h, w, adv; int16_t dy; int8_t dx; uint32_t off; };
  std::vector<G> glyphs;
  const uint8_t* font = nullptr;
  int maxAscent = 0, maxDescent = 0, yAdvance = 0, spaceWidth = 0, ascent = 0, descent = 0;
  uint16_t tfg = 0xFFFF, tbg = 0;
  uint8_t datum = 0;
  const G* find(uint16_t u) const;
};
