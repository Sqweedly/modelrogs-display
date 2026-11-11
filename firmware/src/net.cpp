#include "net.h"
#include "memo.h"
#include "config.h"
#include "diag.h"
#include "ui.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

int gwLastCode = 0;
uint32_t gwLastOkMs = 0;

static IPAddress AP_IP(192, 168, 4, 1);
static IPAddress AP_MASK(255, 255, 255, 0);
static uint32_t lastStaAttempt = 0;
static bool wasConnected = false;

static void startAp() {
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  // Point d'accès OUVERT (sans mot de passe), 4 clients maximum.
  bool ok = WiFi.softAP(cfg.apSsid, nullptr, 1, false, 4);
  Serial.printf("AP \"%s\" : %s (%s)\n", cfg.apSsid, ok ? "OK" : "ECHEC",
                WiFi.softAPIP().toString().c_str());
}

static void startSta() {
  if (cfg.wifiSsid[0] == '\0') return;
  Serial.printf("STA : connexion a \"%s\"...\n", cfg.wifiSsid);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);   // non bloquant
  lastStaAttempt = millis();
}

void netBegin() {
  WiFi.persistent(false);       // la configuration vit dans notre EEPROM
  WiFi.mode(WIFI_AP_STA);       // l'AP ne dépend jamais de la STA
  WiFi.setAutoReconnect(true);
  WiFi.hostname("modelrogs-display");
  startAp();
  startSta();
}

void netTick() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      Serial.printf("STA : connecte, IP %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println(F("STA : deconnecte (AP toujours actif)"));
    }
  }
  // Nouvelle tentative toutes les 30 s, sans jamais bloquer ni couper l'AP.
  if (!connected && cfg.wifiSsid[0] && millis() - lastStaAttempt > 30000UL) {
    startSta();
  }
}

bool staConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void netApplyConfig(bool wifiChanged, bool apChanged) {
  if (apChanged) startAp();
  if (wifiChanged) {
    WiFi.disconnect(false);   // false : la STA seule, l'AP reste actif
    startSta();
  }
}

String urlEncode(const char* s) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(strlen(s) * 3);
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
      out += (char)*p;
    } else {
      out += '%';
      out += hex[*p >> 4];
      out += hex[*p & 0x0F];
    }
  }
  return out;
}

static uint8_t  gwFails = 0;      // échecs de connexion consécutifs
static uint32_t gwRetryAt = 0;    // millis() à partir duquel on réessaie
static const uint16_t GW_PAUSES_S[] = { 5, 15, 30, 60 };

uint32_t gwRetryInSeconds() {
  int32_t reste = (int32_t)(gwRetryAt - millis());
  return (gwFails && reste > 0) ? (reste + 999) / 1000 : 0;
}

// Raté réseau isolé (3.0.0) : un SYN perdu n'est renvoyé par lwIP qu'au bout
// de 3 s, après l'abandon à 2,5 s. Un nouvel essai ouvre une nouvelle
// connexion, donc un nouveau SYN, tout de suite. Requêtes GET : sans risque à
// répéter. Codes HTTPClient : -1 connexion impossible, -5 connexion perdue,
// -11 délai dépassé (en-têtes non reçus).
static bool rateReseau(int codeHttp) {
  return codeHttp == HTTPC_ERROR_CONNECTION_FAILED || codeHttp == HTTPC_ERROR_CONNECTION_LOST ||
         codeHttp == HTTPC_ERROR_READ_TIMEOUT;
}

// Lecture d'un texte en mémoire comme un flux : deserializeJson reçoit alors
// le même type (Stream&) que pour la réponse réseau, et le firmware n'embarque
// qu'un seul analyseur JSON (3.4.0, environ 2,5 Ko de flash de moins, mesuré).
class LecteurTexte : public Stream {
 public:
  explicit LecteurTexte(const String& t) : p_(t.c_str()), n_(t.length()) {}
  int available() override { return (int)(n_ - i_); }
  int read() override { return i_ < n_ ? (uint8_t)p_[i_++] : -1; }
  int peek() override { return i_ < n_ ? (uint8_t)p_[i_] : -1; }
  size_t write(uint8_t) override { return 0; }
  size_t readBytes(char* b, size_t l) override {
    size_t k = l < n_ - i_ ? l : n_ - i_;
    memcpy(b, p_ + i_, k); i_ += k; return k;
  }
 private:
  const char* p_; size_t n_, i_ = 0;
};

int gwRelire(JsonDocument& doc, const String& texte) {
  LecteurTexte lu(texte);
  return deserializeJson(doc, static_cast<Stream&>(lu)) ? -4 : 200;
}

