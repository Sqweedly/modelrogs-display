#include "web.h"
#include "app.h"
#include "config.h"
#include "diag.h"
#include "memo.h"
#include "net.h"
#include "sante.h"

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

// Interface 3.0.0 : 5 onglets (Pages, Écran, Réseau, Système, Diagnostic), une
// adresse chacun, conforme à la maquette validée (docs/maquettes/interface-web-3.0.0).
// color-scheme: dark : les contrôles natifs (icônes horloge des champs d'heure)
// sont dessinés pour un fond sombre (backlog n° 5).
static const char STYLE[] PROGMEM =
  "<style>"
  ":root{--bg:#0B0E13;--card:#151A23;--line:#232A36;--txt:#EEF1F6;--mut:#8E97A8;--acc:#8B7CFF;"
  "--ok:#3DD68C;--warn:#FFB547;--ko:#FF6B70;color-scheme:dark}"
  "*{box-sizing:border-box}"
  "body{font:15px/1.4 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--txt);"
  "margin:0 auto;max-width:520px;padding:0 0 160px}"
  ".top{padding:22px 18px 10px}.titre{display:flex;align-items:center;gap:10px}"
  ".logo{width:34px;height:34px;border-radius:10px;background:linear-gradient(135deg,#8B7CFF,#5B4BDB);"
  "display:grid;place-items:center;font-weight:700;font-size:15px;flex:none}"
  ".titre h1{font-size:19px;margin:0;font-weight:650}"
  ".titre small{display:block;color:var(--mut);font-size:12px;font-weight:400}"
  ".puces{display:flex;gap:6px;margin-top:12px;flex-wrap:wrap}"
  ".puce{display:inline-flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--line);"
  "border-radius:999px;padding:4px 10px;font-size:12px;color:var(--mut)}"
  ".puce i,.pt{width:7px;height:7px;border-radius:50%;background:var(--ok);display:inline-block}"
  ".pt{width:6px;height:6px;margin-right:4px}.puce.w i,.pt.w{background:var(--warn)}.pt.off{background:#4A5366}"
  ".corps{padding:4px 14px}"
  "h2{font-size:12px;text-transform:uppercase;letter-spacing:1.2px;color:var(--mut);margin:16px 6px 8px;font-weight:600}"
  ".carte{background:var(--card);border:1px solid var(--line);border-radius:16px;overflow:hidden}"
  ".carte>*+*{border-top:1px solid var(--line)}"
  ".ligne{display:flex;align-items:center;gap:12px;padding:12px 14px;min-height:52px}"
  "summary.ligne{list-style:none;cursor:pointer}summary::-webkit-details-marker{display:none}"
  ".ico{width:32px;height:32px;border-radius:9px;display:grid;place-items:center;font-size:16px;flex:none}"
  ".lib{flex:1;min-width:0}.lib b{display:block;font-weight:550}"
  ".lib span{display:block;font-size:12px;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
  ".chev{color:#4A5366;font-size:18px}.ro{color:var(--txt);font-variant-numeric:tabular-nums}"
  ".sw{position:relative;width:46px;height:28px;flex:none;display:block}"
  ".sw input{opacity:0;position:absolute;width:0;height:0}"
  ".sw b{position:absolute;inset:0;background:#2A3140;border-radius:999px;transition:.2s}"
  ".sw b:after{content:'';position:absolute;left:3px;top:3px;width:22px;height:22px;border-radius:50%;"
  "background:#fff;transition:.2s}"
  ".sw input:checked+b{background:var(--acc)}.sw input:checked+b:after{left:21px}.sw input:disabled+b{opacity:.35}"
  ".champ{padding:12px 14px}"
  ".champ label,.ext label{display:block;font-size:12px;color:var(--mut);margin:0 0 6px}"
  ".ext label{margin:10px 0 6px}"
  "input[type=text],input[type=password],input[type=time],select{width:100%;padding:11px 12px;background:#0F131A;"
  "color:var(--txt);border:1px solid var(--line);border-radius:10px;font:inherit}"
  ".deux{display:grid;grid-template-columns:1fr 1fr;gap:10px}.pile{display:grid;gap:8px}"
  "input[type=range]{width:100%;accent-color:var(--acc)}"
  ".val{float:right;color:var(--txt);font-variant-numeric:tabular-nums}"
  ".ext{background:#121722;padding:4px 14px 12px}"
  ".note{font-size:12px;color:var(--mut);margin:8px 6px 0;line-height:1.45}.ext .note{margin:8px 0 0}"
  ".lab{font-size:12px;color:var(--mut);margin:10px 0 0}"
  ".btn{display:block;width:100%;padding:13px;border:0;border-radius:12px;background:var(--acc);color:#fff;"
  "font:inherit;font-weight:600;text-align:center;text-decoration:none}.btn.g{background:#232A36;color:var(--txt)}"
  ".sm{display:inline-block;padding:7px 11px;margin:10px 6px 0 0;font-size:13px;background:#232A36;"
  "color:var(--txt);border-radius:9px;text-decoration:none;border:0}"
  ".chk{display:flex;align-items:center;gap:10px;margin:8px 0;font-size:14px}.chk input{width:18px;height:18px}"
  ".grid{display:grid;grid-template-columns:1fr 1fr;gap:2px 12px}"
  "fieldset{border:0;padding:0;margin:0;min-width:0}fieldset[disabled]{opacity:.4;pointer-events:none}"
  ".kv{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;font-size:13px;padding:12px 14px}"
  ".kv b{color:var(--mut);font-weight:500}"
  ".tuiles{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;text-align:center;padding:12px 14px}"
  ".tuiles b{font-size:18px}.tuiles span{display:block;font-size:12px;color:var(--mut)}"
  "table{width:100%;border-collapse:collapse;font-size:12.5px;font-variant-numeric:tabular-nums}"
  "td{padding:8px 14px;border-top:1px solid var(--line);text-align:right;white-space:nowrap}"
  "tr:first-child td{border-top:0}td.g{text-align:left;white-space:normal}"
  ".ok{color:var(--ok)}.warn{color:var(--warn)}.ko{color:var(--ko)}a{color:var(--acc)}"
  ".alerte{border-color:var(--ko)}.alerte.w{border-color:var(--warn)}.alerte p{margin:0;padding:12px 14px}"
  ".enreg{position:fixed;bottom:88px;left:50%;transform:translateX(-50%);width:min(492px,calc(100% - 28px))}"
  ".onglets{position:fixed;left:50%;transform:translateX(-50%);bottom:0;width:100%;max-width:520px;height:78px;"
  "background:rgba(14,18,25,.96);border-top:1px solid var(--line);display:flex;padding:8px 6px 18px}"
  ".onglets a{flex:1;text-align:center;color:#6C7588;font-size:10.5px;text-decoration:none}"
  ".onglets svg{display:block;margin:0 auto 3px;width:24px;height:24px;stroke:currentColor;fill:none;"
  "stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.onglets a.on{color:var(--acc)}"
  "</style>";

// n-ième élément d'une liste « a|b|c » rangée en flash
static String element(PGM_P liste, uint8_t n) {
  String tout = FPSTR(liste);
  int debut = 0;
  for (uint8_t i = 0; i < n && debut >= 0; i++) {
    debut = tout.indexOf('|', debut);
    if (debut >= 0) debut++;
  }
  if (debut < 0) return "";
  int fin = tout.indexOf('|', debut);
  return tout.substring(debut, fin < 0 ? tout.length() : fin);
}

