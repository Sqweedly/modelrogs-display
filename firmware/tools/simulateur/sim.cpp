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
#include "persos.h"
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

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
  float x0 = x + r, x1 = x + w - 1 - r, y0 = y + r, y1 = y + h - 1 - r;   // sans lissage
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) {
    float dx = std::max({x0 - i, 0.f, i - x1}), dy = std::max({y0 - j, 0.f, j - y1});
    if (dx * dx + dy * dy <= (float)r * r) drawPixel(i, j, color);
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

std::string dernierChemin;   // 3.4.0 : dernier chemin demandé (test de « v=1 »)
std::vector<std::string> chemins;   // 3.5.0 : chemins demandés (enchaînement d'une catégorie)
int gwGet(const String& path, JsonDocument& doc, JsonDocument*, bool) {
  dernierChemin = path.s;
  chemins.push_back(path.s);
  if (gatewayDown) return gwLastCode = -3;
  // Réponse du plus long préfixe correspondant (« /btc/30j » avant « /btc »)
  const std::string* trouve = nullptr;
  size_t lg = 0;
  for (auto& [prefix, json] : fixtures) {
    if (path.s.rfind(prefix, 0) == 0 && prefix.size() > lg) { trouve = &json; lg = prefix.size(); }
  }
  if (!trouve) return gwLastCode = -3;
  if (deserializeJson(doc, *trouve)) return gwLastCode = -4;
  return gwLastCode = (doc["ok"] | 0) ? 200 : -5;
}

// 3.4.0 (memo.h) : texte brut toujours vide dans le simulateur, donc rien
// n'est gardé et chaque écran montre la donnée d'exemple du moment
// (comportement testé à part dans test_memo.cpp).
int gwGetBrut(const String& path, JsonDocument& doc, String& brut) {
  brut = String();
  return gwGet(path, doc, nullptr, false);
}

int gwRelire(JsonDocument& doc, const String& texte) {   // 3.4.0 (memo.h)
  return deserializeJson(doc, texte.c_str(), texte.length()) ? -4 : 200;
}

// Images : converties par genimg.py avec le code de la passerelle (images.py)
int imagesDemandees = 0, imagesAbsentes = 0;
bool gwImage(const char* jeton, int x, int y, int w, int h, uint16_t fond, int rayon, bool ajuster) {
  if (!jeton || !jeton[0] || gatewayDown) return false;
  imagesDemandees++;
  char cmd[512];
  unsigned r = (fond >> 11) << 3, g = ((fond >> 5) & 0x3F) << 2, b = (fond & 0x1F) << 3;
  r |= r >> 5; g |= g >> 6; b |= b >> 5;
  snprintf(cmd, sizeof(cmd), "%s genimg.py %s %d %d %02X%02X%02X %d %s /tmp/sim_img.raw",
           getenv("PYTHON") ? getenv("PYTHON") : "python3", jeton, w, h, r, g, b, rayon, ajuster ? "a" : "-");
  if (system(cmd) != 0) { imagesAbsentes++; return false; }
  std::ifstream f("/tmp/sim_img.raw", std::ios::binary);
  std::vector<uint16_t> ligne(w);
  tft.setSwapBytes(true);
  for (int j = 0; j < h; j++) {
    if (!f.read((char*)ligne.data(), w * 2)) { tft.setSwapBytes(false); return false; }
    tft.pushImage(x, y + j, w, 1, ligne.data());
  }
  tft.setSwapBytes(false);
  return true;
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

// 3.5.0 : une catégorie des marchés commence par sa mosaïque ; les écrans de
// cours existants sont ceux des « valeurs choisies », juste après (fixture
// « /marche » : aucune tuile, valeurs choisies). Personnalisée : comme avant.
static const char* MOSA_VIDE = R"({"ok":1,"t":[],"a":[],"x":[],"ch":1})";
static void coursAffiche(uint8_t page, uint8_t idx, uint8_t cnt) {
  drawCours(page, idx, cnt);
  if (pageMarche(page)) coursEcranSuivant(page, idx, cnt);
}

int main(int argc, char** argv) {
  std::string fx = argc > 1 ? argv[1] : "fixtures/";
  auto FX = [&](const char* n) { return readFile(fx + n + ".json"); };

  loadConfig();                    // valeurs par défaut (EEPROM vide)
  strcpy(cfg.ville, "Lyon");
  strcpy(cfg.gateway, "http://passerelle:8080");
  strcpy(cfg.pDiscord, "salon=42");
  uiBegin();

  fixtures = { {"/time", FX("time")}, {"/meteo", FX("meteo")}, {"/btc", FX("crypto1")},
               {"/discord", FX("discord")}, {"/supervision", FX("sup8")}, {"/plex", FX("plex1")},
               {"/btc/30j", FX("crypto30j")} };
  syncTime();
  g_millis += 5000;
  pollDiscord();
  pollSup();
  pollPlex();

  drawHeure(0, 5);                 save("01_heure");
  // Date la plus large (« 30 sept. ») à côté du jour le plus long (« MERCREDI »)
  fixtures["/time"] = R"({"ok":1,"e":1790762400,"o":7200,"j":"Mercredi","d":"30 septembre 2026","lv":"07:36","cc":"19:28"})";
  syncTime();
  drawHeure(0, 5);                 save("01b_heure_30_sept");
  fixtures["/time"] = FX("time");
  syncTime();
  drawMeteo(1, 5);                 save("02_meteo");
  drawDiscord(2, 5);               save("03_discord");
  drawSupervision(3, 5);           save("04_supervision_7");
  fixtures["/supervision"] = FX("sup12"); pollSup();
  drawSupervision(3, 5);           save("05_supervision_12");
  fixtures["/supervision"] = FX("sup15"); pollSup();
  drawSupervision(3, 5);           save("06_supervision_resume");
  fixtures["/supervision"] = FX("supalert"); pollSup();
  drawAlert();                     save("07_alerte");
  fixtures["/marche"] = MOSA_VIDE;
  coursAffiche(PG_BTC, 4, 5);      save("08_crypto_1");
  fixtures["/discord"] = FX("discord6"); pollDiscord();
  drawDiscord(2, 5);               save("09_discord_7");
  fixtures["/btc"] = FX("crypto2");
  coursAffiche(PG_BTC, 4, 6);      save("12_crypto_2");
  fixtures["/btc"] = FX("crypto3");
  coursAffiche(PG_BTC, 4, 6);      save("13_crypto_3");
  coursEcranSuivant(PG_BTC, 4, 6); save("13b_crypto_dediee");
  // Très petit prix (PEPE) : tailles fixes, prix compact « 0,0₅4540 $ »
  fixtures["/btc"] = FX("crypto3_pepe"); coursAffiche(PG_BTC, 4, 6); save("13c_crypto_3_pepe");
  fixtures["/btc"] = FX("crypto2_pepe"); coursAffiche(PG_BTC, 4, 6); save("13d_crypto_2_pepe");
  fixtures["/btc"] = FX("crypto30j_pepe"); coursAffiche(PG_BTC, 4, 6); save("13e_crypto_1_pepe");
  // Très petit prix (PEPE) : tailles fixes, prix compact « 0,0₅4540 $ »
  fixtures["/btc"] = FX("crypto3_pepe"); coursAffiche(PG_BTC, 4, 6); save("13c_crypto_3_pepe");
  fixtures["/btc"] = FX("crypto2_pepe"); coursAffiche(PG_BTC, 4, 6); save("13d_crypto_2_pepe");
  fixtures["/btc"] = FX("crypto30j_pepe"); coursAffiche(PG_BTC, 4, 6); save("13e_crypto_1_pepe");
  // Pages de cours (même rendu que Crypto ; dollars, indices en points)
  const char* const COURS[][3] = {
    {"/actions", "actions1", "20_actions_1"}, {"/actions", "actions2", "21_actions_2"},
    {"/actions", "actions3", "22_actions_3"}, {"/actions", "actions3e", "23_actions_introuvable"},
    {"/actions", "actions2e", "23b_actions_introuvable_2"}, {"/actions", "actions1e", "23c_actions_introuvable_seule"},
    {"/custom", "devises2", "24_devises_2"}, {"/custom", "devises3", "25_devises_3"},   // 3.5.0 : page Personnalisée
    {"/commodities", "commo1", "26_commodities_1"}, {"/commodities", "commo3", "27_commodities_3"},
    {"/commodities", "commo3b", "28_commodities_3b"},
    {"/indices", "indices1", "29_indices_1"}, {"/indices", "indices2", "29b_indices_2"},
    {"/indices", "indices3", "29c_indices_3"},
    {"/custom", "custom1", "30_custom_1"}, {"/custom", "custom2", "30b_custom_2"},
    {"/custom", "custom3", "30c_custom_3"}, {"/custom", "custom3b", "30d_custom_3b"},
  };
  for (auto& k : COURS) {
    fixtures[k[0]] = FX(k[1]);
    uint8_t page = strcmp(k[0], "/actions") == 0 ? PG_ACTIONS
                 : strcmp(k[0], "/indices") == 0 ? PG_INDICES : strcmp(k[0], "/custom") == 0 ? PG_CUSTOM : PG_COMMO;
    coursAffiche(page, 5, 9); save(k[2]);
  }
  // 3.5.0 : mosaïques des 5 catégories (liseré : page à 3 valeurs à suivre)
  const char* const MOSA[][3] = {
    {"/marche/btc", R"({"ok":1,"t":[["DOGE",-64,1],["SOL",52,1],["AVAX",41,1],["XRP",-31,1],["BTC",24,0],["LINK",-22,0],["ADA",-19,0],["LTC",14,0],["ETH",11,0],["DOT",-8,0],["BNB",6,0],["TRX",3,0]],"a":[],"x":[],"ch":0})", "40_mosaique_crypto"},
    {"/marche/actions", R"({"ok":1,"t":[["PLTR",56,1],["TSLA",-42,1],["NVDA",38,1],["AVGO",33,1],["META",21,0],["AAPL",12,0],["TSMC",-11,0],["AMZN",-9,0],["LLY",7,0],["JPM",-5,0],["MSFT",4,0],["ARMCO",2,0]],"a":[],"x":[],"ch":0})", "41_mosaique_monde"},
    {"/marche/europe", R"({"ok":1,"t":[["RHM",44,1],["NOVO",-36,1],["ASML",27,0],["SAN",16,0],["LVMH",-14,0],["SHELL",11,0],["SAP",9,0],["AIR",-8,0],["HSBC",-6,0],["SIE",5,0],["ROCHE",3,0],["NESN",-2,0]],"a":[],"x":[],"ch":0,"p":1})", "42_mosaique_europe_perimee"},
    {"/marche/indices", R"({"ok":1,"t":[["KOSPI",24,0],["HSI",-21,0],["NKY",18,0],["SENSX",9,0],["NDQ",-7,0],["DAX",6,0],["SX5E",5,0],["CAC",4,0],["S&P",-3,0],["FTSE",2,0],["DOW",1,0],["SMI",-1,0]],"a":[],"x":[],"ch":0})", "43_mosaique_indices"},
    {"/marche/commodities", R"({"ok":1,"t":[["GAZ",57,1],["CACAO",-48,1],["WTI",-34,1],["CAFE",32,1],["BRENT",-31,1],["AG",23,0],["CU",-12,0],["SUCRE",11,0],["BLE",-9,0],["OR",8,0],["PLAT",-6,0],["MAIS",4,0]],"a":[],"x":[],"ch":0})", "44_mosaique_matieres"},
    {"/marche/indices", R"({"ok":1,"t":[["CAC",4,0],["S&P",-3,0],["DAX",6,0]],"a":[],"x":[],"ch":0})", "45_mosaique_3_valeurs"},
  };
  for (auto& m : MOSA) {
    fixtures[m[0]] = m[1];
    uint8_t page = strstr(m[0], "btc") ? PG_BTC : strstr(m[0], "actions") ? PG_ACTIONS : strstr(m[0], "europe") ? PG_EUROPE
                 : strstr(m[0], "indices") ? PG_INDICES : PG_COMMO;
    drawCours(page, 5, 9); save(m[2]);
    fixtures.erase(m[0]);
  }
  // 3.5.0 : enchaînement d'une catégorie : mosaïque, valeurs choisies, pages
  // automatiques (une page d'une seule valeur = sur 30 jours, pas de page dédiée
  // en double), puis pages sur 30 jours, sans doublon
  {
    fixtures["/marche/actions"] = R"({"ok":1,"t":[["PLTR",56,1]],"a":[["PLTR","TSLA","NVDA"],["GC=F"]],"x":["PLTR","GC=F","AAPL"],"ch":1})";
    fixtures["/actions"] = FX("actions3");
    fixtures["/actions/30j"] = FX("actions1");
    std::string seule = FX("actions1");                     // page automatique d'une seule valeur
    seule.replace(seule.find("\"id\": \"AAPL\""), 12, "\"id\": \"GC=F\"");
    fixtures["/actions?s1=GC"] = seule;
    strncpy(cfg.pActions, "s1=AAPL", sizeof(cfg.pActions));
    reglerSeuils(6, 10);
    chemins.clear();
    drawCours(PG_ACTIONS, 5, 9);
    int pages = 0;
    while (coursEcranSuivant(PG_ACTIONS, 5, 9) && pages < 10) pages++;
    std::string vu;
    for (auto& c : chemins) vu += c + " | ";
    const std::vector<std::string> attendu = {
      "/marche/actions?s24=3&s7=5&s1=AAPL", "/actions?s1=AAPL&s7=5", "/actions?s1=PLTR&s2=TSLA&s3=NVDA&s7=5",
      "/actions?s1=GC=F&s7=5", "/actions/30j?id=PLTR", "/actions/30j?id=AAPL" };
    if (chemins != attendu || pages != 5) {
      fprintf(stderr, "ÉCHEC : enchaînement d'une catégorie (%d pages) : %s\n", pages, vu.c_str());
      return 1;
    }
    fprintf(stderr, "Enchaînement d'une catégorie : mosaïque, choisies, 2 pages automatiques, 2 pages sur 30 jours.\n");
    fixtures.erase("/marche/actions");
    fixtures.erase("/actions?s1=GC");
    fixtures["/actions"] = FX("actions3");
    cfg.pActions[0] = '\0';
  }
  // Photos Immich : en largeur, souvenir, en hauteur
  fixtures["/immich"] = FX("photos");          drawPhotos(6, 7); save("31_photos");
  fixtures["/immich"] = FX("photos_souvenir"); drawPhotos(6, 7); save("31b_photos_souvenir");
  fixtures["/immich"] = FX("photos_haute");    drawPhotos(6, 7); save("31c_photos_hauteur");
  // 3.4.0 (backlog n° 30) : souvenirs sans photo ce jour-là -> rien dessiné, false ;
  // « v=1 » envoyé à la passerelle
  {
    fixtures["/immich"] = R"({"ok":1,"v":1})";
    tft.fillScreen(0x1234);
    const bool dessine = drawPhotos(6, 7);
    bool intact = true;
    for (uint16_t c : tft.fb) intact = intact && c == 0x1234;
    if (dessine || !intact || dernierChemin.find("v=1") == std::string::npos) {
      fprintf(stderr, "ÉCHEC : Photos sans souvenir (dessine=%d, intact=%d, chemin=%s)\n", dessine, intact,
              dernierChemin.c_str());
      return 1;
    }
    fprintf(stderr, "Photos sans souvenir : page masquée, rien dessiné, v=1 envoyé.\n");
  }
  // Tesla : 4 états (branchée : point ambre, serveur MQTT perdu ; charge : ligne la plus longue)
  for (const char* e : {"route", "stationnee", "branchee", "charge"}) {
    fixtures["/tesla"] = FX((std::string("tesla_") + e).c_str()); pollTesla();
    drawTesla(); save(std::string("32_tesla_") + e);
  }
  // 3.5.0 : couleur de la batterie selon les seuils de Volta (rouge < 20 %, orange < 35 %)
  fixtures["/tesla"] = R"({"ok":1,"e":1,"b":12,"l":"Autonomie 58 km","i":"tesla-park","ev":0,"p":0})";
  pollTesla(); drawTesla(); save("32b_tesla_batterie_rouge");
  fixtures["/tesla"] = R"({"ok":1,"e":3,"b":30,"l":"Charge 11 kW · 1 h 40","i":"tesla-charge","ev":0,"p":0})";
  pollTesla(); drawTesla(); save("32c_tesla_batterie_orange");
  drawNuit(true);                  save("33_nuit");   // écran de nuit (horloge à aiguilles, 3.0.2)
  // Personnages (3.1.0) : Nimbus (ciel), Pousse (température), Volta (Tesla), Bip (panne)
  {
    struct { uint8_t i; int8_t t; const char* l; const char* f; } const CIEL[] = {
      {0, 14, "Ensoleillé", "34a_nimbus_beau"}, {1, 15, "Éclaircies", "34a2_nimbus_eclaircies"}, {3, 11, "Brouillard", "34b_nimbus_couvert"},
      {4, 9, "Averses", "34c_nimbus_pluie"}, {5, -1, "Neige", "34d_nimbus_neige"},
      {6, 17, "Orage et grêle", "34e_nimbus_orage"} };
    for (auto& c : CIEL) { gWx.ok = true; gWx.i = c.i; gWx.t = c.t; strcpy(gWx.l, c.l); drawNimbus(); save(c.f); }
    struct { int8_t t; const char* f; } const TEMP[] = {
      {2, "35a_pousse_glacial"}, {12, "35b_pousse_frais"}, {23, "35c_pousse_agreable"}, {35, "35d_pousse_canicule"} };
    for (auto& c : TEMP) { gWx.t = c.t; drawPousse(); save(c.f); }
    struct { uint8_t pct; uint8_t etat; const char* l; const char* f; } const BATT[] = {
      {12, TESLA_STATIONNEE, "Autonomie 41 km", "36a_volta_rouge"}, {32, TESLA_STATIONNEE, "Autonomie 112 km", "36b_volta_orange"},
      {80, TESLA_BRANCHEE, "Autonomie 281 km", "36c_volta_vert"}, {91, TESLA_STATIONNEE, "Autonomie 321 km", "36d_volta_contente"},
      {64, TESLA_CHARGE, "80 % dans 1 h 05 min", "36e_volta_charge"}, {15, TESLA_CHARGE, "80 % dans 6 h 40 min", "36f_volta_charge_rouge"} };
    for (auto& c : BATT) { gTesla.pct = c.pct; gTesla.etat = c.etat; strcpy(gTesla.ligne, c.l); drawVolta(); save(c.f); }
    gSup.pa = 1; drawBip(); save("37a_bip_1");
    gSup.pa = 3; drawBip(); save("37b_bip_3");
  }
  drawPlex(5, 6);                  save("14_plex_1");
  fixtures["/plex"] = FX("plex1s"); pollPlex();
  drawPlex(5, 6);                  save("14b_plex_serie");
  fixtures["/plex"] = FX("plex2"); pollPlex();
  drawPlex(5, 6);                  save("15a_plex_2");
  fixtures["/plex"] = FX("plex3"); pollPlex();
  drawPlex(5, 6);                  save("15_plex_3");
  fixtures["/discord"] = FX("discord11"); pollDiscord();
  drawDiscord(2, 6);               save("16_discord_11");
  gatewayDown = true;
  drawMeteo(1, 5);                 save("10_meteo_erreur");
  drawSplash("Configuration à valider", "1. Wi-Fi \"MODELROGS Display\"", "2. 192.168.4.1 > Enregistrer");
  save("11_a_valider");

  fprintf(stderr, "glyphes absents : %d, images : %d demandées, %d absentes (remplaçant dessiné)\n",
          tft.unknownGlyphs, imagesDemandees, imagesAbsentes);
  return tft.unknownGlyphs ? 1 : 0;
}
