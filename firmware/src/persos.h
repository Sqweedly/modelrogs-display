#pragma once
// Personnages (3.1.0) : un personnage par domaine, ajouté après la page de son
// domaine pour former un bloc (une durée de page chacun) :
//   Météo  -> Nimbus (ciel actuel), puis Pousse (température actuelle)
//   Tesla  -> Volta (niveau de la batterie, charge)
//   Panne  -> Bip, après PANNE et le récap Supervision (jour et nuit)
// Maquette validée : docs/maquettes/personnages-3.1.png
#include <Arduino.h>

// Météo actuelle, retenue au dernier affichage réussi de la page Météo
struct WxState {
  bool    ok = false;
  int8_t  t = 0;          // température actuelle (°C, arrondie par la passerelle)
  uint8_t i = 0;          // icône actuelle (codes de la passerelle, voir nimbusEtat)
  char    l[24] = "";     // libellé (« Éclaircies »)
};
extern WxState gWx;

// États (fonctions pures, testées par tools/simulateur/test_persos.cpp)
enum NimbusEtat : uint8_t { NIMBUS_BEAU, NIMBUS_COUVERT, NIMBUS_PLUIE, NIMBUS_NEIGE, NIMBUS_ORAGE };
enum PousseEtat : uint8_t { POUSSE_GLACIAL, POUSSE_FRAIS, POUSSE_AGREABLE, POUSSE_CANICULE, POUSSE_AUCUN = 0xFF };
enum VoltaEtat : uint8_t { VOLTA_ROUGE, VOLTA_ORANGE, VOLTA_VERT, VOLTA_CONTENTE };

uint8_t nimbusEtat(uint8_t icone);
// 5 °C et moins, 6 à 17, 18 à 33, 34 et plus ; retour vers un état plus
// tempéré avec 1 °C de marge (voir persos.cpp).
uint8_t pousseEtat(int t, uint8_t precedent);
// moins de 20 %, 20 à 39, 40 à 80, plus de 80 %
uint8_t voltaEtat(uint8_t pct);

// Dessin d'un écran complet (fond, personnage, ligne de texte en bas)
void drawNimbus();
void drawPousse();
void drawVolta();
void drawBip();