// Feuille de style envoyée directement depuis la flash (5 Ko : pas de copie en RAM)
static void envoyerStyle(Chunks& c) {
  c.flush();
  server.sendContent_P(STYLE);
}

// --------------------------------------------------------- cadre des pages

enum Onglet : uint8_t { OG_PAGES, OG_ECRAN, OG_RESEAU, OG_SYSTEME, OG_DIAG, OG_NB };

static const char OG_ICONES[] PROGMEM =   // 5 icônes séparées par '|'
  "<rect x='3' y='3' width='7' height='7' rx='2'/><rect x='14' y='3' width='7' height='7' rx='2'/>"
  "<rect x='3' y='14' width='7' height='7' rx='2'/><rect x='14' y='14' width='7' height='7' rx='2'/>|"
  "<circle cx='12' cy='12' r='4'/><path d='M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2"
  "M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4'/>|"
  "<path d='M2 9a15 15 0 0 1 20 0M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0'/><circle cx='12' cy='19.5' r='1'/>|"
  "<circle cx='12' cy='12' r='3'/><path d='M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0"
  "-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1"
  "-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0"
  "-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0"
  " 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0"
  " 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z'/>|"
  "<path d='M3 12h4l2-6 4 12 2-6h6'/>";
static const char OG_NOMS[] PROGMEM = "Pages|Écran|Réseau|Système|Diagnostic";
static const char OG_LIENS[] PROGMEM = "/|/ecran|/reseau|/systeme|/debug";

// Début de page : en-tête (logo, titre, sous-titre). Envoi par morceaux.
static void debutPage(Chunks& c, const char* titre, const String& sousTitre) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html; charset=utf-8"), "");
  c << F("<!DOCTYPE html><html lang='fr'><head><meta charset='UTF-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'><title>")
    << titre << F(" — MODELROGS</title>");
  envoyerStyle(c);
  c << F("</head><body><div class='top'><div class='titre'><div class='logo'>M</div><h1>") << titre
    << F("<small>") << esc(sousTitre) << F("</small></h1></div>");
}

// Fin de page : barre d'onglets en bas, onglet courant en couleur.
static void finPage(Chunks& c, Onglet actif) {
  String icones = FPSTR(OG_ICONES);
  int debut = 0;
  c << F("<nav class='onglets'>");
  for (uint8_t i = 0; i < OG_NB; i++) {
    int fin = icones.indexOf('|', debut);
    if (fin < 0) fin = icones.length();
    c << F("<a href='") << element(OG_LIENS, i) << (i == actif ? F("' class='on'>") : F("'>"))
      << F("<svg viewBox='0 0 24 24'>") << icones.substring(debut, fin) << F("</svg>") << element(OG_NOMS, i)
      << F("</a>");
    debut = fin + 1;
  }
  c << F("</nav></body></html>");
  c.flush();
  server.sendContent("");   // fin de la réponse
}

// Interrupteur ; nom vide = sans champ de formulaire
static void interrupteur(Chunks& c, const String& nom, bool on, bool actif = true) {
  c << F("<label class='sw'><input type='checkbox'");
  if (nom.length()) c << F(" name='") << nom << F("'");
  c << (on ? F(" checked") : F("")) << (actif ? F("") : F(" disabled")) << F("><b></b></label>");
}

// Interrupteur dessiné seulement (dans un lien : c'est le lien qui agit)
static void interrupteurVisuel(Chunks& c, bool on) {
  c << F("<span class='sw' style='pointer-events:none'><input type='checkbox' tabindex='-1'")
    << (on ? F(" checked") : F("")) << F("><b></b></span>");
}

// Curseur avec sa valeur affichée à droite du libellé
static void curseur(Chunks& c, const char* nom, const char* libelle, int mini, int maxi, int pas, int valeur,
                    const char* unite) {
  c << F("<div class='champ'><label>") << libelle << F(" <span class='val'><output id='o") << nom << F("'>")
    << (long)valeur << F("</output> ") << unite << F("</span></label><input type='range' name='") << nom
    << F("' min='") << (long)mini << F("' max='") << (long)maxi << F("' step='") << (long)pas << F("' value='")
    << (long)valeur << F("' oninput='o") << nom << F(".value=this.value'></div>");
}

static String duree(uint32_t secondes) {
  uint32_t m = secondes / 60;
  if (m < 60) return String(m) + F(" min");
  return String(m / 60) + F(" h ") + (m % 60 < 10 ? F("0") : F("")) + String(m % 60);
}

// --------------------------------------------- champs décrits par la passerelle

// Liste dynamique {"ok":1,"s":[{"id","n"}]} lue sur la passerelle.
static int fetchList(const char* src, JsonDocument& list) {
  JsonDocument filter;
  filter["ok"] = true;
  filter["err"] = true;
  filter["s"][0]["id"] = true;
  filter["s"][0]["n"] = true;
  return gwGet(src, list, &filter, true);
}

static void erreurListe(Chunks& c, int code, JsonDocument& list) {
  c << F("<p class='warn'>Liste indisponible : ")
    << (code == -5 ? esc(list["err"] | "erreur") : String(gwErrorText(code)))
    << F(". Réglage actuel conservé.</p>");
}

