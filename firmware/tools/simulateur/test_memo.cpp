// Banc d'essai des réponses gardées en mémoire (3.4.0, src/memo.cpp) :
// fraîcheur 10 min, secours 30 min marqué périmé, limites de taille et de nombre.
#include <cstdio>
#include <string>
#include "memo.h"

uint32_t g_millis = 1000;

// Passerelle imitée : réponse et code programmables, appels comptés.
static int codeRendu = 200;
static std::string reponse = R"({"ok":1,"v":1})";
static int appels = 0;

int gwGetBrut(const String& path, JsonDocument& doc, String& brut) {
  appels++;
  brut = String();
  if (codeRendu != 200) return codeRendu;
  if (deserializeJson(doc, reponse)) return -4;
  if (reponse.size() <= MEMO_REPONSE) brut = String(reponse);
  return 200;
}

int gwRelire(JsonDocument& doc, const String& texte) {   // 3.4.0 (memo.h)
  return deserializeJson(doc, texte.c_str(), texte.length()) ? -4 : 200;
}

static int cas = 0, echecs = 0;
static void verifier(bool ok, const char* quoi) {
  cas++;
  if (!ok) { echecs++; printf("ÉCHEC : %s\n", quoi); }
}

static const uint32_t MIN = 60UL * 1000;

int main() {
  JsonDocument doc;

  // 1. Première demande : requête ; seconde dans les 10 min : copie, sans requête
  int code = memoGet("/meteo", doc);
  verifier(code == 200 && appels == 1 && (doc["v"] | 0) == 1, "première demande envoyée");
  reponse = R"({"ok":1,"v":2})";
  g_millis += 9 * MIN;
  code = memoGet("/meteo", doc);
  verifier(code == 200 && appels == 1 && (doc["v"] | 0) == 1, "copie réutilisée avant 10 min");
  verifier(!(doc["p"] | 0), "copie fraîche non marquée périmée");

  // 2. À 10 min : nouvelle requête, nouvelle valeur gardée
  g_millis += 1 * MIN;
  code = memoGet("/meteo", doc);
  verifier(code == 200 && appels == 2 && (doc["v"] | 0) == 2, "redemandée à 10 min");

  // 3. Passerelle sans réponse après 10 min : copie marquée périmée, jusqu'à 30 min
  codeRendu = -3;
  g_millis += 12 * MIN;   // copie reçue il y a 12 min
  code = memoGet("/meteo", doc);
  verifier(code == 200 && appels == 3 && (doc["v"] | 0) == 2 && (doc["p"] | 0) == 1,
           "secours : dernière copie marquée périmée");
  g_millis += 17 * MIN;   // 29 min
  code = memoGet("/meteo", doc);
  verifier(code == 200 && (doc["p"] | 0) == 1, "secours encore à 29 min");
  g_millis += 1 * MIN;    // 30 min
  code = memoGet("/meteo", doc);
  verifier(code == -3, "erreur rendue à 30 min");
  verifier(memoStats().entrees == 0, "copie de 30 min rendue");

  // 4. Réponse en erreur de la passerelle (ok=0, -5) : secours aussi
  codeRendu = 200;
  reponse = R"({"ok":1,"v":3})";
  memoGet("/btc?ids=a", doc);
  g_millis += 11 * MIN;
  codeRendu = -5;
  code = memoGet("/btc?ids=a", doc);
  verifier(code == 200 && (doc["v"] | 0) == 3 && (doc["p"] | 0) == 1, "secours sur une erreur -5");

  // 5. Chemins différents : copies séparées ; changer les paramètres = nouvelle requête
  codeRendu = 200;
  int avant = appels;
  memoGet("/btc?ids=b", doc);
  memoGet("/btc?ids=b", doc);
  verifier(appels == avant + 1, "un chemin, une copie");
  memoGet("/btc?ids=c", doc);
  verifier(appels == avant + 2, "paramètres changés : nouvelle requête");

  // 6. Marque « périmée » envoyée par la passerelle : conservée dans la copie
  memoVider();
  reponse = R"({"ok":1,"p":1,"v":4})";
  memoGet("/indices?ids=x", doc);
  memoGet("/indices?ids=x", doc);
  verifier((doc["p"] | 0) == 1 && (doc["v"] | 0) == 4, "marque de la passerelle conservée");

  // 7. Réponse trop grande : pas gardée (chaque affichage la redemande)
  memoVider();
  reponse = std::string(R"({"ok":1,"t":")") + std::string(MEMO_REPONSE, 'x') + "\"}";
  avant = appels;
  memoGet("/custom?ids=z", doc);
  memoGet("/custom?ids=z", doc);
  verifier(appels == avant + 2 && memoStats().entrees == 0, "réponse trop grande non gardée");

  // 8. Nombre limité : au-delà de MEMO_ENTREES, la plus ancienne part
  memoVider();
  reponse = R"({"ok":1,"v":5})";
  for (int i = 0; i < MEMO_ENTREES + 3; i++) {
    g_millis += 1000;
    memoGet(String("/p") + String(i), doc);
  }
  verifier(memoStats().entrees == MEMO_ENTREES, "nombre de copies plafonné");
  avant = appels;
  memoGet("/p0", doc);
  verifier(appels == avant + 1, "la plus ancienne copie est partie");
  memoGet(String("/p") + String(MEMO_ENTREES + 2), doc);
  verifier(appels == avant + 1, "la plus récente est gardée");

  // 9. Taille totale limitée à MEMO_OCTETS
  memoVider();
  reponse = std::string(R"({"ok":1,"t":")") + std::string(1000, 'y') + "\"}";
  for (int i = 0; i < 10; i++) {
    g_millis += 1000;
    memoGet(String("/q") + String(i), doc);
  }
  MemoStats st = memoStats();
  verifier(st.octets <= MEMO_OCTETS && st.entrees >= 5, "taille totale plafonnée");

  // 10. Purge : rien de plus de 30 min ne reste en mémoire
  g_millis += 30 * MIN;
  memoPurger();
  verifier(memoStats().entrees == 0 && memoStats().octets == 0, "purge à 30 min");

  // 11. Compteurs
  st = memoStats();
  verifier(st.servies > 0 && st.secours >= 3 && st.demandees == (uint32_t)appels, "compteurs");

  printf("test_memo : %d cas, %d échec(s)\n", cas, echecs);
  return echecs ? 1 : 0;
}
