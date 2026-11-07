// Test des personnages (3.1.0) : choix de l'état de Nimbus, Pousse et Volta
// (fonctions pures de persos.cpp). Sortie 1 en cas d'erreur.
#include "persos.h"
#include <cstdio>

static int erreurs = 0, cas = 0;
static void attendu(const char* quoi, int obtenu, int voulu) {
  cas++;
  if (obtenu != voulu) { erreurs++; printf("ERREUR %s : %d au lieu de %d\n", quoi, obtenu, voulu); }
}

int main() {
  // Nimbus : icônes de la passerelle (meteo.py)
  const int CIEL[9] = { NIMBUS_BEAU, NIMBUS_BEAU, NIMBUS_COUVERT, NIMBUS_COUVERT, NIMBUS_PLUIE,
                        NIMBUS_NEIGE, NIMBUS_ORAGE, NIMBUS_BEAU, NIMBUS_BEAU };
  for (int i = 0; i < 9; i++) { char q[32]; snprintf(q, sizeof q, "nimbus(icône %d)", i); attendu(q, nimbusEtat(i), CIEL[i]); }
  attendu("nimbus(icône inconnue)", nimbusEtat(42), NIMBUS_BEAU);

  // Volta : moins de 20, 20 à 39, 40 à 80, plus de 80 %
  const struct { int pct, e; } V[] = { {0, VOLTA_ROUGE}, {19, VOLTA_ROUGE}, {20, VOLTA_ORANGE}, {39, VOLTA_ORANGE},
    {40, VOLTA_VERT}, {79, VOLTA_VERT}, {80, VOLTA_VERT}, {81, VOLTA_CONTENTE}, {100, VOLTA_CONTENTE} };
  for (auto& v : V) { char q[32]; snprintf(q, sizeof q, "volta(%d %%)", v.pct); attendu(q, voltaEtat(v.pct), v.e); }

  // Pousse sans état précédent : seuils d'entrée (5, 17, 33)
  for (int t = -30; t <= 50; t++) {
    const int voulu = t <= 5 ? POUSSE_GLACIAL : t <= 17 ? POUSSE_FRAIS : t <= 33 ? POUSSE_AGREABLE : POUSSE_CANICULE;
    char q[40]; snprintf(q, sizeof q, "pousse(%d °C, aucun)", t);
    attendu(q, pousseEtat(t, POUSSE_AUCUN), voulu);
  }
  // Pousse avec état précédent : entrée dès le seuil, retour avec 1 °C de marge
  const struct { int t, prec, e; } P[] = {
    {34, POUSSE_AGREABLE, POUSSE_CANICULE}, {33, POUSSE_CANICULE, POUSSE_CANICULE}, {32, POUSSE_CANICULE, POUSSE_AGREABLE},
    {30, POUSSE_AGREABLE, POUSSE_AGREABLE},
    {5, POUSSE_FRAIS, POUSSE_GLACIAL},      {6, POUSSE_GLACIAL, POUSSE_GLACIAL},    {7, POUSSE_GLACIAL, POUSSE_FRAIS},
    {18, POUSSE_FRAIS, POUSSE_AGREABLE},    {17, POUSSE_AGREABLE, POUSSE_AGREABLE}, {16, POUSSE_AGREABLE, POUSSE_FRAIS},
    {33, POUSSE_AGREABLE, POUSSE_AGREABLE}, {6, POUSSE_FRAIS, POUSSE_FRAIS},        {17, POUSSE_FRAIS, POUSSE_FRAIS},
    {35, POUSSE_GLACIAL, POUSSE_CANICULE},  {-5, POUSSE_CANICULE, POUSSE_GLACIAL},  {12, POUSSE_CANICULE, POUSSE_FRAIS} };
  for (auto& p : P) { char q[48]; snprintf(q, sizeof q, "pousse(%d °C, précédent %d)", p.t, p.prec); attendu(q, pousseEtat(p.t, p.prec), p.e); }
  // Oscillation 33 / 34 / 33 / 34 : une seule entrée en canicule, pas de va-et-vient
  uint8_t e = POUSSE_AUCUN; int changements = 0;
  for (int k = 0; k < 20; k++) { uint8_t n = pousseEtat(k % 2 ? 34 : 33, e); if (n != e && e != POUSSE_AUCUN) changements++; e = n; }
  attendu("pousse : changements en oscillant entre 33 et 34 °C", changements, 1);

  printf("%d cas vérifiés, %d erreur(s)\n", cas, erreurs);
  if (erreurs) { printf("ÉCHEC : personnages\n"); return 1; }
  printf("Personnages : états conformes.\n");
  return 0;
}
