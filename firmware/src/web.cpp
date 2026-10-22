#include "web.h"
#include "app.h"
#include "config.h"
#include "net.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WiFi.h>

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer httpUpdater;
static DNSServer dnsServer;

// ------------------------------------------------------------------ outils

// Envoi de la page par morceaux : évite de construire 10 Ko en RAM.
class Chunks {
 public:
  Chunks() { buf.reserve(1100); }
  Chunks& operator<<(const __FlashStringHelper* s) { buf += s; check(); return *this; }
  Chunks& operator<<(const String& s) { buf += s; check(); return *this; }
  Chunks& operator<<(const char* s) { buf += s; check(); return *this; }
  Chunks& operator<<(long v) { buf += v; check(); return *this; }
  void flush() {
    if (buf.length()) server.sendContent(buf);
    buf = "";
  }
 private:
  String buf;
  void check() { if (buf.length() > 1000) flush(); }
};

static String esc(const char* in) {
  String out;
  for (const char* p = in; *p; p++) {
    switch (*p) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *p;
    }
  }
  return out;
}
static String esc(const String& s) { return esc(s.c_str()); }

static String hhmm(uint16_t minutes) {
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", (minutes / 60) % 24, minutes % 60);
  return b;
}

static uint16_t parseHhmm(const String& s, uint16_t fallback) {
  int sep = s.indexOf(':');
  if (sep < 1) return fallback;
  int h = s.substring(0, sep).toInt(), m = s.substring(sep + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

static void copyArg(const char* name, char* dst, size_t size) {
  String v = server.arg(name);
  v.trim();
  strncpy(dst, v.c_str(), size - 1);
  dst[size - 1] = '\0';
}

// Encodage d'une valeur de réglage (la virgule reste lisible : listes d'identifiants)
static String qsEncode(const String& s) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ',') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static String fieldName(uint8_t page, const char* key) {
  return String("p") + page + "_" + key;
}

static const char STYLE[] PROGMEM =
  "<style>"
  ":root{--bg:#0E1116;--card:#1A1F29;--txt:#F2F4F8;--mut:#9AA3B5;--acc:#8B7CFF;--ok:#3DD68C;--warn:#FFB547;--ko:#FF6B70}"
  "*{box-sizing:border-box}body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
  "background:var(--bg);color:var(--txt);margin:0 auto;padding:16px;max-width:640px}"
  "h1{font-size:22px;margin:4px 0 12px}h2{font-size:16px;margin:0 0 10px;color:var(--acc)}"
  "section{background:var(--card);padding:16px;margin:12px 0;border-radius:14px}"
  ".mod{border-top:1px solid #2A3140;padding:10px 0 4px}.mod:first-of-type{border-top:0}"
  "label{display:block;margin-top:10px;color:var(--mut);font-size:14px}"
  "input[type=text],input[type=password],input[type=number],input[type=time],select{width:100%;padding:11px;"
  "margin-top:5px;background:#11151C;color:var(--txt);border:1px solid #2A3140;border-radius:8px;font-size:15px}"
  "input[type=range]{width:100%}"
  ".chk{display:flex;align-items:center;gap:10px;margin:8px 0;color:var(--txt);font-size:15px}"
  ".chk input{width:20px;height:20px}.mod>.chk{font-weight:600}"
  ".grid{display:grid;grid-template-columns:1fr 1fr;gap:4px 12px}"
  ".row{display:flex;gap:12px}.row>*{flex:1}"
  "button{width:100%;padding:14px;margin-top:8px;border:0;border-radius:10px;background:var(--acc);"
  "color:#0E1116;font-size:16px;font-weight:700}"
  ".sm{display:inline-block;width:auto;padding:8px 12px;margin:4px 6px 0 0;font-size:14px;"
  "background:#2A3140;color:var(--txt);border-radius:8px;text-decoration:none;border:0}"
  ".kv{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:14px}.kv b{color:var(--mut);font-weight:500}"
  ".ok{color:var(--ok)}.warn{color:var(--warn)}.ko{color:var(--ko)}.note{color:var(--mut);font-size:13px;margin:6px 0 0}"
  "a{color:var(--acc)}"
  "</style>";

// --------------------------------------------- champs décrits par la passerelle

// Liste dynamique {"ok":1,"s":[{"id","n"}]} lue sur la passerelle.
static int fetchList(const char* src, JsonDocument& list) {
  JsonDocument filter;
  filter["ok"] = true;
  filter["err"] = true;
  filter["s"][0]["id"] = true;
  filter["s"][0]["n"] = true;
  return gwGet(src, list, &filter);
}

// Affiche un champ ; false si impossible (liste injoignable, type inconnu) :
// les réglages de ce module ne seront alors pas modifiés à l'enregistrement.
static bool renderField(Chunks& c, uint8_t page, JsonObject param) {
  const char* key = param["k"] | "";
  const char* type = param["t"] | "";
  if (!key[0]) return false;
  String name = fieldName(page, key);
  String value = modParam(page, key);
  if (!value.length()) value = (const char*)(param["d"] | "");

  c << F("<label>") << esc(param["l"] | key) << F("</label>");

  if (strcmp(type, "text") == 0) {
    c << F("<input type='text' name='") << name << F("' value='") << esc(value) << F("'>");
    return true;
  }

  JsonDocument list;
  JsonArray options = param["o"].as<JsonArray>();
  if (options.isNull()) {
    const char* src = param["src"] | "";
    int code = src[0] ? fetchList(src, list) : -2;
    if (code != 200) {
      c << F("<p class='warn'>Liste indisponible : ")
        << (code == -5 ? esc(list["err"] | "erreur") : String(gwErrorText(code)))
        << F(". Réglage actuel conservé.</p>");
      return false;
    }
    options = list["s"].as<JsonArray>();
  }

  if (strcmp(type, "select") == 0) {
    c << F("<select name='") << name << F("'><option value=''>— choisir —</option>");
    for (JsonObject o : options) {
      String id = (const char*)(o["id"] | "");
      c << F("<option value='") << esc(id) << F("'") << (id == value ? F(" selected") : F("")) << F(">")
        << esc(o["n"] | id.c_str()) << F("</option>");
    }
    c << F("</select>");
    return true;
  }

  if (strcmp(type, "multi") == 0) {
    String selected = "," + value + ",";
    String cls = String("m") + page;
    c << F("<p class='note'>") << (long)options.size() << F(" choix");
    if (param["max"].is<int>()) c << F(" — ") << (long)(param["max"] | 0) << F(" au maximum");
    c << F("</p><a class='sm' href='#' onclick=\"document.querySelectorAll('.") << cls
      << F("').forEach(e=>e.checked=true);return false\">Tout cocher</a>"
           "<a class='sm' href='#' onclick=\"document.querySelectorAll('.") << cls
      << F("').forEach(e=>e.checked=false);return false\">Tout décocher</a><div class='grid' style='margin-top:8px'>");
    for (JsonObject o : options) {
      String id = (const char*)(o["id"] | "");
      bool on = selected.indexOf("," + id + ",") >= 0;
      c << F("<label class='chk'><input class='") << cls << F("' type='checkbox' name='") << name
        << F("' value='") << esc(id) << F("'") << (on ? F(" checked") : F("")) << F(">")
        << esc(o["n"] | id.c_str()) << F("</label>");
    }
    c << F("</div>");
    return true;
  }

  c << F("<p class='warn'>Type de réglage non pris en charge par ce firmware.</p>");
  return false;
}

static void renderModules(Chunks& c) {
  c << F("<section><h2>Pages et modules</h2>");
  c.flush();

  JsonDocument man;
  int code = gwGet("/modules", man);
  if (code != 200) {
    c << F("<p class='warn'>Modules de la passerelle inconnus : ") << gwErrorText(code)
      << F(".</p><p class='note'>Les réglages des modules sont conservés. L'afficheur doit être "
           "connecté au Wi-Fi maison et l'adresse de la passerelle renseignée.</p>");
  }

  for (uint8_t p = 0; p < PG_COUNT; p++) {
    JsonObject mod;
    if (code == 200) {
      for (JsonObject m : man["m"].as<JsonArray>()) {
        if (strcmp(m["id"] | "", PAGE_IDS[p]) == 0) mod = m;
      }
    }
    c << F("<div class='mod'>");

    if (code == 200 && mod.isNull()) {
      // Module non activé sur la passerelle : réglage de page laissé tel quel.
      c << F("<label class='chk'><input type='checkbox' disabled>") << PAGE_NAMES[p]
        << F("</label><p class='note'>Module non activé sur la passerelle (variable MODULES).</p></div>");
      continue;
    }

    c << F("<input type='hidden' name='pk") << (long)p << F("' value='1'>"
           "<label class='chk'><input type='checkbox' name='pg") << (long)p << F("'")
      << (pageEnabled(p) ? F(" checked") : F("")) << F(">") << PAGE_NAMES[p] << F("</label>");

    if (!mod.isNull()) {
      if (!(mod["ok"] | 0)) {
        c << F("<p class='warn'>Non configuré sur la passerelle : ") << esc(mod["err"] | "")
          << F("</p>");
      } else {
        bool all = true;
        for (JsonObject param : mod["p"].as<JsonArray>()) all &= renderField(c, p, param);
        if (all) c << F("<input type='hidden' name='pm") << (long)p << F("' value='1'>");
      }
    }
    c << F("</div>");
    c.flush();
  }

  c << F("<label>Durée d'affichage de chaque page (secondes)</label>"
         "<input type='number' name='duree' min='5' max='120' value='") << (long)cfg.pageSec
    << F("'><p class='note'>Discord n'apparaît que si quelqu'un est dans le salon, et passe en tête "
         "à chaque arrivée. Une panne en supervision affiche l'écran d'alerte jusqu'au retour à la normale.</p></section>");
}

// --------------------------------------------------------------- page « / »

static void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html; charset=utf-8"), "");
  Chunks c;

  c << F("<!DOCTYPE html><html lang='fr'><head><meta charset='UTF-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>MODELROGS Display</title>")
    << FPSTR(STYLE) << F("</head><body><h1>MODELROGS Display</h1>");

  // --- État
  bool sta = staConnected();
  c << F("<section><h2>État</h2><div class='kv'>")
    << F("<b>Point d'accès</b><span>") << esc(cfg.apSsid) << F(" — ") << WiFi.softAPIP().toString() << F("</span>")
    << F("<b>Wi-Fi maison</b><span class='") << (sta ? "ok'>Connecté — " : "warn'>Non connecté")
    << (sta ? WiFi.localIP().toString() + " (" + String(WiFi.RSSI()) + " dBm)" : String("")) << F("</span>")
    << F("<b>Passerelle</b><span class='") << (gwLastCode == 200 ? "ok" : "warn") << F("'>")
    << gwErrorText(gwLastCode);
  if (gMods.known) c << F(" — version ") << esc(gMods.version);
  if (gwLastOkMs) c << F(" (dernière réponse il y a ") << (long)((millis() - gwLastOkMs) / 1000) << F(" s)");
  c << F("</span>");
  if (timeValid()) c << F("<b>Heure</b><span>") << hhmm(localMinuteOfDay()) << F("</span>");
  c << F("<b>Mémoire libre</b><span>") << (long)ESP.getFreeHeap() << F(" o (fragmentation ")
    << (long)ESP.getHeapFragmentation() << F(" %)</span>")
    << F("<b>Flash</b><span>") << (long)(ESP.getFlashChipRealSize() / 1048576) << F(" Mo — firmware ")
    << (long)(ESP.getSketchSize() / 1024) << F(" Ko — place libre pour l'OTA ")
    << (long)(ESP.getFreeSketchSpace() / 1024) << F(" Ko</span>")
    << F("<b>Fonctionne depuis</b><span>") << (long)(millis() / 60000) << F(" min</span>")
    << F("<b>Version</b><span>") << FW_VERSION << F("</span></div>");

  c << F("<p class='note'>Afficher maintenant :</p>");
  for (uint8_t p = 0; p < PG_COUNT; p++) {
    if (!moduleUsable(p)) continue;
    c << F("<a class='sm' href='/show?p=") << (long)p << F("'>") << PAGE_NAMES[p] << F("</a>");
  }
  c << F("</section>");

  // --- Formulaire
  c << F("<form action='/save' method='POST'>");

  c << F("<section><h2>Wi-Fi maison</h2><label>Nom du réseau (SSID)</label>"
         "<input type='text' name='wifi_ssid' maxlength='32' value='") << esc(cfg.wifiSsid)
    << F("'><label>Mot de passe</label><input type='password' name='wifi_pass' maxlength='64' "
         "placeholder='inchangé si vide' autocomplete='new-password'>"
         "<p class='note'>Laisser vide pour conserver le mot de passe actuel. "
         "Si vous changez de réseau, un champ vide signifie « réseau ouvert ».</p></section>");

  c << F("<section><h2>Point d'accès de l'afficheur</h2><label>Nom du point d'accès (ouvert, sans mot de passe)</label>"
         "<input type='text' name='ap_ssid' maxlength='32' value='") << esc(cfg.apSsid) << F("'></section>");

  c << F("<section><h2>Général</h2><label>Adresse de la passerelle</label>"
         "<input type='text' name='gw' maxlength='95' placeholder='http://192.168.1.44:8080' value='")
    << esc(cfg.gateway) << F("'><label>Ville (heure, météo, lever et coucher du soleil)</label>"
         "<input type='text' name='ville' maxlength='47' placeholder='Paris' value='") << esc(cfg.ville) << F("'></section>");

  renderModules(c);

  c << F("<section><h2>Nuit et luminosité</h2>"
         "<label class='chk'><input type='checkbox' name='nuit'") << (cfg.nightOn ? F(" checked") : F(""))
    << F(">Activer la plage nocturne</label><div class='row'><div><label>Début</label>"
         "<input type='time' name='nd' value='") << hhmm(cfg.nightStart)
    << F("'></div><div><label>Fin</label><input type='time' name='nf' value='") << hhmm(cfg.nightEnd)
    << F("'></div></div><label>Luminosité de jour : <output id='oj'>") << (long)cfg.brightDay
    << F("</output> %</label><input type='range' name='lj' min='5' max='100' value='") << (long)cfg.brightDay
    << F("' oninput='oj.value=this.value'><label>Luminosité de nuit : <output id='on'>") << (long)cfg.brightNight
    << F("</output> % (0 = écran éteint)</label><input type='range' name='ln' min='0' max='100' value='")
    << (long)cfg.brightNight << F("' oninput='on.value=this.value'></section>");

  c << F("<button type='submit'>Enregistrer</button></form>"
         "<section><h2>Mise à jour du firmware</h2><p>OTA : <a href='/update'>/update</a> "
         "(fichier .bin ou .bin.gz)</p></section></body></html>");
  c.flush();
  server.sendContent("");   // fin de la réponse
}

