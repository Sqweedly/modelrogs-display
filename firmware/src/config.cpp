#include "config.h"
#include <EEPROM.h>

Config cfg;

const char* const PAGE_NAMES[PG_COUNT] = { "Heure", "Météo", "Discord", "Supervision", "Bitcoin" };
const char* const PAGE_IDS[PG_COUNT]   = { "heure", "meteo", "discord", "supervision", "btc" };

// --- Anciennes structures, lues une seule fois lors de la mise à jour ------

struct ConfigV1 {            // firmware « MODELROGS Discord - Test 4 »
  uint32_t magic;
  uint16_t version;
  char wifiSsid[64];
  char wifiPassword[64];
  char apSsid[33];
  char apPassword[65];
  char discordUrl[160];
  char discordChannel[64];
  uint8_t screenBrightness;
};

struct ConfigV2 {            // MODELROGS Display 2.0.x
  uint32_t magic;
  uint16_t version;
  char wifiSsid[33];
  char wifiPass[65];
  char apSsid[33];
  char gateway[96];
  char ville[48];
  uint8_t  pages;
  uint8_t  pageSec;
  uint8_t  nightOn;
  uint16_t nightStart;
  uint16_t nightEnd;
  uint8_t  brightDay;
  uint8_t  brightNight;
  uint8_t  supCount;
  uint16_t supIds[48];
};

static void copyStr(char* dst, size_t size, const char* src) {
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

static void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  copyStr(cfg.apSsid, sizeof(cfg.apSsid), "MODELROGS Display");
  cfg.pages = (1 << PG_COUNT) - 1;
  cfg.pageSec = 10;
  cfg.nightStart = 23 * 60;
  cfg.nightEnd = 7 * 60;
  cfg.brightDay = 100;
  cfg.brightNight = 20;
  copyStr(cfg.pBtc, sizeof(cfg.pBtc), "devise=eur");
}

// http://192.168.1.44:5090/snapshot -> http://192.168.1.44:8080
static void gatewayFromOldUrl(const char* oldUrl) {
  String u = oldUrl;
  int start = u.indexOf("://");
  if (start < 0) return;
  start += 3;
  int end = start;
  while (end < (int)u.length() && u[end] != ':' && u[end] != '/') end++;
  String host = u.substring(start, end);
  if (host.length() == 0) return;
  String gw = "http://" + host + ":8080";
  copyStr(cfg.gateway, sizeof(cfg.gateway), gw.c_str());
}

static void terminate(char* s, size_t size) { s[size - 1] = '\0'; }

