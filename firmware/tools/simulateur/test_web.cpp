// Banc d'essai de l'interface web (3.0.0) : exécute le vrai web.cpp sur PC.
// Chaque onglet est écrit dans out_web/<onglet>.html (à ouvrir dans un
// navigateur), puis les enregistrements sont vérifiés (seuls les champs de
// l'onglet changent). Données de la passerelle : fixtures_web/ (générées par la
// vraie passerelle : gateway/tools/fixtures_interface.py).
// Compilation : voir apercu.sh.
#include <fstream>
#include <sstream>

#include "../../src/web.cpp"   // même unité de compilation : accès au serveur (static)
#include "diag.h"

uint32_t g_millis = 7123456;   // 1 h 58 de marche
bool gSafeMode = false;

// ------------------------------------------------------------ santé, ordonnanceur
bool santeDemarrage() { return false; }
void santeTick(bool, int, bool) {}
static int redemarrages = 0;
void santeRedemarrer(CauseRedemarrage, bool) { redemarrages++; }
bool santeNuitAuDemarrage() { return false; }
const char* santeCauseDemarrage() { return "redémarrage nocturne planifié"; }
uint32_t santeMinTasLibre() { return 27120; }
uint32_t santeMinBlocLibre() { return 20288; }
uint32_t santeRedemarragesPlanifies() { return 3; }
static int pageForcee = -1;
void schedulerForce(int8_t p) { pageForcee = p; }
void schedulerConfigChanged(bool) {}