// Affiche un champ ; false si impossible (liste injoignable, type inconnu) :
// les réglages de ce module ne seront alors pas modifiés à l'enregistrement.
// optionsModule : liste commune aux listes déroulantes du module (pages de cours).
static bool renderField(Chunks& c, uint8_t page, JsonObject param, JsonArray optionsModule) {
  const char* key = param["k"] | "";
  const char* type = param["t"] | "";
  if (!key[0]) return false;
  String name = fieldName(page, key);
  String value = modParam(page, key);
  if (!value.length()) value = (const char*)(param["d"] | "");
  const char* ph = param["ph"] | "";

  const char* libelle = param["l"] | "";
  if (libelle[0]) c << F("<label>") << esc(libelle) << F("</label>");

  if (strcmp(type, "text") == 0) {
    c << F("<input type='text' name='") << name << F("' value='") << esc(value) << F("'");
    if (ph[0]) c << F(" placeholder='") << esc(ph) << F("'");
    c << F(">");
    return true;
  }

  JsonDocument list;
  JsonArray options = param["o"].as<JsonArray>();
  if (options.isNull() && !param["src"].is<const char*>()) options = optionsModule;
  if (options.isNull()) {
    const char* src = param["src"] | "";
    int code = src[0] ? fetchList(src, list) : -2;
    if (code != 200) {
      erreurListe(c, code, list);
      return false;
    }
    options = list["s"].as<JsonArray>();
  }

  if (strcmp(type, "select") == 0) {
    c << F("<select name='") << name << F("'><option value=''>") << (ph[0] ? esc(ph) : String(F("— choisir —")))
      << F("</option>");
    bool trouve = false;
    for (JsonObject o : options) {
      String id = (const char*)(o["id"] | "");
      trouve |= id == value;
      c << F("<option value='") << esc(id) << F("'") << (id == value ? F(" selected") : F("")) << F(">")
        << esc(o["n"] | id.c_str()) << F("</option>");
    }
    // Valeur choisie qui n'est plus proposée (ex. crypto sortie du top 10) : gardée
    if (value.length() && !trouve)
      c << F("<option value='") << esc(value) << F("' selected>") << esc(value) << F("</option>");
    c << F("</select>");
    return true;
  }

  if (strcmp(type, "multi") == 0) {
    String selected = "," + value + ",";
    String cls = String("m") + page;
    // Option « tous » (proposée par la passerelle) : valeur « * », active aussi
    // quand rien n'est encore choisi. La liste est alors grisée et non envoyée.
    const char* tous = param["tous"] | "";
    bool tousOn = tous[0] && (!value.length() || selected.indexOf(",*,") >= 0);
    if (tous[0]) {
      c << F("<label class='chk'><input type='checkbox' name='") << name << F("' value='*'")
        << (tousOn ? F(" checked") : F("")) << F(" onchange=\"document.getElementById('f") << cls
        << F("').disabled=this.checked\">") << esc(tous) << F("</label>");
    }
    c << F("<fieldset id='f") << cls << F("'") << (tousOn ? F(" disabled") : F("")) << F(">");
    c << F("<p class='note'>") << (long)options.size() << F(" choix");
    if (param["max"].is<int>()) c << F(" — ") << (long)(param["max"] | 0) << F(" au maximum");
    if (tous[0]) c << F(". Si aucun n'est coché, tous sont affichés");
    // Plafond (« max ») : une case de plus est refusée ; « Tout cocher » masqué
    // quand il dépasserait le plafond.
    long maxi = param["max"] | 0;
    bool plafond = maxi > 0 && (long)options.size() > maxi;
    c << F("</p>");
    if (!plafond) {
      c << F("<a class='sm' href='#' onclick=\"document.querySelectorAll('.") << cls
        << F("').forEach(e=>e.checked=true);return false\">Tout cocher</a>");
    }
    c << F("<a class='sm' href='#' onclick=\"document.querySelectorAll('.") << cls
      << F("').forEach(e=>e.checked=false);return false\">Tout décocher</a><div class='grid' style='margin-top:8px'>");
    String limite;
    if (plafond) {
      limite = String(F(" onchange=\"if(document.querySelectorAll('.")) + cls + F(":checked').length>") +
               maxi + F(")this.checked=false\"");
    }
    for (JsonObject o : options) {
      String id = (const char*)(o["id"] | "");
      bool on = selected.indexOf("," + id + ",") >= 0;
      c << F("<label class='chk'><input class='") << cls << F("' type='checkbox' name='") << name
        << F("' value='") << esc(id) << F("'") << (on ? F(" checked") : F("")) << limite << F(">")
        << esc(o["n"] | id.c_str()) << F("</label>");
    }
    c << F("</div></fieldset>");
    return true;
  }

  c << F("<p class='warn'>Type de réglage non pris en charge par ce firmware.</p>");
  return false;
}

// ------------------------------------------------------------ diagnostic
// Page /debug (lecture humaine) et /debug.json : compteurs toujours tenus,
// journal des dernières requêtes en mode diagnostic (voir diag.h).
// /debug?on=1 active le mode, /debug?off=1 l'arrête (mémoire vive seulement).

static String ilYa(uint32_t quandMs) {
  if (!quandMs) return F("jamais");
  uint32_t s = (millis() - quandMs) / 1000;
  if (s < 120) return String(F("il y a ")) + s + F(" s");
  if (s < 7200) return String(F("il y a ")) + (s / 60) + F(" min");
  return String(F("il y a ")) + (s / 3600) + F(" h");
}

static bool basculerDiag() {
  if (server.arg("on") == "1") return diagActiver(true);
  if (server.arg("off") == "1") diagActiver(false);
  return true;
}

// ----------------------------------------------------------- onglet Pages

// Icône et couleur de fond de chaque page (ordre des PageId)
// Heure, Météo, Discord, Supervision, Crypto, Plex, Actions, Devises, Commodities,
// Indices, Custom, Photos, Tesla
// Une chaîne par page, séparée par '|' (en flash)
static const char PG_ICONES[] PROGMEM = "🕒|⛅|🎧|📡|₿|▶|📈|💱|🛢|📊|⭐|🖼|🚗";
static const char PG_FONDS[] PROGMEM = "#2B2550|#16324A|#24284A|#13372A|#3A2A12|#3A3212|#13372A|#24284A|#3A2F12|#16324A|#2B2550|#3A1D33|#3A1A1E";
static_assert(PG_COUNT == 13, "PG_ICONES et PG_FONDS : une entrée par page");


// Ligne sous le nom des pages sans valeurs à choisir (pastille verte devant)
static String phraseFixe(uint8_t p) {
  switch (p) {
    case PG_DISCORD: return F("si quelqu'un est en salon");
    case PG_PLEX:    return F("pendant une lecture");
    case PG_TESLA:   return F("prioritaire en charge");
    case PG_SUP:     return F("en cas de panne ou d'instabilité");
    default:         return "";
  }
}

// Personnage du domaine (3.1.0) : nom court (ligne sous le nom de la page) et
// explication (réglage dans la page dépliée)
static const __FlashStringHelper* persoNom(uint8_t p) {
  switch (p) {
    case PG_HEURE: return F("Nimbus, Pousse");
    case PG_TESLA: return F("Volta");
    case PG_SUP:   return F("Bip");
    default:       return nullptr;
  }
}

static void renderPerso(Chunks& c, uint8_t p) {
  if (!persoNom(p)) return;
  c << F("<input type='hidden' name='pp") << (long)p << F("' value='1'><div class='ligne'><div class='lib'><b>Personnage</b><span>");
  switch (p) {
    case PG_HEURE: c << F("Nimbus (ciel) et Pousse (température)"); break;
    case PG_TESLA: c << F("Volta (batterie)"); break;
    default:       c << F("Bip, dans le bloc de panne"); break;
  }
  c << F("</span></div>");
  interrupteur(c, String("ps") + p, persoActif(p));
  c << F("</div>");
}

// Libellé d'une valeur choisie, d'après les options connues sans requête
static String libelleOption(JsonArray options, const String& id) {
  for (JsonObject o : options) {
    if (id == (const char*)(o["id"] | "")) {
      String n = (const char*)(o["n"] | id.c_str());
      int i = n.indexOf(" (");                  // « Bitcoin (BTC) » -> « Bitcoin »
      return i > 0 ? n.substring(0, i) : n;
    }
  }
  return "";
}

// Pages de cours au format compact (passerelle 3.2.0, /modules?f=2) : la
// passerelle n'envoie que « f »: « c » et l'exemple de saisie « ph » ; les
// 3 listes, la saisie et la note sont construites ici, à l'identique de ce que
// les passerelles plus anciennes envoient dans « p » et « nt » (même HTML).
static bool pageDeCours(JsonObject mod) { return strcmp(mod["f"] | "", "c") == 0; }

static JsonArray champsModule(JsonObject mod, JsonDocument& tmp) {
  // Autres modules : champs envoyés par la passerelle (absents au format compact s'ils sont vides)
  if (!pageDeCours(mod)) return mod["p"].as<JsonArray>();
  JsonArray a = tmp.to<JsonArray>();
  for (uint8_t i = 1; i <= 3; i++) {
    JsonObject o = a.add<JsonObject>();
    o["k"] = String('s') + i;
    o["t"] = "select";
    o["g"] = 1;
    o["ph"] = String(i) + ". —";
  }
  JsonObject m = a.add<JsonObject>();
  m["k"] = "m";
  m["t"] = "text";
  m["g"] = 1;
  m["ph"] = (const char*)(mod["ph"] | "");
  return a;
}

