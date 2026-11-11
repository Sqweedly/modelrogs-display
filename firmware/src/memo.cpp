#include "memo.h"

// Réponses gardées : clé (chemin), texte brut, instant de réception.
struct Entree {
  String cle;
  String json;
  uint32_t recu = 0;
};
static Entree entrees[MEMO_ENTREES];
static uint32_t nServies = 0, nSecours = 0, nDemandees = 0;

static bool occupee(const Entree& e) { return e.cle.length() > 0; }

static void liberer(Entree& e) {
  e.cle = String();    // rend la mémoire tout de suite
  e.json = String();
  e.recu = 0;
}

static Entree* trouver(const String& path) {
  for (Entree& e : entrees)
    if (occupee(e) && e.cle == path) return &e;
  return nullptr;
}

static uint16_t octets() {
  uint32_t n = 0;
  for (const Entree& e : entrees)
    if (occupee(e)) n += e.cle.length() + e.json.length();
  return n > 0xFFFF ? 0xFFFF : (uint16_t)n;
}

// La plus ancienne copie, sauf `garder` ; nullptr si aucune.
static Entree* plusAncienne(const Entree* garder, uint32_t maintenant) {
  Entree* vieille = nullptr;
  for (Entree& e : entrees) {
    if (!occupee(e) || &e == garder) continue;
    if (!vieille || maintenant - e.recu > maintenant - vieille->recu) vieille = &e;
  }
  return vieille;
}

static void ranger(const String& path, const String& brut, uint32_t maintenant) {
  Entree* e = trouver(path);
  if (!e) {
    for (Entree& l : entrees)
      if (!occupee(l)) { e = &l; break; }
    if (!e) e = plusAncienne(nullptr, maintenant);   // table pleine : la plus ancienne part
    e->cle = path;
  }
  e->json = brut;   // copie : le tampon existant est réutilisé s'il est assez grand
  e->recu = maintenant;
  while (octets() > MEMO_OCTETS) {
    Entree* v = plusAncienne(e, maintenant);
    if (!v) break;
    liberer(*v);
  }
  if (octets() > MEMO_OCTETS) liberer(*e);   // seule, encore trop grande : pas gardée
}

static bool relire(const Entree& e, JsonDocument& doc) {
  return gwRelire(doc, e.json) == 200;
}

int memoGet(const String& path, JsonDocument& doc) {
  const uint32_t maintenant = millis();
  memoPurger();
  Entree* e = trouver(path);
  if (e && maintenant - e->recu < MEMO_FRAIS_MS) {
    if (relire(*e, doc)) { nServies++; return 200; }
    liberer(*e);
    e = nullptr;
  }
  String brut;
  nDemandees++;
  const int code = gwGetBrut(path, doc, brut);
  if (code == 200) {
    if (brut.length()) ranger(path, brut, maintenant);
    else if (e) liberer(*e);   // réponse devenue trop grande : l'ancienne copie ne sert plus
    return 200;
  }
  // La passerelle n'a pas répondu (ou en erreur) : dernière copie, marquée périmée
  if (e && maintenant - e->recu < MEMO_SECOURS_MS && relire(*e, doc)) {
    doc["p"] = 1;
    nSecours++;
    return 200;
  }
  return code;
}

void memoPurger() {
  const uint32_t maintenant = millis();
  for (Entree& e : entrees)
    if (occupee(e) && maintenant - e.recu >= MEMO_SECOURS_MS) liberer(e);
}

void memoVider() {
  for (Entree& e : entrees) liberer(e);
}

MemoStats memoStats() {
  MemoStats s{};
  for (const Entree& e : entrees)
    if (occupee(e)) s.entrees++;
  s.octets = octets();
  s.servies = nServies;
  s.secours = nSecours;
  s.demandees = nDemandees;
  return s;
}
