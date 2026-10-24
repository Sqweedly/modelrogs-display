// Compare fitText (tampon fixe + dichotomie) à l'ancienne méthode lettre par lettre.
// Lancé par apercu.sh.
#include "ui.h"

// Ancienne méthode (lettre par lettre), pour comparaison
static String ancien(const String& s, FontId f, int maxW) {
  if (textW(s, f) <= maxW) return s;
  String t = s;
  while (t.length() > 0) {
    int n = t.length(); while (n > 0 && (t[n-1] & 0xC0) == 0x80) n--; if (n > 0) n--; t.remove(n); t.trim();
    if (textW(t + "…", f) <= maxW) return t + "…";
  }
  return "";
}
int main() {
  uiBegin();
  const char* textes[] = {"Nicolas le très long pseudo", "Home Assistant", "Vaultwarden", "Éclaircies", "Mosquitto broker MQTT",
    "ÉÈÀÇ très accentué", "a", "", "Riot Shits", "Salon vocal général de l'équipe", "Passerelle injoignable", "x y z w v u t s"};
  int largeurs[] = {20, 40, 75, 110, 160, 190, 212};
  FontId fonts[] = {F_11, F_13, F_16, F_18, F_32};
  int diff = 0, total = 0;
  for (auto t : textes) for (int w : largeurs) for (FontId f : fonts) {
    String a = ancien(t, f, w), b = fitText(t, f, w); total++;
    if (!(a == b)) { diff++; printf("DIFF [%s] f=%d w=%d : ancien='%s' nouveau='%s'\n", t, f, w, a.c_str(), b.c_str()); }
  }
  printf("%d cas comparés, %d différence(s)\n", total, diff);
  return diff != 0;
}
