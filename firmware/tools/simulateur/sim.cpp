// Simulateur : exécute le vrai code de rendu (pages.cpp, ui.cpp, app.cpp)
// Lancement : ./apercu.sh
// et enregistre chaque écran en image PPM.
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

#include "TFT_eSPI.h"
#include "app.h"
#include "config.h"
#include "net.h"
#include "pages.h"
#include "ui.h"

uint32_t g_millis = 1000;

// ------------------------------------------------------------- TFT_eSPI

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

uint16_t TFT_eSPI::alphaBlend(uint8_t a, uint16_t fg, uint16_t bg) {
  uint16_t fr = fg >> 11, fgg = (fg >> 5) & 63, fb_ = fg & 31;
  uint16_t br = bg >> 11, bgg = (bg >> 5) & 63, bb = bg & 31;
  uint16_t r = (fr * a + br * (255 - a)) / 255;
  uint16_t g = (fgg * a + bgg * (255 - a)) / 255;
  uint16_t b = (fb_ * a + bb * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}

void TFT_eSPI::cover(int x, int y, float a, uint16_t c, uint32_t bg) {
  if (a <= 0) return;
  if (a >= 1) { px(x, y, c); return; }
  uint16_t base = (bg == 0x00FFFFFF) ? fb[std::clamp(y, 0, 239) * 240 + std::clamp(x, 0, 239)] : (uint16_t)bg;
  px(x, y, alphaBlend((uint8_t)(a * 255), c, base));
}

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, c);
}

void TFT_eSPI::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
  for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) px(x + i, y + j, c);
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
  int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
  int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
  auto e = [](float ax, float ay, float bx, float by, float px_, float py_) { return (bx - ax) * (py_ - ay) - (by - ay) * (px_ - ax); };
  for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
    float a = e(x0, y0, x1, y1, x, y), b = e(x1, y1, x2, y2, x, y), d = e(x2, y2, x0, y0, x, y);
    if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) px(x, y, c);
  }
}

void TFT_eSPI::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg) {
  for (int j = -r - 1; j <= r + 1; j++) for (int i = -r - 1; i <= r + 1; i++)
    cover(x + i, y + j, clamp01(r + 0.5f - std::sqrt((float)(i * i + j * j))), color, bg);
}

void TFT_eSPI::fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color, uint32_t bg) {
  float x0 = x + r, x1 = x + w - 1 - r, y0 = y + r, y1 = y + h - 1 - r;
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) {
    float dx = std::max({x0 - i, 0.f, i - x1}), dy = std::max({y0 - j, 0.f, j - y1});
    cover(i, j, clamp01(r + 0.5f - std::sqrt(dx * dx + dy * dy)), color, bg);
  }
}

void TFT_eSPI::drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg) {
  float r = wd / 2;
  int minx = (int)std::floor(std::min(ax, bx) - r - 1), maxx = (int)std::ceil(std::max(ax, bx) + r + 1);
  int miny = (int)std::floor(std::min(ay, by) - r - 1), maxy = (int)std::ceil(std::max(ay, by) + r + 1);
  float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
  for (int j = miny; j <= maxy; j++) for (int i = minx; i <= maxx; i++) {
    float t = l2 > 0 ? clamp01(((i - ax) * vx + (j - ay) * vy) / l2) : 0;
    float dx = i - (ax + t * vx), dy = j - (ay + t * vy);
    cover(i, j, clamp01(r + 0.5f - std::sqrt(dx * dx + dy * dy)), fg, bg);
  }
}