static void sanitize() {
  terminate(cfg.wifiSsid, sizeof(cfg.wifiSsid));
  terminate(cfg.wifiPass, sizeof(cfg.wifiPass));
  terminate(cfg.apSsid, sizeof(cfg.apSsid));
  terminate(cfg.gateway, sizeof(cfg.gateway));
  terminate(cfg.ville, sizeof(cfg.ville));
  for (uint8_t p = 0; p < PG_COUNT; p++) terminate(modParams(p), modParamsSize(p));
  if (cfg.apSsid[0] == '\0') copyStr(cfg.apSsid, sizeof(cfg.apSsid), "MODELROGS Display");
  cfg.pages &= (1 << PG_COUNT) - 1;
  cfg.pageSec = constrain(cfg.pageSec, 5, 120);
  cfg.nightStart %= 1440;
  cfg.nightEnd %= 1440;
  cfg.brightDay = constrain(cfg.brightDay, 5, 100);
  cfg.brightNight = min<uint8_t>(cfg.brightNight, 100);
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic;
  uint16_t version;
  EEPROM.get(0, magic);
  EEPROM.get(4, version);

  if (magic == CONFIG_MAGIC && version == CONFIG_VERSION) {
    EEPROM.get(0, cfg);
    sanitize();
    Serial.println(F("EEPROM : configuration v3 chargee."));
    return;
  }

  setDefaults();

  if (magic == CONFIG_MAGIC && version == 2) {
    ConfigV2 old;
    EEPROM.get(0, old);
    copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), old.wifiSsid);
    copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), old.wifiPass);
    copyStr(cfg.apSsid, sizeof(cfg.apSsid), old.apSsid);
    copyStr(cfg.gateway, sizeof(cfg.gateway), old.gateway);
    copyStr(cfg.ville, sizeof(cfg.ville), old.ville);
    cfg.pages = old.pages;
    cfg.pageSec = old.pageSec;
    cfg.nightOn = old.nightOn;
    cfg.nightStart = old.nightStart;
    cfg.nightEnd = old.nightEnd;
    cfg.brightDay = old.brightDay;
    cfg.brightNight = old.brightNight;
    String ids = "ids=";
    for (uint8_t i = 0; i < old.supCount && i < 48; i++) {
      if (i) ids += ',';
      ids += old.supIds[i];
    }
    if (old.supCount) copyStr(cfg.pSup, sizeof(cfg.pSup), ids.c_str());
    Serial.println(F("EEPROM : migration v2 -> v3 effectuee."));
  } else if (magic == CONFIG_MAGIC && version == 1) {
    // Firmware Discord d'origine : on garde Wi-Fi, point d'accès et luminosité.
    ConfigV1 old;
    EEPROM.get(0, old);
    old.wifiSsid[sizeof(old.wifiSsid) - 1] = '\0';
    old.wifiPassword[sizeof(old.wifiPassword) - 1] = '\0';
    old.apSsid[sizeof(old.apSsid) - 1] = '\0';
    old.discordUrl[sizeof(old.discordUrl) - 1] = '\0';
    copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), old.wifiSsid);
    copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), old.wifiPassword);
    if (old.apSsid[0]) copyStr(cfg.apSsid, sizeof(cfg.apSsid), old.apSsid);
    gatewayFromOldUrl(old.discordUrl);
    if (old.screenBrightness >= 5 && old.screenBrightness <= 100) cfg.brightDay = old.screenBrightness;
    Serial.println(F("EEPROM : migration v1 -> v3 effectuee (Wi-Fi conserve)."));
  } else {
    Serial.println(F("EEPROM : configuration absente -> valeurs par defaut."));
  }
  saveConfig();
}

void saveConfig() {
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  sanitize();
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println(F("EEPROM : configuration sauvegardee."));
}

bool pageEnabled(uint8_t page) {
  return page < PG_COUNT && (cfg.pages & (1 << page));
}

char* modParams(uint8_t page) {
  switch (page) {
    case PG_HEURE:   return cfg.pHeure;
    case PG_METEO:   return cfg.pMeteo;
    case PG_DISCORD: return cfg.pDiscord;
    case PG_SUP:     return cfg.pSup;
    default:         return cfg.pBtc;
  }
}

size_t modParamsSize(uint8_t page) {
  switch (page) {
    case PG_HEURE:   return sizeof(cfg.pHeure);
    case PG_METEO:   return sizeof(cfg.pMeteo);
    case PG_DISCORD: return sizeof(cfg.pDiscord);
    case PG_SUP:     return sizeof(cfg.pSup);
    default:         return sizeof(cfg.pBtc);
  }
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

String modParam(uint8_t page, const char* key) {
  const char* p = modParams(page);
  size_t klen = strlen(key);
  while (*p) {
    const char* end = strchr(p, '&');
    if (!end) end = p + strlen(p);
    if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
      String v;
      for (const char* c = p + klen + 1; c < end; c++) {
        if (*c == '%' && c + 2 < end && hexVal(c[1]) >= 0 && hexVal(c[2]) >= 0) {
          v += (char)(hexVal(c[1]) * 16 + hexVal(c[2]));
          c += 2;
        } else {
          v += (*c == '+') ? ' ' : *c;
        }
      }
      return v;
    }
    p = *end ? end + 1 : end;
  }
  return "";
}