// Un essai de GET JSON. Renvoie le code de gwGet ; *codeHttp = code d'HTTPClient.
// `brut` (memo.h) : si non nul et la réponse fait au plus MEMO_REPONSE octets,
// elle est lue en entier dans *brut puis analysée depuis ce texte.
static int gwGetEssai(const String& path, JsonDocument& doc, JsonDocument* filter, int& codeHttp,
                      int16_t& diagCode, String* brut) {
  WiFiClient client;
  HTTPClient http;
  String url = String(cfg.gateway) + path;
  int code = -3;
  codeHttp = 0;
  diagCode = DG_ADRESSE;
  const uint32_t id = diagNouvelId();
  const uint32_t t0 = millis();
  uint32_t tEnTetes = t0;
  int taille = 0;
  if (http.begin(client, url)) {
    http.setTimeout(2500);   // borne aussi la connexion TCP
    http.useHTTP10(true);    // pas de « chunked » : lecture directe du flux
    http.setReuse(false);
    http.addHeader(F("X-Req"), String(id));   // diagnostic : même numéro dans le journal de la passerelle
    codeHttp = http.GET();
    tEnTetes = millis();
    diagCode = codeHttp;
    if (codeHttp == HTTP_CODE_OK) {
      taille = http.getSize();
      doc.clear();
      DeserializationError err;
      if (brut && taille > 0 && taille <= (int)MEMO_REPONSE) {
        *brut = http.getString();
        LecteurTexte lu(*brut);
        err = deserializeJson(doc, static_cast<Stream&>(lu));
      } else {
        Stream& flux = http.getStream();   // un seul analyseur JSON pour les deux lectures (flash)
        err = filter
          ? deserializeJson(doc, flux, DeserializationOption::Filter(*filter))
          : deserializeJson(doc, flux);
      }
      if (err) {
        code = -4;
        diagCode = DG_JSON_KO;
      } else if (!(doc["ok"] | 0)) {
        code = -5;
        diagCode = DG_SOURCE;
      } else {
        code = 200;
      }
    } else if (codeHttp > 0) {
      code = codeHttp;
    }
    http.end();
  }
  diagRequete(DG_JSON, id, path.c_str(), diagCode, tEnTetes - t0, millis() - t0, taille > 0 ? taille : 0);
  return code;
}

static int gwGetInterne(const String& path, JsonDocument& doc, JsonDocument* filter, bool interactif,
                        String* brut) {
  int code;
  int16_t diagCode = 0;   // code détaillé pour le diagnostic (diag.h)
  if (!staConnected()) {
    code = -1;
  } else if (cfg.gateway[0] == '\0') {
    code = -2;
  } else if (!interactif && gwRetryInSeconds() > 0) {
    diagSaute(false);
    return -3;   // temporisation en cours : pas de tentative, réponse immédiate
  } else {
    int codeHttp;
    code = gwGetEssai(path, doc, filter, codeHttp, diagCode, brut);
    if (code == -3 && rateReseau(codeHttp) && staConnected()) {
      yield();                                   // Wi-Fi et chien de garde entre les deux essais
      code = gwGetEssai(path, doc, filter, codeHttp, diagCode, brut);
      diagRelance(code != -3);
    }
  }
  // Passerelle injoignable (deux essais ratés, ou autre erreur réseau) ou en
  // erreur serveur : on espace les tentatives.
  if (code == -3 || code >= 500) {
    if (gwFails < 255) gwFails++;
    uint8_t i = min<uint8_t>(gwFails, sizeof(GW_PAUSES_S) / sizeof(GW_PAUSES_S[0])) - 1;
    gwRetryAt = millis() + GW_PAUSES_S[i] * 1000UL;
    diagTempo(path.c_str(), code >= 500 ? (int16_t)code : diagCode);
  } else if (code != -1 && code != -2) {
    gwFails = 0;   // la passerelle a répondu : rythme normal
  }
  gwLastCode = code;
  if (code == 200) gwLastOkMs = millis();
  return code;
}

int gwGet(const String& path, JsonDocument& doc, JsonDocument* filter, bool interactif) {
  return gwGetInterne(path, doc, filter, interactif, nullptr);
}

int gwGetBrut(const String& path, JsonDocument& doc, String& brut) {
  brut = String();
  const int code = gwGetInterne(path, doc, nullptr, false, &brut);
  if (code != 200) brut = String();   // seule une réponse valide est gardée
  return code;
}

static bool gwImageEssai(const char* path, int x, int y, int w, int h, int& codeHttpSortie);