// ----------------------------------------------------------------- réseau
int gwLastCode = 200;
uint32_t gwLastOkMs = 7120456;
static std::string dossier = "fixtures_web/";
// Réponse à /modules?f=2 : « modules » (passerelle 3.2.0, format compact) ou
// « modules_v1 » (passerelle plus ancienne, qui ignore ?f=2 : format d'origine)
static std::string modulesFixture = "modules";
static std::string lire(const std::string& p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
bool staConnected() { return true; }
void netApplyConfig(bool, bool) {}
uint32_t gwRetryInSeconds() { return 0; }
const char* gwErrorText(int code) { return code == -3 ? "Passerelle injoignable" : "Erreur"; }
String urlEncode(const char* s) { return String(s); }
int gwGet(const String& path, JsonDocument& doc, JsonDocument*, bool) {
  std::string p = path.s, nom;
  if (p == "/modules?f=2") nom = modulesFixture;   // l'afficheur demande le format compact (3.2.0)
  else if (p == "/btc/liste") nom = "btc_liste";
  else if (p == "/custom/liste") nom = "custom_liste";
  else if (p == "/actions/liste") nom = "actions_liste";
  else if (p == "/devises/liste") nom = "devises_liste";
  else if (p == "/commodities/liste") nom = "commodities_liste";
  else if (p == "/immich/sources") nom = "immich_sources";
  else if (p == "/time") nom = "time";
  std::string json = nom.empty() ? "" : lire(dossier + nom + ".json");
  if (json.empty()) return gwLastCode = -3;
  diagRequete(DG_JSON, 0, p.c_str(), 200, 0, 0, json.size());   // comme net.cpp : taille annoncée
  if (deserializeJson(doc, json)) return gwLastCode = -4;
  return gwLastCode = (doc["ok"] | 0) ? 200 : -5;
}

int gwRelire(JsonDocument& doc, const String& texte) {   // 3.4.0 (memo.h)
  return deserializeJson(doc, texte.c_str(), texte.length()) ? -4 : 200;
}

int gwGetBrut(const String& path, JsonDocument& doc, String& brut) {   // 3.4.0 (memo.h)
  brut = String();
  return gwGet(path, doc, nullptr, false);
}
bool gwImage(const char*, int, int, int, int, uint16_t, int, bool) { return false; }

// ----------------------------------------------------------------- outils
static int echecs = 0;
static void verifier(const char* libelle, bool ok) {
  echecs += !ok;
  printf("%s %s\n", ok ? "OK   " : "ÉCHEC", libelle);
}
static std::string appeler(const char* route, std::vector<std::pair<std::string, std::string>> args = {}) {
  server.args_ = args;
  server.corps.clear();
  server.entetes.clear();
  server.routes.at(route)();
  return server.corps;
}
static void ecrire(const char* nom, const std::string& html) {
  std::ofstream(std::string("out_web/") + nom + ".html") << html;
}
static bool contient(const std::string& h, const char* x) { return h.find(x) != std::string::npos; }

int main() {
  loadConfig();
  strcpy(cfg.wifiSsid, "Livebox-A1F2");
  strcpy(cfg.gateway, "http://192.168.1.20:8680");
  strcpy(cfg.pCrypto, "s1=bitcoin&s2=ethereum&s3=solana");
  strcpy(cfg.pPhotos, "duree=30");
  cfg.pages &= ~(1u << PG_COMMO);
  cfg.validated = 1;
  cfg.nightOn = 1; cfg.nightStart = 22 * 60 + 30; cfg.nightEnd = 7 * 60; cfg.brightDay = 80; cfg.brightNight = 8;
  dossier = "fixtures/";
  syncTime();                       // heure : fixtures/time.json
  dossier = "fixtures_web/";
  fetchModules();
  diagBegin();
  diagActiver(true);
  const struct { const char* r; int16_t code; uint16_t ms; uint32_t o; uint8_t t; } J[] = {
    {"/img", DG_INCOMPL, 2512, 1460, DG_IMG}, {"/supervision", 200, 67, 85, DG_JSON},
    {"/img", 200, 1704, 115200, DG_IMG}, {"/plex", 200, 11, 42, DG_JSON}};
  for (auto& e : J) diagRequete(e.t, diagNouvelId(), e.r, e.code, e.ms / 2, e.ms, e.o);
  webBegin();

  // --- Rendu des 5 onglets
  std::string h = appeler("/");
  ecrire("pages", h);
  verifier("Pages : 5 onglets, Pages en couleur", contient(h, "<a href='/' class='on'>") && contient(h, "Diagnostic</a>"));
  verifier("Pages : 3 listes et « Autre valeur » pour Crypto", contient(h, "name='p4_s1'") && contient(h, "name='p4_s3'")
           && contient(h, "Autre valeur (code, si absente des listes)"));
  verifier("Pages : Crypto résumée « Bitcoin, Ethereum, Solana »", contient(h, "Bitcoin, Ethereum, Solana"));
  verifier("Pages : ville de la passerelle sous Météo seulement", contient(h, "Sernhac · 5 jours") && !contient(h, "name='ville'"));
  verifier("Pages : Indices absent de la passerelle, interrupteur grisé", contient(h, "module absent de la passerelle")
           && !contient(h, "name='pk9'"));
  verifier("Pages : Discord non configuré signalé", contient(h, "non configuré"));
  verifier("Pages : Heure toujours active, sans champ", contient(h, "toujours active") && !contient(h, "name='pk0'"));
  verifier("Pages : Commodities décochée", contient(h, "name='pg8'>") || contient(h, "name='pg8'><b>"));
  verifier("Pages : Actions, listes de /actions/liste (format compact)", contient(h, "name='p6_s1'")
           && contient(h, "<option value='MC.PA'>LVMH</option>") && contient(h, "placeholder='ex. BNP.PA, ^FCHI'"));
  verifier("Pages : durées des Photos, liste de la passerelle (format compact)",
           contient(h, "<option value='90'>1 min 30 s</option>") && contient(h, "<option value='300'>5 min</option>"));
  verifier("Pages : note des pages de cours construite par l'afficheur",
           contient(h, "<p class='note'>3 au plus, dans l&#39;ordre : listes 1 à 3, puis saisie</p>"));
  // Passerelle plus ancienne (format d'origine) : exactement la même page
  modulesFixture = "modules_v1";
  {
    const std::string v1 = appeler("/");
    verifier("Pages : passerelle d'avant la 3.2.0 (format d'origine), HTML identique", v1 == h);
    if (v1 != h) ecrire("pages_v1", v1);
  }
  modulesFixture = "modules";
  h = appeler("/ecran");
  ecrire("ecran", h);
  verifier("Écran : color-scheme dark, champs d'heure 22:30 et 07:00", contient(h, "color-scheme:dark")
           && contient(h, "name='nd' value='22:30'") && contient(h, "name='nf' value='07:00'"));
  h = appeler("/reseau");
  ecrire("reseau", h);
  verifier("Réseau : adresse de la passerelle, point d'accès, pas de ville", contient(h, "value='http://192.168.1.20:8680'")
           && contient(h, "<h2>Point d'accès</h2>") && !contient(h, "Ville"));
  h = appeler("/systeme");
  ecrire("systeme", h);
  verifier("Système : lien /update et redémarrage", contient(h, "href='/update'") && contient(h, "href='/restart'"));
  h = appeler("/debug");
  ecrire("diagnostic", h);
  verifier("Diagnostic : journal avec l'image incomplète (-203)", contient(h, ">-203<") && contient(h, "Nouveaux essais"));
  h = appeler("/debug.json");
  verifier("debug.json : inchangé (clés relances en plus)", contient(h, "\"diag\":1") && contient(h, "\"relances\":0")
           && contient(h, "\"sautes_appels\":"));
  verifier("debug.json : mémoire de l'onglet Pages (2 ouvertures ; taille de /modules de la dernière, format d'origine, et non de la dernière liste)",
           contient(h, ("\"pages\":{\"ouvertures\":2,\"modules_octets\":" + std::to_string(lire(dossier + "modules_v1.json").size()) + ",").c_str())
           && contient(h, "\"tas_min\":30216,\"bloc_min\":28896,")
           && contient(h, "\"bloc_min_depuis_demarrage\":28896}"));
  verifier("debug.json : mesures réseau vides au départ (veille non relevée = -1)",
           contient(h, "\"reseau\":{\"veille\":-1,\"fond\":{\"tours\":0,\"requetes\":0,\"moyenne_ms\":0,")
           && contient(h, "\"max_il_y_a_s\":-1,") && contient(h, "\"pire_baisse_il_y_a_s\":-1}"));
  // 3.5.0 (backlog n° 32) : relevés, puis lecture dans /debug.json
  diagFond(0, 999, 1, 1, 1);                          // aucun envoi : ignoré
  diagFond(3, 420, 30000, 29500, 19800);              // tour de 3 requêtes : baisse de 500 octets
  g_millis += 2000;
  diagFond(1, 80, 29800, 29900, 19900);               // la mémoire remonte : pas une baisse
  diagTcp(4, 2, 160, 0);
  diagTcp(1, 2, 160, 0);                              // le maximum reste à 4
  diagPause(1500);
  diagPause(6200);
  diagPause(30);
  g_millis += 3000;
  h = appeler("/debug.json");
  verifier("debug.json : tours de fond (2 tours, 4 requêtes, moyenne 250 ms, pire 420 ms à 3 requêtes il y a 5 s)",
           contient(h, "\"fond\":{\"tours\":2,\"requetes\":4,\"moyenne_ms\":250,\"dernier_ms\":80,\"dernier_n\":1,"
                       "\"max_ms\":420,\"max_n\":3,\"max_il_y_a_s\":5,"));
  verifier("debug.json : mémoire du dernier tour et pire baisse (500 octets il y a 5 s)",
           contient(h, "\"tas_avant\":29800,\"tas_apres\":29900,\"bloc_apres\":19900,"
                       "\"pire_baisse_tas\":500,\"pire_baisse_il_y_a_s\":5}"));
  verifier("debug.json : connexions TCP (1 en TIME_WAIT, 4 au plus) et veille aucune (0)",
           contient(h, "\"reseau\":{\"veille\":0,")
           && contient(h, "\"tcp\":{\"time_wait\":1,\"time_wait_max\":4,\"actives\":2,\"octets_par_connexion\":160}"));
  verifier("debug.json : pauses de l'interface web (pire 6,2 s il y a 3 s ; 2 de plus de 1 s, 1 de plus de 5 s)",
           contient(h, "\"web\":{\"pause_max_ms\":6200,\"pause_max_il_y_a_s\":3,\"pauses_1s\":2,\"pauses_5s\":1}}"));

  // --- Enregistrements : seuls les champs de l'onglet changent
  Config avant = cfg;
  appeler("/save", {{"o", "ecran"}, {"nd", "23:00"}, {"nf", "06:30"}, {"lj", "70"}, {"ln", "0"}});
  verifier("Écran enregistré, nuit décochée = plage arrêtée, Wi-Fi intact", !cfg.nightOn && cfg.nightStart == 23 * 60
           && cfg.nightEnd == 6 * 60 + 30 && cfg.brightDay == 70 && cfg.brightNight == 0
           && !strcmp(cfg.wifiSsid, avant.wifiSsid) && cfg.pages == avant.pages);
  appeler("/save", {{"o", "pages"}, {"pk4", "1"}, {"pg4", "on"}, {"pm4", "1"}, {"p4_s1", "bitcoin"}, {"p4_s2", ""},
                    {"p4_s3", ""}, {"p4_m", ""}, {"pk8", "1"}, {"pg8", "on"}, {"pk2", "1"}, {"duree", "20"}});
  verifier("Pages enregistrées : Crypto = bitcoin, Commodities cochée, Discord décochée, durée 20",
           !strcmp(cfg.pCrypto, "s1=bitcoin") && (cfg.pages & 1u << PG_COMMO) && !(cfg.pages & 1u << PG_DISCORD)
           && cfg.pageSec == 20 && !cfg.nightOn);
  verifier("Page Heure toujours active après enregistrement", cfg.pages & 1u << PG_HEURE);
  appeler("/save", {{"o", "reseau"}, {"wifi_ssid", "Livebox-A1F2"}, {"wifi_pass", ""}, {"gw", "192.168.1.30:8680/"},
                    {"ap_ssid", "ModelrogsTV"}});
  verifier("Réseau enregistré : adresse normalisée, mot de passe gardé, redémarrage",
           !strcmp(cfg.gateway, "http://192.168.1.30:8680") && !strcmp(cfg.wifiPass, avant.wifiPass) && redemarrages == 1
           && cfg.pageSec == 20);
  appeler("/show", {{"p", "12"}});
  verifier("Afficher maintenant (Tesla)", pageForcee == PG_TESLA);

  // --- Mode secours : OTA en tête, sans appel à la passerelle
  gSafeMode = true;
  h = appeler("/");
  ecrire("secours", h);
  verifier("Mode secours : lien /update, pas de liste de pages", contient(h, "Mode secours") && contient(h, "href='/update'")
           && !contient(h, "Rotation"));
  printf("%s\n", echecs ? "Des tests ont échoué." : "Tous les tests de l'interface sont passés.");
  return echecs ? 1 : 0;
}