void TFT_eSPI::drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t a0, uint32_t a1, uint32_t fg, uint32_t bg, bool roundEnds) {
  for (int j = -r - 1; j <= r + 1; j++) for (int i = -r - 1; i <= r + 1; i++) {
    float d = std::sqrt((float)(i * i + j * j));
    float ang = std::atan2((float)-i, (float)j) * 180.f / (float)M_PI;
    if (ang < 0) ang += 360;
    if (ang < a0 || ang > a1) continue;
    cover(x + i, y + j, clamp01(r + 0.5f - d) * clamp01(d - ir + 0.5f), fg, bg);
  }
  if (roundEnds) {
    float rm = (r + ir) / 2.f, rc = (r - ir) / 2.f;
    for (uint32_t a : {a0, a1}) {
      float rad = a * (float)M_PI / 180.f;
      float cx = x - std::sin(rad) * rm, cy = y + std::cos(rad) * rm;
      drawWideLine(cx, cy, cx, cy, rc * 2, fg, bg);
    }
  }
}

static int32_t rd32(const uint8_t* p) { return (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

void TFT_eSPI::loadFont(const uint8_t* a) {
  font = a;
  int n = rd32(a);
  ascent = rd32(a + 16);
  descent = rd32(a + 20);
  glyphs.clear();
  maxAscent = maxDescent = 0;
  uint32_t off = 24 + n * 28;
  for (int i = 0; i < n; i++) {
    const uint8_t* g = a + 24 + i * 28;
    G gl{ (uint16_t)rd32(g), (uint8_t)rd32(g + 4), (uint8_t)rd32(g + 8), (uint8_t)rd32(g + 12),
          (int16_t)rd32(g + 16), (int8_t)rd32(g + 20), off };
    off += gl.w * gl.h;
    if ((gl.u > 0x20 && gl.u < 0x7F) || gl.u > 0xA0) {
      maxAscent = std::max<int>(maxAscent, gl.dy);
      maxDescent = std::max<int>(maxDescent, gl.h - gl.dy);
    }
    glyphs.push_back(gl);
  }
  yAdvance = maxAscent + maxDescent;
  spaceWidth = (ascent + descent) * 2 / 7;
}

const TFT_eSPI::G* TFT_eSPI::find(uint16_t u) const {
  for (auto& g : glyphs) if (g.u == u) return &g;
  return nullptr;
}

static std::vector<uint16_t> utf8(const char* s) {
  std::vector<uint16_t> out;
  const uint8_t* p = (const uint8_t*)s;
  while (*p) {
    uint16_t c = *p++;
    if ((c & 0xE0) == 0xC0) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    else if ((c & 0xF0) == 0xE0) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
    out.push_back(c);
  }
  return out;
}

int16_t TFT_eSPI::textWidth(const String& s) {
  int w = 0;
  auto cs = utf8(s.c_str());
  for (size_t k = 0; k < cs.size(); k++) {
    if (cs[k] == 0x20) { w += spaceWidth; continue; }
    const G* g = find(cs[k]);
    if (!g) { w += spaceWidth + 1; continue; }
    if (w == 0 && g->dx < 0) w -= g->dx;
    w += (k + 1 < cs.size() || (cs[k] >= '0' && cs[k] <= '9')) ? g->adv : g->dx + g->w;
  }
  return w;
}

int16_t TFT_eSPI::drawString(const String& s, int32_t x, int32_t y) {
  int cw = textWidth(s), ch = yAdvance;
  switch (datum) {
    case TC_DATUM: x -= cw / 2; break;
    case TR_DATUM: x -= cw; break;
    case ML_DATUM: y -= ch / 2; break;
    case MC_DATUM: x -= cw / 2; y -= ch / 2; break;
    case MR_DATUM: x -= cw; y -= ch / 2; break;
    case L_BASELINE: y -= maxAscent; break;
    case C_BASELINE: x -= cw / 2; y -= maxAscent; break;
    case R_BASELINE: x -= cw; y -= maxAscent; break;
  }
  int cx = x;
  for (uint16_t c : utf8(s.c_str())) {
    if (c == 0x20) { cx += spaceWidth; continue; }
    const G* g = find(c);
    if (!g) { unknownGlyphs++; fprintf(stderr, "  ! glyphe absent U+%04X dans \"%s\"\n", c, s.c_str()); cx += spaceWidth + 1; continue; }
    if (cx == 0) cx -= g->dx;
    int gy = y + maxAscent - g->dy, gx = cx + g->dx;
    for (int j = 0; j < g->h; j++) for (int i = 0; i < g->w; i++) {
      uint8_t a = font[g->off + j * g->w + i];
      if (a == 255) px(gx + i, gy + j, tfg);
      else if (a) px(gx + i, gy + j, alphaBlend(a, tfg, tbg));
    }
    cx += g->adv;
  }
  return cw;
}

// ------------------------------------------------- réseau et configuration

int gwLastCode = 0;
uint32_t gwLastOkMs = 0;
std::map<std::string, std::string> fixtures;   // préfixe de chemin -> JSON
bool gatewayDown = false;

String urlEncode(const char* s) { return String(s); }
const char* gwErrorText(int code) { return code == -3 ? "Passerelle injoignable" : "Erreur"; }

int gwGet(const String& path, JsonDocument& doc, JsonDocument*) {
  if (gatewayDown) return gwLastCode = -3;
  for (auto& [prefix, json] : fixtures) {
    if (path.s.rfind(prefix, 0) == 0) {
      if (deserializeJson(doc, json)) return gwLastCode = -4;
      return gwLastCode = (doc["ok"] | 0) ? 200 : -5;
    }
  }
  return gwLastCode = -3;
}

static std::string readFile(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void save(const std::string& name) {
  std::ofstream f("out/" + name + ".ppm", std::ios::binary);
  f << "P6\n240 240\n255\n";
  for (uint16_t c : tft.fb) {
    unsigned char rgb[3] = { (unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                             (unsigned char)((c & 31) * 255 / 31) };
    f.write((char*)rgb, 3);
  }
  fprintf(stderr, "%s\n", name.c_str());
}

int main(int argc, char** argv) {
  std::string fx = argc > 1 ? argv[1] : "fixtures/";
  auto FX = [&](const char* n) { return readFile(fx + n + ".json"); };

  loadConfig();                    // valeurs par défaut (EEPROM vide)
  strcpy(cfg.ville, "Lyon");
  strcpy(cfg.gateway, "http://passerelle:8080");
  strcpy(cfg.pSup, "ids=1,2,3,4,5,6,7,8");
  strcpy(cfg.pDiscord, "salon=42");
  uiBegin();

  fixtures = { {"/time", FX("time")}, {"/meteo", FX("meteo")}, {"/btc", FX("btc")},
               {"/discord", FX("discord")}, {"/supervision", FX("sup8")} };
  syncTime();
  g_millis += 5000;
  pollDiscord();
  pollSup();

  drawHeure(0, 5);                 save("01_heure");
  drawMeteo(1, 5);                 save("02_meteo");
  drawDiscord(2, 5);               save("03_discord");
  drawSupervision(3, 5);           save("04_supervision_7");
  fixtures["/supervision"] = FX("sup12"); pollSup();
  drawSupervision(3, 5);           save("05_supervision_12");
  fixtures["/supervision"] = FX("sup15"); pollSup();
  drawSupervision(3, 5);           save("06_supervision_resume");
  fixtures["/supervision"] = FX("supalert"); pollSup();
  drawAlert();                     save("07_alerte");
  drawBtc(4, 5);                   save("08_btc");
  fixtures["/discord"] = FX("discord6"); pollDiscord();
  drawDiscord(2, 5);               save("09_discord_6");
  gatewayDown = true;
  drawMeteo(1, 5);                 save("10_meteo_erreur");
  drawSplash("Configuration à valider", "1. Wi-Fi \"MODELROGS Display\"", "2. 192.168.4.1 > Enregistrer");
  save("11_a_valider");

  fprintf(stderr, "glyphes absents : %d\n", tft.unknownGlyphs);
  return tft.unknownGlyphs ? 1 : 0;
}