// Résumé des réglages d'une page (« Bitcoin, Ethereum, Solana », « Au hasard · 30 s »)
static String resumeReglages(uint8_t p, JsonArray champs, JsonObject mod, JsonArray optionsModule) {
  String r;
  for (JsonObject param : champs) {
    const char* key = param["k"] | "";
    String v = modParam(p, key);
    if (!v.length()) v = (const char*)(param["d"] | "");
    String l;
    if (strcmp(param["t"] | "", "select") == 0) {
      JsonArray o = param["o"].as<JsonArray>();
      if (o.isNull() && !param["src"].is<const char*>()) o = optionsModule;
      if (v.length()) l = o.isNull() ? String("") : libelleOption(o, v);
      else if (param["src"].is<const char*>()) l = (const char*)(param["ph"] | "");
    } else if (strcmp(param["t"] | "", "text") == 0) {
      l = v;
    }
    if (!l.length()) continue;
    if (r.length()) r += param["g"] | 0 ? ", " : " · ";
    r += l;
  }
  if (!r.length() && mod["df"].is<const char*>()) r = (const char*)(mod["df"] | "");
  return r;
}

// Mémoire pendant la construction de l'onglet Pages (3.3.0, /debug « pages ») :
// c'est là que /modules entier, les champs construits et une liste sont en
// mémoire ensemble. Relevé après la lecture de /modules et à chaque page.
struct MesurePages {
  uint32_t ouvertures = 0, octets = 0;
  uint32_t tas = 0, bloc = 0;                       // dernière ouverture : minimums
  uint32_t tasMin = UINT32_MAX, blocMin = UINT32_MAX;   // depuis le démarrage
};
static MesurePages gMesurePages;
static uint32_t mesTas, mesBloc;

static void mesurer() {
  mesTas = min(mesTas, (uint32_t)ESP.getFreeHeap());
  mesBloc = min(mesBloc, (uint32_t)ESP.getMaxFreeBlockSize());
}

static void renderModules(Chunks& c) {
  JsonDocument man;
  // 3.5.1 : les réponses gardées 10 min (memo.h, ~4 Ko relevés en 3.5.0) sont
  // rendues avant de construire l'onglet, son moment le plus gourmand en
  // mémoire ; chaque page concernée les redemande à son prochain affichage.
  memoVider();
  mesTas = mesBloc = UINT32_MAX;
  int code = gwGet("/modules?f=2", man, nullptr, true);   // ?f=2 : voir champsModule
  // Taille annoncée de /modules, relevée tout de suite : les listes demandées
  // ensuite pour chaque page deviennent à leur tour « la dernière réponse »
  const uint32_t octetsModules = code == 200 ? diagDerniersOctets() : 0;
  mesurer();
  auto trouver = [&](uint8_t p) {
    JsonObject mod;
    if (code == 200)
      for (JsonObject m : man["m"].as<JsonArray>())
        if (strcmp(m["id"] | "", PAGE_IDS[p]) == 0) mod = m;
    return mod;
  };
  uint8_t actives = 0;
  for (uint8_t p = 0; p < PG_COUNT; p++)
    if (pageEnabled(p) && (code != 200 || !trouver(p).isNull())) actives++;

  if (code != 200) {
    c << F("<div class='carte alerte w' style='margin-top:12px'><p class='warn'>Modules de la passerelle inconnus : ")
      << gwErrorText(code) << F(". Les réglages des pages sont conservés.</p></div>");
  }
  c << F("<h2>Rotation · ") << (long)actives << (actives > 1 ? F(" pages actives") : F(" page active"))
    << F("</h2><div class='carte'>");
  c.flush();

  for (uint8_t i = 0; i < PG_COUNT; i++) {
    const uint8_t p = ROTATION[i];
    JsonObject mod = trouver(p);
    const bool absent = code == 200 && mod.isNull();
    const bool configure = !mod.isNull() && (mod["ok"] | 0);

    // Champs de réglage (construits ici pour une page de cours au format compact)
    JsonDocument champsDoc;
    JsonArray champs = mod.isNull() ? JsonArray() : champsModule(mod, champsDoc);

    // Liste commune aux listes déroulantes du module (pages de cours)
    JsonDocument listeModule;
    JsonArray optionsModule = configure ? mod["o"].as<JsonArray>() : JsonArray();
    int codeListe = 200;
    if (configure && optionsModule.isNull() && mod["src"].is<const char*>()) {
      codeListe = fetchList(mod["src"] | "", listeModule);
      if (codeListe == 200) optionsModule = listeModule["s"].as<JsonArray>();
    }

    // Ligne d'état sous le nom
    String ligne, pastille;
    if (absent) { ligne = F("module absent de la passerelle"); pastille = F("off"); }
    else if (!mod.isNull() && !configure) { ligne = String(F("non configuré : ")) + (const char*)(mod["err"] | ""); pastille = F("w"); }
    else if (p == PG_HEURE) ligne = F("toujours active");
    else if (p == PG_METEO) { if (mod["v"].is<const char*>()) ligne = String((const char*)(mod["v"] | "")) + F(" · 5 jours"); }
    else if (phraseFixe(p).length()) { ligne = phraseFixe(p); pastille = F("on"); }
    else if (configure) ligne = resumeReglages(p, champs, mod, optionsModule);
    if (!absent && persoNom(p) && persoActif(p)) {
      if (ligne.length()) ligne += F(" · ");
      ligne += persoNom(p);
    }

    c << (absent ? F("<div class='ligne'>") : F("<details><summary class='ligne'>"))
      << F("<div class='ico' style='background:") << element(PG_FONDS, p) << F("'>") << element(PG_ICONES, p)
      << F("</div><div class='lib'><b>") << PAGE_NAMES[p] << F("</b><span>");
    if (pastille == "on") c << F("<i class='pt'></i>");
    else if (pastille.length()) c << F("<i class='pt ") << pastille << F("'></i>");
    c << esc(ligne) << F("</span></div>");
    if (absent) {                       // réglage de page laissé tel quel
      interrupteur(c, "", false, false);
      c << F("</div>");
      continue;
    }
    if (p == PG_HEURE) {                // toujours active (config.cpp)
      interrupteur(c, "", true, false);
    } else {
      c << F("<input type='hidden' name='pk") << (long)p << F("' value='1'>");
      interrupteur(c, String("pg") + p, pageEnabled(p));
    }
    c << F("</summary><div class='ext'>");

    if (configure) {
      bool all = true;
      if (codeListe != 200) { erreurListe(c, codeListe, listeModule); all = false; }
      if (all) {
        bool grille = false;
        for (JsonObject param : champs) {
          bool g = param["g"] | 0;
          if (g && !grille) c << F("<label>Valeurs 1 à 3 (listes fournies par la passerelle)</label><div class='pile'>");
          if (!g && grille) c << F("</div>");
          grille = g;
          if (g && strcmp(param["t"] | "", "text") == 0)
            c << F("</div><label>Autre valeur (code, si absente des listes)</label><div class='pile'>");
          all &= renderField(c, p, param, optionsModule);
        }
        if (grille) c << F("</div>");
        const char* df = mod["df"] | "";
        if (df[0]) c << F("<p class='note'>Rien de choisi : valeurs du docker-compose (") << esc(df) << F(").</p>");
        const char* nt = mod["nt"] | "";
        if (nt[0]) c << F("<p class='note'>") << esc(nt) << F("</p>");
        else if (pageDeCours(mod)) c << F("<p class='note'>3 au plus, dans l&#39;ordre : listes 1 à 3, puis saisie</p>");
      }
      if (all) c << F("<input type='hidden' name='pm") << (long)p << F("' value='1'>");
      if (p == PG_SUP)
        c << F("<p class='note'>Tous les services de la page de statut Uptime Kuma sont suivis, "
               "y compris ceux ajoutés ou retirés ensuite.</p>");
    } else if (!mod.isNull()) {
      c << F("<p class='note'>À régler dans le docker-compose de la passerelle.</p>");
    }
    renderPerso(c, p);
    if (moduleUsable(p)) c << F("<a class='sm' href='/show?p=") << (long)p << F("'>Afficher maintenant</a>");
    c << F("</div></details>");
    mesurer();   // liste du module et champs encore en mémoire
    c.flush();
  }
  gMesurePages.ouvertures++;
  gMesurePages.octets = octetsModules;
  gMesurePages.tas = mesTas;
  gMesurePages.bloc = mesBloc;
  gMesurePages.tasMin = min(gMesurePages.tasMin, mesTas);
  gMesurePages.blocMin = min(gMesurePages.blocMin, mesBloc);
  c << F("</div><div class='carte' style='margin-top:12px'>");
  curseur(c, "duree", "Durée de chaque page", 5, 120, 5, cfg.pageSec, "s");
  c << F("</div><p class='note'>Discord, Plex et Tesla passent en tête quand quelqu'un arrive dans le salon, "
         "quand une lecture ou une charge commence. Une panne de supervision s'affiche aussitôt, puis après "
         "chaque page. Les personnages suivent leur page, le temps d'une page chacun. "
         "La nuit, seul l'écran de nuit est affiché (onglet Écran).</p>");
}

