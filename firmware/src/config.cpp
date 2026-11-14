#include "config.h"
#include <EEPROM.h>

Config cfg;
static_assert(sizeof(Config) <= EEPROM_SIZE, "Config trop grande pour l'EEPROM");
static_assert(PG_COUNT <= 32, "pages : masque de 32 bits (cfg.pages + reserve[2], reserve[3])");

const char* const PAGE_NAMES[PG_COUNT] = { "Heure", "Météo", "Discord", "Supervision", "Crypto", "Plex",
                                           "Actions Monde", "Actions Europe", "Matières premières", "Indices",
                                           "Personnalisée", "Photos", "Tesla" };
const char* const PAGE_IDS[PG_COUNT]   = { "heure", "meteo", "discord", "supervision", "btc", "plex",
                                           "actions", "europe", "commodities", "indices", "custom", "immich",
                                           "tesla" };
const uint8_t ROTATION[PG_COUNT] = { PG_HEURE, PG_METEO, PG_DISCORD, PG_SUP, PG_BTC, PG_ACTIONS,
                                     PG_EUROPE, PG_INDICES, PG_COMMO, PG_CUSTOM, PG_PHOTOS, PG_TESLA, PG_PLEX };
// Pages ajoutées en 2.9 : activées à la mise à jour (affichées seulement si
// le module est actif sur la passerelle)
static const uint16_t PAGES_29 = (1 << PG_ACTIONS) | (1 << PG_EUROPE) | (1 << PG_COMMO);   // PG_EUROPE : ancienne page Devises
// Page ajoutée en 2.10, activée de la même façon
static const uint16_t PAGES_210 = 1 << PG_INDICES;
static const uint16_t PAGES_211 = 1 << PG_CUSTOM;
static const uint16_t PAGES_212 = 1 << PG_PHOTOS;
// 2.15 : page Tesla, sans réglage ni changement de structure. Activée une seule
// fois (repère dans reserve[0]) : une fois décochée par l'utilisateur, elle le reste.
static const uint8_t RES_PAGE_TESLA = 0x01;
// 3.5.0 : reprise des réglages des marchés faite (reserve[4]) ; seuils par défaut
static const uint8_t RES_MARCHES = 0x01;
static const uint8_t SEUIL24_DEFAUT = 6, SEUIL7_DEFAUT = 10;   // 3 % et 5 %
static const uint8_t SEUIL_MAX = 40;                            // 20 %

bool pageCours(uint8_t page) {
  return pageMarche(page) || page == PG_CUSTOM;
}

bool pageMarche(uint8_t page) {
  return page == PG_BTC || page == PG_ACTIONS || page == PG_EUROPE || page == PG_COMMO || page == PG_INDICES;
}

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

struct ConfigV4 {            // MODELROGS Display 2.2 à 2.8
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
  char pHeure[24];
  char pMeteo[24];
  char pDiscord[40];
  char pSup[300];
  char pBtc[24];         // réglages Crypto jusqu'en 2.5
  uint8_t validated;
  char    pCrypto[52];   // réglages Crypto 2.6 à 2.8 : « ids=bitcoin,ethereum&devise=eur »
  char    pPlex[4];
  uint8_t ext26;         // 1 = pCrypto renseigné
  uint8_t reserve[6];
};

struct ConfigV3 {            // MODELROGS Display 2.1.x (= v4 sans validated/reserve)
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
  char pHeure[24];
  char pMeteo[24];
  char pDiscord[40];
  char pSup[300];
  char pBtc[24];
};

static const char* const AP_DEFAUT = "MODELROGS Display";
static const char* const AP_ANCIEN = "MODELROGS Discord";   // nom du firmware Discord
static const uint8_t     LUMINOSITE_DEFAUT = 60;

