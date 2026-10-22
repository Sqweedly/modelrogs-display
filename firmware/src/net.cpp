#include "net.h"
#include "config.h"

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

int gwGet(const String& path, JsonDocument& doc, JsonDocument* filter) {
  int code;
  if (!staConnected()) {
    code = -1;
  } else if (cfg.gateway[0] == '\0') {
    code = -2;
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
  gwLastCode = code;
  if (code == 200) gwLastOkMs = millis();
  return code;
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