static void handleRoot() {
  Chunks c;
  debutPage(c, "MODELROGS Display", FW_VERSION);
  const bool sta = staConnected();
  c << F("<div class='puces'><span class='puce") << (sta ? F("'><i></i>Wi-Fi ") : F(" w'><i></i>Wi-Fi absent"));
  if (sta) c << (long)WiFi.RSSI() << F(" dBm");
  c << F("</span><span class='puce") << (gwLastCode == 200 ? F("'") : F(" w'")) << F("><i></i>Passerelle</span>");
  if (timeValid()) c << F("<span class='puce'><i></i>") << hhmm(localMinuteOfDay()) << F("</span>");
  c << F("</div></div><div class='corps'>");

  if (gSafeMode) {
    // L'OTA reste accessible en premier : c'est la fonction qui doit toujours marcher.
    c << F("<div class='carte alerte'><p class='ko'><b>Mode secours</b> : l'afficheur a planté plusieurs fois "
           "de suite au démarrage. Seuls le Wi-Fi, cette interface et la mise à jour tournent.</p></div>"
           "<div style='margin-top:12px'><a class='btn' href='/update'>Mettre à jour le firmware</a></div>"
           "<div style='margin-top:10px'><a class='btn g' href='/restart'>Redémarrer normalement</a></div></div>");
    finPage(c, OG_PAGES);
    return;
  }
  if (!cfg.validated) {
    c << F("<div class='carte alerte w' style='margin-top:12px'><p class='warn'><b>Configuration à valider</b> : "
           "nouveau firmware installé. Vérifiez les réglages, puis <b>Enregistrer</b> pour démarrer l'affichage.</p></div>");
  }
  c << F("<form action='/save' method='POST'><input type='hidden' name='o' value='pages'>");
  renderModules(c);
  c << F("<div class='enreg'><button class='btn' type='submit'>Enregistrer</button></div></form></div>");
  finPage(c, OG_PAGES);
}

// ----------------------------------------------------------- onglet Écran

static void handleEcran() {
  Chunks c;
  debutPage(c, "Écran", F("Nuit et luminosité"));
  c << F("</div><div class='corps'><form action='/save' method='POST'><input type='hidden' name='o' value='ecran'>"
         "<h2>Plage nocturne</h2><div class='carte'><div class='ligne'><div class='ico' style='background:#24284A'>"
         "🌙</div><div class='lib'><b>Luminosité de nuit</b><span>écran de nuit, luminosité réduite</span></div>");
  interrupteur(c, "nuit", cfg.nightOn);
  c << F("</div><div class='champ'><div class='deux'><div><label>Début</label><input type='time' name='nd' value='")
    << hhmm(cfg.nightStart) << F("'></div><div><label>Fin</label><input type='time' name='nf' value='")
    << hhmm(cfg.nightEnd) << F("'></div></div></div></div><h2>Luminosité</h2><div class='carte'>");
  curseur(c, "lj", "Jour", 5, 100, 1, cfg.brightDay, "%");
  curseur(c, "ln", "Nuit", 0, 100, 1, cfg.brightNight, "%");
  c << F("</div><p class='note'>Nuit à 0 % : écran éteint. Pendant la plage, l'écran n'affiche que l'heure ; "
         "une panne de supervision s'affiche à la luminosité de jour jusqu'au retour à la normale.</p>"
         "<div class='enreg'><button class='btn' type='submit'>Enregistrer</button></div></form></div>");
  finPage(c, OG_ECRAN);
}

// ---------------------------------------------------------- onglet Réseau

static uint8_t nbModules() {   // pages servies par la passerelle
  uint8_t n = 0;
  for (uint8_t p = 0; p < PG_COUNT; p++) n += (gMods.present >> p) & 1;
  return n;
}

static void handleReseau() {
  Chunks c;
  debutPage(c, "Réseau", F("Wi-Fi et passerelle"));
  const bool ok = gwLastCode == 200;
  c << F("</div><div class='corps'><form action='/save' method='POST'><input type='hidden' name='o' value='reseau'>"
         "<h2>Wi-Fi maison</h2><div class='carte'><div class='champ'><label>Nom du réseau (SSID)</label>"
         "<input type='text' name='wifi_ssid' maxlength='32' value='") << esc(cfg.wifiSsid)
    << F("'></div><div class='champ'><label>Mot de passe</label><input type='password' name='wifi_pass' "
         "maxlength='64' placeholder='inchangé si vide' autocomplete='new-password'></div></div>"
         "<p class='note'>Mot de passe vide : celui d'aujourd'hui est gardé ; avec un autre réseau, vide = réseau ouvert.</p>"
         "<h2>Passerelle</h2><div class='carte'><div class='champ'><label>Adresse de la passerelle</label>"
         "<input type='text' name='gw' maxlength='95' placeholder='http://192.168.1.44:8080' value='")
    << esc(cfg.gateway) << F("'></div><div class='ligne'><div class='ico' style='background:")
    << (ok ? F("#13372A") : F("#3A2F12")) << F("'>⇄</div><div class='lib'><b>")
    << (ok ? String(F("Connectée")) : String(gwErrorText(gwLastCode))) << F("</b><span class='ro'>");
  if (gMods.known) c << F("passerelle ") << esc(gMods.version) << F(" · ") << (long)nbModules()
                     << F(" modules");
  else c << F("modules inconnus");
  c << F("</span></div><i class='pt") << (ok ? F("") : F(" w")) << F("'></i></div></div>"
         "<h2>Point d'accès</h2><div class='carte'><div class='champ'><label>Nom (ouvert, actif si le Wi-Fi maison "
         "est absent)</label><input type='text' name='ap_ssid' maxlength='32' value='") << esc(cfg.apSsid)
    << F("'></div></div><div class='enreg'><button class='btn' type='submit'>Enregistrer et redémarrer</button>"
         "</div></form></div>");
  finPage(c, OG_RESEAU);
}