static void copyStr(char* dst, size_t size, const char* src) {
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

static void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = PRODUCT_MAGIC;
  cfg.version = CONFIG_VERSION;
  copyStr(cfg.apSsid, sizeof(cfg.apSsid), AP_DEFAUT);
  setPagesMask(PG_COUNT >= 32 ? 0xFFFFFFFFu : (1u << PG_COUNT) - 1);
  cfg.pageSec = 10;
  cfg.nightStart = 23 * 60;
  cfg.nightEnd = 7 * 60;
  cfg.brightDay = LUMINOSITE_DEFAUT;
  cfg.brightNight = 20;
  // Pages de cours : réglages vides = valeurs du docker-compose de la passerelle
}

// Réglages Crypto d'avant la 2.9 (« ids=bitcoin,ethereum,ripple&devise=eur »)
// -> 3 listes (« s1=bitcoin&s2=ethereum&s3=ripple ») ; la devise disparaît
// (tous les prix sont en dollars).
static void reprendreCrypto(const char* ancien) {
  cfg.pCrypto[0] = '\0';
  const char* p = strstr(ancien, "ids=");
  if (!p || (p != ancien && p[-1] != '&')) return;
  p += 4;
  String out;
  uint8_t n = 0;
  while (*p && *p != '&' && n < 3) {
    const char* fin = p;
    while (*fin && *fin != ',' && *fin != '&') fin++;
    if (fin > p && *p != '*') {
      if (out.length()) out += '&';
      out += "s" + String(++n) + "=" + String(p).substring(0, fin - p);
    }
    p = *fin == ',' ? fin + 1 : fin;
  }
  copyStr(cfg.pCrypto, sizeof(cfg.pCrypto), out.c_str());
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

static int hexVal(char c);

// Valeur brute (encodée) d'une clé dans une chaîne « cle=valeur&… » ("" si absente)
static String valeurBrute(const char* p, const char* cle) {
  const size_t n = strlen(cle);
  while (*p) {
    const char* fin = strchr(p, '&');
    if (!fin) fin = p + strlen(p);
    if ((size_t)(fin - p) > n && strncmp(p, cle, n) == 0 && p[n] == '=') return String(p).substring(n + 1, fin - p);
    p = *fin ? fin + 1 : fin;
  }
  return "";
}

// 3.5.0 : la page Devises disparaît ; sa place (bit et tampon) devient Actions
// Europe. Les devises choisies passent dans la page Personnalisée (« d:EUR »),
// dans les cases libres de ses listes ; Actions Europe est activée comme
// Actions Monde ; seuils par défaut (3 % sur 24 h, 5 % sur 7 jours).
static void reprendreMarches() {
  // Devises choisies : listes 1 à 3, puis saisie (« m », séparée par , ; ou espaces)
  String codes[3];
  uint8_t n = 0;
  for (const char* k : { "s1", "s2", "s3" }) {
    const String v = valeurBrute(cfg.pEurope, k);
    if (v.length() && n < 3) codes[n++] = v;
  }
  // Saisie : séparateurs (, ; espace, éventuellement encodés) ramenés à des espaces
  const String brut = valeurBrute(cfg.pEurope, "m");
  String m;
  for (int k = 0; k < (int)brut.length(); k++) {
    char c = brut[k];
    if (c == '%' && k + 2 < (int)brut.length() && hexVal(brut[k + 1]) >= 0 && hexVal(brut[k + 2]) >= 0) {
      c = (char)(hexVal(brut[k + 1]) * 16 + hexVal(brut[k + 2]));
      k += 2;
    }
    m += (c == ',' || c == ';' || c == '+') ? ' ' : c;
  }
  int i = 0;
  while (n < 3 && i < (int)m.length()) {
    while (i < (int)m.length() && m[i] == ' ') i++;
    int j = i;
    while (j < (int)m.length() && m[j] != ' ') j++;
    if (j > i) codes[n++] = m.substring(i, j);
    i = j;
  }
  // Cases libres de la page Personnalisée
  String s[3], mc = valeurBrute(cfg.pCustom, "m");
  for (uint8_t k = 0; k < 3; k++) s[k] = valeurBrute(cfg.pCustom, k == 0 ? "s1" : k == 1 ? "s2" : "s3");
  uint8_t repris = 0;
  for (uint8_t k = 0; k < 3 && repris < n; k++)
    if (!s[k].length()) s[k] = "d%3A" + codes[repris++];
  if (repris) {
    String nouveau;
    for (uint8_t k = 0; k < 3; k++)
      if (s[k].length()) nouveau += String(nouveau.length() ? "&" : "") + "s" + String(k + 1) + "=" + s[k];
    if (mc.length()) nouveau += String(nouveau.length() ? "&" : "") + "m=" + mc;
    if (nouveau.length() < sizeof(cfg.pCustom)) {
      copyStr(cfg.pCustom, sizeof(cfg.pCustom), nouveau.c_str());
      if (pageEnabled(PG_EUROPE)) setPageEnabled(PG_CUSTOM, true);   // l'ancienne page Devises était affichée
    }
  }
  memset(cfg.pEurope, 0, sizeof(cfg.pEurope));
  setPageEnabled(PG_EUROPE, pageEnabled(PG_ACTIONS));
  cfg.reserve[5] = SEUIL24_DEFAUT;
  cfg.reserve[6] = SEUIL7_DEFAUT;
  cfg.reserve[4] |= RES_MARCHES;
}

static void sanitize() {
  terminate(cfg.wifiSsid, sizeof(cfg.wifiSsid));
  terminate(cfg.wifiPass, sizeof(cfg.wifiPass));
  terminate(cfg.apSsid, sizeof(cfg.apSsid));
  terminate(cfg.gateway, sizeof(cfg.gateway));
  terminate(cfg.ville, sizeof(cfg.ville));
  for (uint8_t p = 0; p < PG_COUNT; p++) terminate(modParams(p), modParamsSize(p));
  if (cfg.apSsid[0] == '\0') copyStr(cfg.apSsid, sizeof(cfg.apSsid), AP_DEFAUT);
  if (!(cfg.reserve[0] & RES_PAGE_TESLA)) {   // première lecture par la 2.15 ou plus récente
    cfg.pages |= 1u << PG_TESLA;
    cfg.reserve[0] |= RES_PAGE_TESLA;
  }
  if (!(cfg.reserve[4] & RES_MARCHES)) reprendreMarches();   // première lecture par la 3.5.0
  cfg.reserve[5] = min(cfg.reserve[5], SEUIL_MAX);
  cfg.reserve[6] = min(cfg.reserve[6], SEUIL_MAX);
  // Pages 0 à 31 (3.3.0) : bits au-delà des pages existantes effacés
  setPagesMask(pagesMask() & (PG_COUNT >= 32 ? 0xFFFFFFFFu : (1u << PG_COUNT) - 1));
  cfg.reserve[1] &= 0x07;          // 3.1.0 : bits des personnages (Heure, Tesla, Supervision)
  setPageEnabled(PG_HEURE, true);  // 3.0.0 : la page Heure est toujours active (interface)
  cfg.pageSec = constrain(cfg.pageSec, 5, 120);
  cfg.nightStart %= 1440;
  cfg.nightEnd %= 1440;
  cfg.brightDay = constrain(cfg.brightDay, 5, 100);
  cfg.brightNight = min<uint8_t>(cfg.brightNight, 100);
}

// Reprise d'une configuration d'un firmware plus ancien : le nom de point
// d'accès hérité du firmware Discord est remplacé, la luminosité plafonnée.
static void apresMigration() {
  if (strcmp(cfg.apSsid, AP_ANCIEN) == 0) copyStr(cfg.apSsid, sizeof(cfg.apSsid), AP_DEFAUT);
  cfg.brightDay = min<uint8_t>(cfg.brightDay, LUMINOSITE_DEFAUT);
}

static void copyV3(const ConfigV3& o) {
  copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), o.wifiSsid);
  copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), o.wifiPass);
  copyStr(cfg.apSsid, sizeof(cfg.apSsid), o.apSsid);
  copyStr(cfg.gateway, sizeof(cfg.gateway), o.gateway);
  copyStr(cfg.ville, sizeof(cfg.ville), o.ville);
  cfg.pages = o.pages;
  cfg.pageSec = o.pageSec;
  cfg.nightOn = o.nightOn;
  cfg.nightStart = o.nightStart;
  cfg.nightEnd = o.nightEnd;
  cfg.brightDay = o.brightDay;
  cfg.brightNight = o.brightNight;
  copyStr(cfg.pHeure, sizeof(cfg.pHeure), o.pHeure);
  copyStr(cfg.pMeteo, sizeof(cfg.pMeteo), o.pMeteo);
  copyStr(cfg.pDiscord, sizeof(cfg.pDiscord), o.pDiscord);
  char ancien[sizeof(o.pBtc) + 1];
  copyStr(ancien, sizeof(ancien), o.pBtc);
  reprendreCrypto(ancien);
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic;
  uint16_t version;
  EEPROM.get(0, magic);
  EEPROM.get(4, version);

  // --- Même firmware (MODELROGS Display), structure actuelle : tout est conservé,
  //     y compris la validation.
  if (magic == PRODUCT_MAGIC && version == CONFIG_VERSION) {
    EEPROM.get(0, cfg);
    sanitize();
    Serial.println(F("EEPROM : configuration chargee."));
    return;
  }

  // --- Même firmware, 2.11 (structure 7) : la structure 8 commence comme elle ;
  //     le réglage de la page Photos, ajouté à la fin, part vide (au hasard,
  //     30 s). Tout est conservé, validation comprise ; la page Photos est activée.
  if (magic == PRODUCT_MAGIC && version == 7) {
    EEPROM.get(0, cfg);
    memset(cfg.pPhotos, 0, sizeof(cfg.pPhotos));
    cfg.pages |= PAGES_212;
    Serial.println(F("EEPROM : reprise de la configuration 2.11."));
    saveConfig();
    return;
  }

  // --- Même firmware, 2.10 (structure 6) : la structure 7 commence comme elle ;
  //     le réglage de la page Custom, ajouté à la fin, part vide (valeurs du
  //     compose). Tout est conservé, validation comprise ; la page Custom est activée.
  if (magic == PRODUCT_MAGIC && version == 6) {
    EEPROM.get(0, cfg);
    memset(cfg.pCustom, 0, sizeof(cfg.pCustom));
    memset(cfg.pPhotos, 0, sizeof(cfg.pPhotos));
    cfg.pages |= PAGES_211 | PAGES_212;
    Serial.println(F("EEPROM : reprise de la configuration 2.10."));
    saveConfig();
    return;
  }

  // --- Même firmware, 2.9.0 et 2.9.1 (structure 5) : même disposition, le
  //     réglage des indices occupe une partie de l'ancienne réserve (vide).
  //     Tout est conservé, validation comprise ; la page Indices est activée.
  if (magic == PRODUCT_MAGIC && version == 5) {
    EEPROM.get(0, cfg);
    memset(cfg.pIndices, 0, sizeof(cfg.pIndices));
    memset(cfg.reserve, 0, sizeof(cfg.reserve));
    memset(cfg.pCustom, 0, sizeof(cfg.pCustom));
    memset(cfg.pPhotos, 0, sizeof(cfg.pPhotos));
    cfg.pages |= PAGES_210 | PAGES_211 | PAGES_212;
    Serial.println(F("EEPROM : reprise de la configuration 2.9.0-2.9.1."));
    saveConfig();
    return;
  }

  setDefaults();

  // --- Même firmware, 2.2 à 2.8 (structure 4) : tout est repris, y compris la
  //     validation ; Crypto passe au nouveau format, pages de cours activées.
  if (magic == PRODUCT_MAGIC && version == 4) {
    ConfigV4 old;
    EEPROM.get(0, old);
    copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), old.wifiSsid);
    copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), old.wifiPass);
    copyStr(cfg.apSsid, sizeof(cfg.apSsid), old.apSsid);
    copyStr(cfg.gateway, sizeof(cfg.gateway), old.gateway);
    copyStr(cfg.ville, sizeof(cfg.ville), old.ville);
    cfg.pages = old.pages | PAGES_29 | PAGES_210 | PAGES_211 | PAGES_212 | (old.ext26 == 1 ? 0 : 1 << PG_PLEX);
    cfg.pageSec = old.pageSec;
    cfg.nightOn = old.nightOn;
    cfg.nightStart = old.nightStart;
    cfg.nightEnd = old.nightEnd;
    cfg.brightDay = old.brightDay;
    cfg.brightNight = old.brightNight;
    copyStr(cfg.pHeure, sizeof(cfg.pHeure), old.pHeure);
    copyStr(cfg.pMeteo, sizeof(cfg.pMeteo), old.pMeteo);
    copyStr(cfg.pDiscord, sizeof(cfg.pDiscord), old.pDiscord);
    old.pCrypto[sizeof(old.pCrypto) - 1] = '\0';
    old.pBtc[sizeof(old.pBtc) - 1] = '\0';
    reprendreCrypto(old.ext26 == 1 ? old.pCrypto : old.pBtc);
    cfg.validated = old.validated == 1;
    Serial.println(F("EEPROM : reprise de la configuration 2.2-2.8."));
    saveConfig();
    return;
  }

  // --- Même firmware, structure future ou inconnue (retour à une version
  //     plus ancienne) : rien n'est lisible, on repart des valeurs par défaut.
  //     Les futures versions ajouteront leurs migrations (version 5 -> 6...)
  //     au-dessus, en conservant cfg.validated.
  if (magic == PRODUCT_MAGIC) {
    Serial.println(F("EEPROM : structure inconnue -> valeurs par defaut."));

  // --- Display 2.1.x : même firmware, mais sans validation enregistrée.
  } else if (magic == LEGACY_MAGIC && version == 3) {
    ConfigV3 old;
    EEPROM.get(0, old);
    copyV3(old);
    apresMigration();
    Serial.println(F("EEPROM : reprise de la configuration 2.1 (a valider)."));

  // --- Display 2.0.x
  } else if (magic == LEGACY_MAGIC && version == 2) {
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
    apresMigration();
    Serial.println(F("EEPROM : reprise de la configuration 2.0 (a valider)."));

  // --- Autre firmware (MODELROGS Discord) : Wi-Fi et passerelle pré-remplis.
  } else if (magic == LEGACY_MAGIC && version == 1) {
    ConfigV1 old;
    EEPROM.get(0, old);
    old.wifiSsid[sizeof(old.wifiSsid) - 1] = '\0';
    old.wifiPassword[sizeof(old.wifiPassword) - 1] = '\0';
    old.discordUrl[sizeof(old.discordUrl) - 1] = '\0';
    copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), old.wifiSsid);
    copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), old.wifiPassword);
    gatewayFromOldUrl(old.discordUrl);
    Serial.println(F("EEPROM : firmware different (Discord) -> Wi-Fi pre-rempli, a valider."));

  // --- Autre firmware inconnu ou EEPROM vierge.
  } else {
    Serial.println(F("EEPROM : configuration absente -> valeurs par defaut."));
  }

  cfg.pages |= (1 << PG_PLEX) | PAGES_29 | PAGES_210 | PAGES_211 | PAGES_212;   // pages apparues depuis (si le module est actif)
  cfg.validated = 0;   // firmware différent ou jamais validé : validation requise
  saveConfig();
}

