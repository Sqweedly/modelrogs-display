// Test de l'écran de nuit (3.0.2) : le redessin partiel à chaque minute
// (anciennes aiguilles, texte, points nouvellement allumés) doit donner
// exactement les mêmes pixels qu'un redessin complet, pour toutes les minutes
// d'une journée, en avançant de 1, 2 et 7 minutes (minutes sautées).
// Compilé avec sim_lib.o (apercu.sh) ; sortie 1 en cas d'écart.
#include "app.h"
#include "config.h"
#include "pages.h"
#include "ui.h"
#include <cmath>
#include <cstdio>
#include <vector>

static void regle(int minuteDuJour) {
  gTime.valid = true;
  gTime.epoch = 1767225600UL + minuteDuJour * 60UL;   // 2026-01-01 00:00 UTC
  gTime.offset = 0;
  gTime.atMs = millis();
}

int main() {
  loadConfig();
  uiBegin();
  int cas = 0, partiels = 0, ecarts = 0;
  for (int pas : {1, 2, 7}) {
    regle(0);
    drawNuit(true);
    for (int md = pas; md < 1440; md += pas) {
      regle(md);
      tft.fb[0] = 0x1234;                 // coin jamais dessiné : reste intact si pas d'effacement complet
      drawNuit(false);
      partiels += tft.fb[0] == 0x1234;
      tft.fb[0] = 0;
      std::vector<uint16_t> inc(tft.fb, tft.fb + 240 * 240);
      drawNuit(true);
      int d = 0;
      for (int i = 0; i < 240 * 240; i++) d += inc[i] != tft.fb[i];
      if (d) { ecarts++; printf("ECART  %02d:%02d (pas %d) : %d px\n", md / 60, md % 60, pas, d); }
      cas++;
    }
  }
  printf("%d minutes, dont %d redessins partiels, %d écart(s)\n", cas, partiels, ecarts);
  // Cadran évidé (3.1.0) : rien ne se recouvre. Pour chaque minute de la
  // journée, les couronnes vides entre les éléments doivent rester noires :
  // chiffres | 49-50,5 px | anneau 52-55 | 56,5-58,5 | heures 60-82 | 83,8-85,2 | minutes 87-104
  // | 105,2-105,8 | couronne (points de 3 px à 110 px). Bords lissés : 1 px de part et d'autre.
  struct { float r0, r1; const char* entre; } const VIDES[] = {
    {49.0f, 50.5f, "chiffres et anneau"}, {56.5f, 58.5f, "anneau et heures"}, {83.8f, 85.2f, "heures et minutes"},
    {105.2f, 105.8f, "minutes et couronne"} };
  int touches = 0;
  for (int md = 0; md < 1440; md++) {
    regle(md);
    drawNuit(true);
    for (auto& v : VIDES)
      for (int y = 0; y < 240; y++)
        for (int x = 0; x < 240; x++) {
          const float d = sqrtf((x + 0.5f - 120) * (x + 0.5f - 120) + (y + 0.5f - 120) * (y + 0.5f - 120));
          if (d >= v.r0 && d <= v.r1 && tft.fb[y * 240 + x] != 0) {
            if (touches++ < 5) printf("RECOUVREMENT %02d:%02d entre %s (x=%d, y=%d)\n", md / 60, md % 60, v.entre, x, y);
          }
        }
  }
  printf("%d pixel(s) dans les espaces vides sur 1440 minutes\n", touches);
  if (touches) { printf("ÉCHEC : écran de nuit (recouvrement)\n"); return 1; }
  if (ecarts || partiels < cas / 2) { printf("ÉCHEC : écran de nuit\n"); return 1; }
  printf("Écran de nuit : redessin partiel identique au complet, aucun recouvrement.\n");
  return 0;
}