// --------------------------------------------------------- onglet Système

static void handleSysteme() {
  Chunks c;
  debutPage(c, "Système", String(F("firmware ")) + FW_VERSION);
  const bool ok = gwLastCode == 200;
  c << F("</div><div class='corps'><h2>État</h2><div class='carte'>"
         "<div class='ligne'><div class='lib'><b>Mémoire libre</b><span>plus grand bloc ")
    << (long)(ESP.getMaxFreeBlockSize() / 1024) << F(" Ko · fragmentation ") << (long)ESP.getHeapFragmentation()
    << F(" %</span></div><span class='ro'>") << (long)(ESP.getFreeHeap() / 1024) << F(" Ko</span></div>"
         "<div class='ligne'><div class='lib'><b>En marche depuis</b><span>redémarrage nocturne à 2:15</span></div>"
         "<span class='ro'>") << duree(millis() / 1000) << F("</span></div>"
         "<div class='ligne'><div class='lib'><b>Passerelle</b><span>");
  if (gwLastOkMs) c << F("dernière réponse ") << ilYa(gwLastOkMs);
  else c << F("pas encore de réponse");
  if (gwRetryInSeconds()) c << F(" · nouvel essai dans ") << (long)gwRetryInSeconds() << F(" s");
  c << F("</span></div><span class='") << (ok ? F("ok'>OK") : F("warn'>KO")) << F("</span></div>"
         "<div class='ligne'><div class='lib'><b>Dernier démarrage</b><span>") << santeCauseDemarrage()
    << F("</span></div></div></div><h2>Outils</h2><div class='carte'>"
         "<a class='ligne' href='/update' style='color:inherit;text-decoration:none'><div class='ico' "
         "style='background:#13372A'>⬆</div><div class='lib'><b>Mise à jour du firmware</b><span>fichier "
         "firmware.bin.gz · place libre ") << (long)(ESP.getFreeSketchSpace() / 1024)
    << F(" Ko</span></div><span class='chev'>›</span></a></div>"
         "<div style='margin-top:16px'><a class='btn g' href='/restart'>Redémarrer l'afficheur</a></div></div>");
  finPage(c, OG_SYSTEME);
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
    if (("," + vals[k] + ",").indexOf(",*,") >= 0) vals[k] = "*";   // « tous » l'emporte
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

// Page de confirmation, puis retour à l'onglet (ou redémarrage). Envoyée par
// morceaux, comme les onglets (la feuille de style fait plusieurs Ko).
static void confirmation(const char* titre, const String& message, const char* retour, uint8_t secondes) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html; charset=utf-8"), "");
  Chunks c;
  c << F("<!DOCTYPE html><html lang='fr'><head><meta charset='UTF-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='")
    << (long)secondes << F(";url=") << retour << F("'><title>MODELROGS</title>");
  envoyerStyle(c);
  c << F("</head><body><div class='top'><div class='titre'><div class='logo'>M</div><h1>") << titre
    << F("<small>MODELROGS Display</small></h1></div></div><div class='corps'><div class='carte'><p style='margin:0;"
         "padding:14px'>") << message << F("</p></div></div></body></html>");
  c.flush();
  server.sendContent("");
}