void saveConfig() {
  cfg.magic = PRODUCT_MAGIC;
  cfg.version = CONFIG_VERSION;
  sanitize();
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println(F("EEPROM : configuration sauvegardee."));
}

// Masque des pages (3.3.0) : pages 0 à 15 dans cfg.pages, 16 à 31 dans
// reserve[2] (16 à 23) et reserve[3] (24 à 31). Structure de Config inchangée :
// une configuration 3.2.0 ou plus ancienne (reserve[2..3] à 0) se relit telle quelle.
uint32_t pagesMask() {
  return (uint32_t)cfg.pages | (uint32_t)cfg.reserve[2] << 16 | (uint32_t)cfg.reserve[3] << 24;
}

void setPagesMask(uint32_t m) {
  cfg.pages = (uint16_t)m;
  cfg.reserve[2] = (uint8_t)(m >> 16);
  cfg.reserve[3] = (uint8_t)(m >> 24);
}

bool pageEnabled(uint8_t page) {
  return page < PG_COUNT && (pagesMask() & ((uint32_t)1 << page));
}

void setPageEnabled(uint8_t page, bool actif) {
  if (page >= 32) return;
  const uint32_t b = (uint32_t)1 << page;
  setPagesMask(actif ? pagesMask() | b : pagesMask() & ~b);
}