// ------------------------------------------------------------ enregistrer

// Reconstruit « cle=valeur&cle2=v1,v2 » à partir des champs p<page>_<cle>.
static void saveModuleParams(uint8_t page) {
  const uint8_t MAX_KEYS = 6;
  String keys[MAX_KEYS], vals[MAX_KEYS];
  uint8_t nk = 0;
  String prefix = String("p") + page + "_";

  for (int i = 0; i < server.args(); i++) {
    String name = server.argName(i);
    if (!name.startsWith(prefix)) continue;
    String key = name.substring(prefix.length());
    String v = server.arg(i);
    v.trim();
    uint8_t k = 0;
    while (k < nk && keys[k] != key) k++;
    if (k == nk) {
      if (nk == MAX_KEYS) continue;
      keys[nk++] = key;
    }
    if (!v.length()) continue;
    if (vals[k].length()) vals[k] += ',';
    vals[k] += v;
  }

  String qs;
  for (uint8_t k = 0; k < nk; k++) {
    if (!vals[k].length()) continue;
    if (qs.length()) qs += '&';
    qs += qsEncode(keys[k]) + "=" + qsEncode(vals[k]);
  }

  size_t size = modParamsSize(page);
  if (qs.length() >= size) {           // trop long : on coupe proprement à la dernière valeur
    qs.remove(size - 1);
    int cut = max(qs.lastIndexOf(','), qs.lastIndexOf('&'));
    if (cut > 0) qs.remove(cut);
  }
  strncpy(modParams(page), qs.c_str(), size - 1);
  modParams(page)[size - 1] = '\0';
}