bool gwImage(const char* jeton, int x, int y, int w, int h, uint16_t fond, int rayon, bool ajuster) {
  if (!jeton || !jeton[0] || w <= 0 || h <= 0 || w > 240 || h > 240) return false;
  if (!staConnected() || cfg.gateway[0] == '\0') return false;
  if (gwRetryInSeconds() > 0) { diagSaute(true); return false; }

  // RGB565 -> RRGGBB (couleur exacte du fond sous les coins arrondis)
  uint8_t r = (fond >> 11) << 3, g = ((fond >> 5) & 0x3F) << 2, b = (fond & 0x1F) << 3;
  r |= r >> 5; g |= g >> 6; b |= b >> 5;
  char path[96];
  snprintf(path, sizeof(path), "/img?t=%s&w=%d&h=%d&f=%02X%02X%02X&r=%d%s",
           jeton, w, h, r, g, b, rayon, ajuster ? "&m=a" : "");

  int codeHttp;
  bool ok = gwImageEssai(path, x, y, w, h, codeHttp);
  if (!ok && rateReseau(codeHttp) && staConnected()) {   // rien n'a été dessiné
    yield();
    ok = gwImageEssai(path, x, y, w, h, codeHttp);
    diagRelance(ok);
  }
  return ok;
}

static bool gwImageEssai(const char* path, int x, int y, int w, int h, int& codeHttpSortie) {
  WiFiClient client;
  HTTPClient http;
  bool ok = false;
  const uint32_t id = diagNouvelId();
  const uint32_t t0 = millis();
  uint32_t tEnTetes = t0, recus = 0;
  int16_t diagCode = DG_ADRESSE;
  codeHttpSortie = 0;
  if (http.begin(client, String(cfg.gateway) + path)) {
    http.setTimeout(2500);
    http.useHTTP10(true);
    http.setReuse(false);
    http.addHeader(F("X-Req"), String(id));   // diagnostic : même numéro dans le journal de la passerelle
    const int codeHttp = http.GET();
    codeHttpSortie = codeHttp;
    tEnTetes = millis();
    const int taille = codeHttp == HTTP_CODE_OK ? http.getSize() : 0;
    diagCode = codeHttp;
    if (codeHttp == HTTP_CODE_OK && taille != w * h * 2) {
      diagCode = DG_TAILLE;
      recus = taille > 0 ? taille : 0;
    }
    if (codeHttp == HTTP_CODE_OK && taille == w * h * 2) {
      WiFiClient& flux = http.getStream();
      uint16_t ligne[240];
      tft.setSwapBytes(true);        // pixels reçus dans l'ordre natif de l'ESP
      ok = true;
      for (int j = 0; j < h; j++) {
        size_t n = flux.readBytes((uint8_t*)ligne, w * 2);
        recus += n;
        if (n != (size_t)(w * 2)) { ok = false; break; }
        tft.pushImage(x, y + j, w, 1, ligne);
        if ((j & 15) == 15) yield();
      }
      tft.setSwapBytes(false);
      if (!ok) diagCode = DG_INCOMPL;
    }
    http.end();
  }
  diagRequete(DG_IMG, id, path, diagCode, tEnTetes - t0, millis() - t0, recus);
  return ok;
}

void netSonde() {
  static uint32_t derniere = 0;
  if (!diagActif() || !staConnected() || cfg.gateway[0] == '\0') return;
  if (derniere && millis() - derniere < 30000UL) return;
  derniere = millis();
  WiFiClient client;
  HTTPClient http;
  const uint32_t id = diagNouvelId();
  const uint32_t t0 = millis();
  uint32_t tEnTetes = t0;
  int16_t code = DG_ADRESSE;
  int taille = 0;
  if (http.begin(client, String(cfg.gateway) + F("/ping"))) {
    http.setTimeout(2500);
    http.useHTTP10(true);
    http.setReuse(false);
    http.addHeader(F("X-Req"), String(id));
    code = http.GET();
    tEnTetes = millis();
    if (code == HTTP_CODE_OK) {
      taille = http.getSize();
      http.getString();          // lit la réponse ({"ok":1}) jusqu'au bout
    }
    http.end();
  }
  diagRequete(DG_PING, id, "/ping", code, tEnTetes - t0, millis() - t0, taille > 0 ? taille : 0);
}

const char* gwErrorText(int code) {
  switch (code) {
    case 200: return "OK";
    case -1:  return "Wi-Fi non connecté";
    case -2:  return "Passerelle non configurée";
    case -3:  return "Passerelle injoignable";
    case -4:  return "Réponse illisible";
    case -5:  return "Source indisponible";
    default:  return "Erreur HTTP";
  }
}