// reserve[1] : un bit par domaine, à 1 quand le personnage est MASQUÉ
static uint8_t bitPerso(uint8_t page) {
  switch (page) {
    case PG_HEURE: return 0x01;
    case PG_TESLA: return 0x02;
    case PG_SUP:   return 0x04;
    default:       return 0;
  }
}

bool persoActif(uint8_t page) {
  const uint8_t b = bitPerso(page);
  return b && !(cfg.reserve[1] & b);
}

void persoRegler(uint8_t page, bool actif) {
  const uint8_t b = bitPerso(page);
  if (actif) cfg.reserve[1] &= ~b;
  else cfg.reserve[1] |= b;
}

char* modParams(uint8_t page) {
  switch (page) {
    case PG_HEURE:   return cfg.pHeure;
    case PG_METEO:   return cfg.pMeteo;
    case PG_DISCORD: return cfg.pDiscord;
    case PG_SUP:     return cfg.pSup;
    case PG_PLEX:
    case PG_TESLA:   return cfg.pPlex;   // pages sans réglage : même tampon vide
    case PG_ACTIONS: return cfg.pActions;
    case PG_EUROPE:  return cfg.pEurope;
    case PG_COMMO:   return cfg.pCommo;
    case PG_INDICES: return cfg.pIndices;
    case PG_CUSTOM:  return cfg.pCustom;
    case PG_PHOTOS:  return cfg.pPhotos;
    default:         return cfg.pCrypto;
  }
}