// Chaque onglet envoie ses propres champs (« o » = onglet) : seuls ceux-là changent.
static void handleSave() {
  const String o = server.arg("o");
  bool redemarrer = false, apChanged = false;

  if (o == "reseau") {
    String ssid = server.arg("wifi_ssid");
    ssid.trim();
    String pass = server.arg("wifi_pass");
    if (ssid != cfg.wifiSsid) {
      copyArg("wifi_ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
      copyArg("wifi_pass", cfg.wifiPass, sizeof(cfg.wifiPass));   // vide = réseau ouvert
    } else if (pass.length()) {
      copyArg("wifi_pass", cfg.wifiPass, sizeof(cfg.wifiPass));
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
    redemarrer = true;
  } else if (o == "ecran") {
    cfg.nightOn = server.hasArg("nuit");
    cfg.nightStart = parseHhmm(server.arg("nd"), cfg.nightStart);
    cfg.nightEnd = parseHhmm(server.arg("nf"), cfg.nightEnd);
    if (server.hasArg("lj")) cfg.brightDay = constrain(server.arg("lj").toInt(), 5, 100);
    if (server.hasArg("ln")) cfg.brightNight = constrain(server.arg("ln").toInt(), 0, 100);
  } else {   // « pages »
    for (uint8_t p = 0; p < PG_COUNT; p++) {
      // Seules les pages affichées dans le formulaire sont modifiées.
      if (server.hasArg(String("pk") + p)) {
        setPageEnabled(p, server.hasArg(String("pg") + p));
      }
      if (server.hasArg(String("pm") + p)) saveModuleParams(p);
      if (server.hasArg(String("pp") + p)) persoRegler(p, server.hasArg(String("ps") + p));
    }
    if (server.hasArg("duree")) cfg.pageSec = constrain(server.arg("duree").toInt(), 5, 120);
  }

  cfg.validated = 1;   // le premier Enregistrer valide la configuration
  saveConfig();

  if (redemarrer) {
    confirmation("Réseau", apChanged
      ? String(F("Enregistré. Redémarrage : reconnectez-vous au Wi-Fi « ")) + esc(cfg.apSsid) + F(" », puis rouvrez 192.168.4.1.")
      : String(F("Enregistré. L'afficheur redémarre ; retour à l'interface dans 15 secondes.")), "/reseau", 15);
    delay(200);
    santeRedemarrer(CAUSE_INTERFACE);
    return;
  }
  confirmation("Enregistré", F("Réglages enregistrés."), o == "ecran" ? "/ecran" : "/", 2);
  schedulerConfigChanged(false);
}

static void handleRestart() {
  confirmation("Redémarrage", F("L'afficheur redémarre. Reconnectez-vous à son Wi-Fi si nécessaire."), "/systeme", 15);
  delay(200);
  santeRedemarrer(CAUSE_INTERFACE);
}

static void handleShow() {
  int p = server.arg("p").toInt();
  if (p >= 0 && p < PG_COUNT) schedulerForce(p);
  server.sendHeader("Location", "/", true);
  server.send(303, F("text/plain"), "");
}

static void handleDebug() {
  const bool memoireOk = basculerDiag();
  const DiagCompteurs& k = diagCompteurs();
  Chunks c;
  debutPage(c, "Diagnostic", String(F("depuis ")) + duree(millis() / 1000) + F(" · X-Req ") + String(diagDernierId()));
  const bool actif = diagActif();

  // --- Mode : l'interrupteur est un lien (/debug?on=1 ou ?off=1)
  c << F("</div><div class='corps'><div class='carte' style='margin-top:6px'><a class='ligne' href='/debug?")
    << (actif ? F("off=1") : F("on=1")) << F("' style='color:inherit;text-decoration:none'><div class='ico' "
         "style='background:#2B2550'>🩺</div><div class='lib'><b>Mode diagnostic</b><span>journal des 32 dernières "
         "requêtes, sonde /ping ; arrêté au redémarrage</span></div>");
  interrupteurVisuel(c, actif);
  c << F("</a></div>");
  if (!memoireOk) c << F("<p class='note ko'>Mémoire insuffisante pour le journal.</p>");

  // --- Wi-Fi et mémoire
  c << F("<h2>Wi-Fi et mémoire</h2><div class='carte'><div class='tuiles'><div><b>") << (long)WiFi.RSSI()
    << F("</b><span>dBm · canal ") << (long)WiFi.channel() << F("</span></div><div><b>")
    << (long)(ESP.getMaxFreeBlockSize() / 1024) << F(" Ko</b><span>plus grand bloc</span></div><div><b>")
    << (long)k.deconnexions << F("</b><span>déconnexions</span></div></div><div class='kv'>"
         "<b>Mémoire libre</b><span>") << (long)ESP.getFreeHeap() << F(" o, minimum ") << (long)santeMinTasLibre()
    << F(" o</span><b>Plus grand bloc</b><span>minimum ") << (long)santeMinBlocLibre() << F(" o, fragmentation ")
    << (long)ESP.getHeapFragmentation() << F(" %</span><b>Adresse</b><span>") << WiFi.localIP().toString()
    << F("</span>");
  if (k.deconnexionMs)
    c << F("<b>Dernière déconnexion</b><span>") << ilYa(k.deconnexionMs) << F(", raison ") << (long)k.raison << F("</span>");
  c << F("</div></div>");

  // --- Requêtes
  const uint32_t echecs = k.connexion + k.delai + k.perdue + k.autresReseau + k.imageKo;
  c << F("<h2>Requêtes</h2><div class='carte'><div class='ligne'><div class='lib'><b>Réussies</b><span>JSON ")
    << (long)k.requetes[DG_JSON] << F(" · images ") << (long)k.requetes[DG_IMG] << F(" · sondes ") << (long)k.sondes
    << F("</span></div><span class='ro ok'>") << (long)k.reussies << F("</span></div>"
         "<div class='ligne'><div class='lib'><b>Échecs réseau</b><span>connexion ") << (long)k.connexion
    << F(" · délai ") << (long)k.delai << F(" · perdue ") << (long)k.perdue << F(" · incomplètes ") << (long)k.imageKo
    << F("</span></div><span class='ro") << (echecs ? F(" warn") : F("")) << F("'>") << (long)echecs << F("</span></div>"
         "<div class='ligne'><div class='lib'><b>Nouveaux essais</b><span>dont réussis ") << (long)k.relancesOk
    << F("</span></div><span class='ro'>") << (long)k.relances << F("</span></div>"
         "<div class='ligne'><div class='lib'><b>Temporisations</b><span>");
  if (k.tempoMs) c << F("dernière : ") << esc(k.tempoChemin) << F(", ") << ilYa(k.tempoMs);
  else c << F("aucune");
  c << F("</span></div><span class='ro'>") << (long)k.tempos << F("</span></div><div class='kv'>"
         "<b>Pire durée</b><span>JSON ") << (long)k.maxMs[DG_JSON] << F(" ms · image ") << (long)k.maxMs[DG_IMG]
    << F(" ms</span><b>Autres erreurs</b><span>réseau ") << (long)k.autresReseau << F(" · HTTP ") << (long)k.http
    << F(" · JSON ") << (long)k.jsonKo << F("</span><b>Sautés</b><span>appels ") << (long)k.sautesAppels
    << F(" · images ") << (long)k.sautesImages << F("</span><b>Dernier échec</b><span>");
  if (k.dernierEchecMs) c << diagTexteCode(k.dernierCode) << F(", ") << esc(k.dernierChemin) << F(", ") << ilYa(k.dernierEchecMs);
  else c << F("aucun");
  c << F("</span><b>Sonde /ping</b><span>") << (long)k.sondes << F(", échecs ") << (long)k.sondesKo;
  const uint32_t okSondes = k.sondes - k.sondesKo;
  if (okSondes) c << F(", ") << (long)k.sondeMin << F(" / ") << (long)(k.sondeSomme / okSondes) << F(" / ")
                  << (long)k.sondeMax << F(" ms");
  c << F("</span></div></div>");

  // --- Journal
  c << F("<h2>Dernières requêtes</h2><div class='carte'>");
  if (!actif) {
    c << F("<p class='note' style='margin:0;padding:12px 14px'>Journal disponible en mode diagnostic.</p>");
  } else {
    c << F("<table>");
    for (uint8_t i = 0; i < diagNbEntrees(); i++) {
      const DiagEntree* e = diagEntree(i);
      c << F("<tr><td class='g'>") << diagNomType(e->type) << F(" ") << esc(e->route) << F(" <span style='color:"
             "var(--mut)'>· ") << (long)e->id << F(" · il y a ") << (long)((millis() - e->quandMs) / 1000)
        << F(" s</span></td><td>") << (long)e->totalMs << F(" ms</td><td class='")
        << (e->code == 200 ? "ok" : (e->code == DG_SOURCE ? "warn" : "ko")) << F("'>")
        << (e->code == 200 ? String("OK") : String(e->code)) << F("</td></tr>");
    }
    c << F("</table>");
  }
  c << F("</div><p class='note'>Le numéro X-Req se retrouve dans le journal de la passerelle (DIAGNOSTIC=1 ou "
         "/debug?on=1 sur la passerelle).</p><p><a class='sm' href='/debug'>Actualiser</a><a class='sm' "
         "href='/debug.json'>JSON</a></p></div>");
  finPage(c, OG_DIAG);
}

static void handleDebugJson() {
  basculerDiag();
  const DiagCompteurs& k = diagCompteurs();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("application/json; charset=utf-8"), "");
  Chunks c;
  uint32_t okSondes = k.sondes - k.sondesKo;
  const MemoStats m = memoStats();   // 3.4.0 : réponses gardées (memo.h)
  const DiagReseau& r = diagReseau();   // 3.5.0 : mesures réseau (backlog n° 32)
  c << F("{\"diag\":") << (long)diagActif() << F(",\"version\":\"") << FW_VERSION << F("\",\"depuis_s\":") << (long)(millis() / 1000)
    << F(",\"rssi\":") << (long)WiFi.RSSI() << F(",\"canal\":") << (long)WiFi.channel()
    << F(",\"ip\":\"") << WiFi.localIP().toString() << F("\",\"tas\":") << (long)ESP.getFreeHeap()
    << F(",\"tas_min\":") << (long)santeMinTasLibre() << F(",\"bloc\":") << (long)ESP.getMaxFreeBlockSize()
    << F(",\"bloc_min\":") << (long)santeMinBlocLibre() << F(",\"frag\":") << (long)ESP.getHeapFragmentation()
    << F(",\"pages\":{\"ouvertures\":") << (long)gMesurePages.ouvertures
    << F(",\"modules_octets\":") << (long)gMesurePages.octets
    << F(",\"tas_min\":") << (long)(gMesurePages.ouvertures ? gMesurePages.tas : 0)
    << F(",\"bloc_min\":") << (long)(gMesurePages.ouvertures ? gMesurePages.bloc : 0)
    << F(",\"tas_min_depuis_demarrage\":") << (long)(gMesurePages.ouvertures ? gMesurePages.tasMin : 0)
    << F(",\"bloc_min_depuis_demarrage\":") << (long)(gMesurePages.ouvertures ? gMesurePages.blocMin : 0) << F("}")
    << F(",\"memo\":{\"entrees\":") << (long)m.entrees << F(",\"octets\":") << (long)m.octets
    << F(",\"servies\":") << (long)m.servies << F(",\"secours\":") << (long)m.secours
    << F(",\"demandees\":") << (long)m.demandees << F("}")
    << F(",\"reseau\":{\"veille\":") << (long)(r.veille == 255 ? -1 : r.veille)
    << F(",\"fond\":{\"tours\":") << (long)r.tours << F(",\"requetes\":") << (long)r.requetes
    << F(",\"moyenne_ms\":") << (long)(r.tours ? r.sommeMs / r.tours : 0)
    << F(",\"dernier_ms\":") << (long)r.dernierMs << F(",\"dernier_n\":") << (long)r.dernierN
    << F(",\"max_ms\":") << (long)r.maxMs << F(",\"max_n\":") << (long)r.maxN
    << F(",\"max_il_y_a_s\":") << (r.maxQuandMs ? (long)((millis() - r.maxQuandMs) / 1000) : -1L)
    << F(",\"tas_avant\":") << (long)r.tasAvant << F(",\"tas_apres\":") << (long)r.tasApres
    << F(",\"bloc_apres\":") << (long)r.blocApres
    << F(",\"pire_baisse_tas\":") << (long)r.pireBaisseTas
    << F(",\"pire_baisse_il_y_a_s\":") << (r.pireBaisseQuandMs ? (long)((millis() - r.pireBaisseQuandMs) / 1000) : -1L)
    << F("},\"tcp\":{\"time_wait\":") << (long)r.timeWait << F(",\"time_wait_max\":") << (long)r.timeWaitMax
    << F(",\"actives\":") << (long)r.actives << F(",\"octets_par_connexion\":") << (long)r.pcbOctets
    << F("},\"web\":{\"pause_max_ms\":") << (long)r.pauseMaxMs
    << F(",\"pause_max_il_y_a_s\":") << (r.pauseMaxQuandMs ? (long)((millis() - r.pauseMaxQuandMs) / 1000) : -1L)
    << F(",\"pauses_1s\":") << (long)r.pauses1s << F(",\"pauses_5s\":") << (long)r.pauses5s << F("}}")
    << F(",\"compteurs\":{\"json\":") << (long)k.requetes[DG_JSON] << F(",\"images\":") << (long)k.requetes[DG_IMG]
    << F(",\"sondes\":") << (long)k.sondes << F(",\"reussies\":") << (long)k.reussies
    << F(",\"connexion\":") << (long)k.connexion << F(",\"delai\":") << (long)k.delai << F(",\"perdue\":") << (long)k.perdue
    << F(",\"autres_reseau\":") << (long)k.autresReseau << F(",\"http\":") << (long)k.http << F(",\"json_ko\":") << (long)k.jsonKo
    << F(",\"image_ko\":") << (long)k.imageKo << F(",\"tempos\":") << (long)k.tempos
    << F(",\"relances\":") << (long)k.relances << F(",\"relances_ok\":") << (long)k.relancesOk
    << F(",\"sautes_appels\":") << (long)k.sautesAppels << F(",\"sautes_images\":") << (long)k.sautesImages
    << F(",\"max_ms\":{\"json\":") << (long)k.maxMs[DG_JSON] << F(",\"image\":") << (long)k.maxMs[DG_IMG]
    << F(",\"sonde\":") << (long)k.maxMs[DG_PING] << F("},\"deconnexions\":") << (long)k.deconnexions
    << F(",\"deconnexion_raison\":") << (long)k.raison
    << F(",\"deconnexion_il_y_a_s\":") << (long)(k.deconnexionMs ? (millis() - k.deconnexionMs) / 1000 : -1)
    << F(",\"sonde_ko\":") << (long)k.sondesKo << F(",\"sonde_min_ms\":") << (long)k.sondeMin
    << F(",\"sonde_moy_ms\":") << (long)(okSondes ? k.sondeSomme / okSondes : 0) << F(",\"sonde_max_ms\":") << (long)k.sondeMax
    << F("},\"dernier_echec\":{\"code\":") << (long)k.dernierCode << F(",\"texte\":\"") << diagTexteCode(k.dernierCode)
    << F("\",\"chemin\":\"") << k.dernierChemin
    << F("\",\"il_y_a_s\":") << (long)(k.dernierEchecMs ? (millis() - k.dernierEchecMs) / 1000 : -1)
    << F("},\"derniere_tempo\":{\"code\":") << (long)k.tempoCode << F(",\"texte\":\"") << diagTexteCode(k.tempoCode)
    << F("\",\"chemin\":\"") << k.tempoChemin
    << F("\",\"il_y_a_s\":") << (long)(k.tempoMs ? (millis() - k.tempoMs) / 1000 : -1) << F("},\"journal\":[");
  for (uint8_t i = 0; i < diagNbEntrees(); i++) {
    const DiagEntree* e = diagEntree(i);
    c << (i ? F(",{\"x\":") : F("{\"x\":")) << (long)e->id << F(",\"il_y_a_s\":") << (long)((millis() - e->quandMs) / 1000)
      << F(",\"type\":\"") << diagNomType(e->type) << F("\",\"route\":\"") << e->route
      << F("\",\"code\":") << (long)e->code << F(",\"texte\":\"") << diagTexteCode(e->code)
      << F("\",\"entetes_ms\":") << (long)e->enTetesMs << F(",\"total_ms\":") << (long)e->totalMs
      << F(",\"octets\":") << (long)e->octets << F(",\"rssi\":") << (long)e->rssi << F("}");
  }
  c << F("]}");
  c.flush();
  server.sendContent("");
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
  server.on("/restart", HTTP_GET, handleRestart);
  server.on("/ecran", HTTP_GET, handleEcran);
  server.on("/reseau", HTTP_GET, handleReseau);
  server.on("/systeme", HTTP_GET, handleSysteme);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/debug.json", HTTP_GET, handleDebugJson);

  // OTA : même mécanisme qu'avant (ESP8266HTTPUpdateServer sur /update),
  // enregistré UNE seule fois.
  httpUpdater.setup(&server, "/update");

  server.onNotFound(handleNotFound);
  server.begin();

  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println(F("HTTP + DNS : OK — OTA : http://192.168.4.1/update"));
}

void webTick() {
  // 3.5.0 (backlog n° 32) : temps écoulé depuis le passage précédent, pendant
  // lequel l'interface web ne pouvait pas répondre
  static uint32_t finPrecedent = 0;
  if (finPrecedent) diagPause(millis() - finPrecedent);
  dnsServer.processNextRequest();
  server.handleClient();
  finPrecedent = millis();
}
