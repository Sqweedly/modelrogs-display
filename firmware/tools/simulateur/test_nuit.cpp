// Test de l'écran de nuit (3.0.2) : le redessin partiel à chaque minute
// (anciennes aiguilles, texte, points nouvellement allumés) doit donner
// exactement les mêmes pixels qu'un redessin complet, pour toutes les minutes
// d'une journée, en avançant de 1, 2 et 7 minutes (minutes sautées).
// Compilé avec sim_lib.o (apercu.sh) ; sortie 1 en cas d'écart.
#include "app.h"
#include "config.h"
#include "pages.h"
#include "ui.h"
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
  if (ecarts || partiels < cas / 2) { printf("ÉCHEC : écran de nuit\n"); return 1; }
  printf("Écran de nuit : redessin partiel identique au redessin complet.\n");
  return 0;
}