size_t modParamsSize(uint8_t page) {
  switch (page) {
    case PG_HEURE:   return sizeof(cfg.pHeure);
    case PG_METEO:   return sizeof(cfg.pMeteo);
    case PG_DISCORD: return sizeof(cfg.pDiscord);
    case PG_SUP:     return sizeof(cfg.pSup);
    case PG_PLEX:
    case PG_TESLA:   return sizeof(cfg.pPlex);
    case PG_ACTIONS: return sizeof(cfg.pActions);
    case PG_EUROPE:  return sizeof(cfg.pEurope);
    case PG_COMMO:   return sizeof(cfg.pCommo);
    case PG_INDICES: return sizeof(cfg.pIndices);
    case PG_CUSTOM:  return sizeof(cfg.pCustom);
    case PG_PHOTOS:  return sizeof(cfg.pPhotos);
    default:         return sizeof(cfg.pCrypto);
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

// --------------------------------------------------------------- Seuils des marchés (3.5.0)

uint8_t seuil24() { return cfg.reserve[5]; }
uint8_t seuil7() { return cfg.reserve[6]; }

void reglerSeuils(uint8_t demi24, uint8_t demi7) {
  cfg.reserve[5] = min(demi24, SEUIL_MAX);
  cfg.reserve[6] = min(demi7, SEUIL_MAX);
}

String texteSeuil(uint8_t demi) {
  return String(demi / 2) + (demi % 2 ? ".5" : "");
}