static void handleSave() {
  bool wifiChanged = false, apChanged = false, villeChanged = false;

  String ssid = server.arg("wifi_ssid");
  ssid.trim();
  String pass = server.arg("wifi_pass");
  if (ssid != cfg.wifiSsid) {
    copyArg("wifi_ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
    copyArg("wifi_pass", cfg.wifiPass, sizeof(cfg.wifiPass));   // vide = réseau ouvert
    wifiChanged = true;
  } else if (pass.length()) {
    copyArg("wifi_pass", cfg.wifiPass, sizeof(cfg.wifiPass));
    wifiChanged = true;
  }

  String ap = server.arg("ap_ssid");
  ap.trim();
  if (ap.length() && ap != cfg.apSsid) {
    copyArg("ap_ssid", cfg.apSsid, sizeof(cfg.apSsid));
    apChanged = true;
  }

  String gw = server.arg("gw");
  gw.trim();
  while (gw.endsWith("/")) gw.remove(gw.length() - 1);
  if (gw.length() && !gw.startsWith("http://")) gw = "http://" + gw;
  strncpy(cfg.gateway, gw.c_str(), sizeof(cfg.gateway) - 1);
  cfg.gateway[sizeof(cfg.gateway) - 1] = '\0';

  String ville = server.arg("ville");
  ville.trim();
  if (ville != cfg.ville) villeChanged = true;
  copyArg("ville", cfg.ville, sizeof(cfg.ville));

  for (uint8_t p = 0; p < PG_COUNT; p++) {
    // Seules les pages affichées dans le formulaire sont modifiées.
    if (server.hasArg(String("pk") + p)) {
      if (server.hasArg(String("pg") + p)) cfg.pages |= 1 << p;
      else cfg.pages &= ~(1 << p);
    }
    if (server.hasArg(String("pm") + p)) saveModuleParams(p);
  }
  if (server.hasArg("duree")) cfg.pageSec = constrain(server.arg("duree").toInt(), 5, 120);

  cfg.nightOn = server.hasArg("nuit");
  cfg.nightStart = parseHhmm(server.arg("nd"), cfg.nightStart);
  cfg.nightEnd = parseHhmm(server.arg("nf"), cfg.nightEnd);
  if (server.hasArg("lj")) cfg.brightDay = constrain(server.arg("lj").toInt(), 5, 100);
  if (server.hasArg("ln")) cfg.brightNight = constrain(server.arg("ln").toInt(), 0, 100);

  saveConfig();

  String msg = apChanged
    ? F("Le point d'accès a changé de nom : reconnectez-vous au nouveau Wi-Fi, puis rouvrez 192.168.4.1.")
    : F("Retour à la configuration dans quelques secondes...");
  server.send(200, F("text/html; charset=utf-8"),
    String(F("<!DOCTYPE html><html lang='fr'><head><meta charset='UTF-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<meta http-equiv='refresh' content='4;url=/'><title>MODELROGS</title>"))
    + FPSTR(STYLE) + F("</head><body><section><h2>Configuration enregistrée</h2><p>") + msg
    + F("</p></section></body></html>"));

  // Appliqué APRÈS la réponse : l'AP n'est redémarré que si son nom change,
  // et les routes du serveur ne sont jamais réenregistrées.
  delay(50);
  netApplyConfig(wifiChanged, apChanged);
  schedulerConfigChanged(villeChanged);
}

static void handleShow() {
  int p = server.arg("p").toInt();
  if (p >= 0 && p < PG_COUNT) schedulerForce(p);
  server.sendHeader("Location", "/", true);
  server.send(303, F("text/plain"), "");
}

// Portail captif : toute adresse inconnue renvoie vers la configuration,
// sauf si l'on s'adresse directement à l'afficheur (vraie page introuvable).
static void handleNotFound() {
  String host = server.hostHeader();
  if (host == WiFi.softAPIP().toString() || (staConnected() && host == WiFi.localIP().toString())) {
    server.send(404, F("text/plain; charset=utf-8"), F("MODELROGS : page introuvable"));
    return;
  }
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, F("text/plain; charset=utf-8"), F("Redirection vers MODELROGS Display"));
}

// --------------------------------------------------------------- démarrage

void webBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/show", HTTP_GET, handleShow);

  // OTA : même mécanisme qu'avant (ESP8266HTTPUpdateServer sur /update),
  // enregistré UNE seule fois.
  httpUpdater.setup(&server, "/update");

  server.onNotFound(handleNotFound);
  server.begin();

  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println(F("HTTP + DNS : OK — OTA : http://192.168.4.1/update"));
}

void webTick() {
  dnsServer.processNextRequest();
  server.handleClient();
}
