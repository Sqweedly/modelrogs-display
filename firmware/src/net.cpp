#include "net.h"
#include "config.h"
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

int gwGet(const String& path, JsonDocument& doc, JsonDocument* filter, bool interactif) {
  int code;
  if (!staConnected()) {
    code = -1;
  } else if (cfg.gateway[0] == '\0') {
    code = -2;
  } else if (!interactif && gwRetryInSeconds() > 0) {
    return -3;   // temporisation en cours : pas de tentative, réponse immédiate
  } else {
    WiFiClient client;
    HTTPClient http;
    String url = String(cfg.gateway) + path;
    code = -3;
    if (http.begin(client, url)) {
      http.setTimeout(2500);   // borne aussi la connexion TCP
      http.useHTTP10(true);    // pas de « chunked » : lecture directe du flux
      http.setReuse(false);
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        DeserializationError err = filter
          ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
          : deserializeJson(doc, http.getStream());
        if (err) {
          code = -4;
        } else if (!(doc["ok"] | 0)) {
          code = -5;
        } else {
          code = 200;
        }
      } else if (httpCode > 0) {
        code = httpCode;
      }
      http.end();
    }
  }
  // Passerelle injoignable ou en erreur serveur : on espace les tentatives.
  if (code == -3 || code >= 500) {
    if (gwFails < 255) gwFails++;
    uint8_t i = min<uint8_t>(gwFails, sizeof(GW_PAUSES_S) / sizeof(GW_PAUSES_S[0])) - 1;
    gwRetryAt = millis() + GW_PAUSES_S[i] * 1000UL;
  } else if (code != -1 && code != -2) {
    gwFails = 0;   // la passerelle a répondu : rythme normal
  }
  gwLastCode = code;
  if (code == 200) gwLastOkMs = millis();
  return code;
}

bool gwImage(const char* jeton, int x, int y, int w, int h, uint16_t fond, int rayon, bool ajuster) {
  if (!jeton || !jeton[0] || w <= 0 || h <= 0 || w > 160 || h > 160) return false;
  if (!staConnected() || cfg.gateway[0] == '\0' || gwRetryInSeconds() > 0) return false;

  // RGB565 -> RRGGBB (couleur exacte du fond sous les coins arrondis)
  uint8_t r = (fond >> 11) << 3, g = ((fond >> 5) & 0x3F) << 2, b = (fond & 0x1F) << 3;
  r |= r >> 5; g |= g >> 6; b |= b >> 5;
  char path[96];
  snprintf(path, sizeof(path), "/img?t=%s&w=%d&h=%d&f=%02X%02X%02X&r=%d%s",
           jeton, w, h, r, g, b, rayon, ajuster ? "&m=a" : "");

  WiFiClient client;
  HTTPClient http;
  bool ok = false;
  if (http.begin(client, String(cfg.gateway) + path)) {
    http.setTimeout(2500);
    http.useHTTP10(true);
    http.setReuse(false);
    if (http.GET() == HTTP_CODE_OK && http.getSize() == w * h * 2) {
      WiFiClient& flux = http.getStream();
      uint16_t ligne[160];
      tft.setSwapBytes(true);        // pixels reçus dans l'ordre natif de l'ESP
      ok = true;
      for (int j = 0; j < h; j++) {
        if (flux.readBytes((uint8_t*)ligne, w * 2) != (size_t)(w * 2)) { ok = false; break; }
        tft.pushImage(x, y + j, w, 1, ligne);
        if ((j & 15) == 15) yield();
      }
      tft.setSwapBytes(false);
    }
    http.end();
  }
  return ok;
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
