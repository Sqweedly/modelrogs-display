#pragma once
#include <Arduino.h>

// =====================================================
// Santé de l'afficheur : mode secours, redémarrage préventif, mémoire
//
// - Mode secours : 3 plantages successifs dans la première minute
//   -> seuls le Wi-Fi, l'interface web et l'OTA tournent.
// - Redémarrage préventif chaque nuit à 2 h 15 (heure locale de la
//   passerelle) : le Wi-Fi est arrêté, puis l'ESP redémarre, ce qui
//   réinitialise toute la mémoire de travail (fin de la fragmentation).
//   Sans heure connue : redémarrage après 25 h de fonctionnement.
// - Filet de sécurité : redémarrage d'urgence si le plus grand bloc de
//   mémoire libre reste sous 4 Ko pendant 1 minute.
// - Mesures affichées dans la section État de l'interface web.
// =====================================================

enum CauseRedemarrage : uint8_t {
  CAUSE_AUCUNE = 0,
  CAUSE_PLANIFIE,       // 2 h 15
  CAUSE_SANS_HEURE,     // 25 h sans heure connue
  CAUSE_URGENCE,        // mémoire critique
  CAUSE_INTERFACE,      // bouton « Redémarrer normalement »
};

extern bool gSafeMode;

// Au démarrage : met à jour le compteur de plantages ; renvoie true si
// l'afficheur doit démarrer en mode secours.
bool santeDemarrage();

// À appeler à chaque tour de boucle (hors mode secours). `nuit` : plage
// nocturne active, mémorisée pour garder l'écran éteint au redémarrage.
void santeTick(bool heureConnue, int minuteDuJour, bool nuit);

// Redémarrage propre (Wi-Fi arrêté) en mémorisant sa cause.
void santeRedemarrer(CauseRedemarrage cause, bool nuit = false);

// Le dernier redémarrage planifié a eu lieu pendant la plage nocturne :
// garder la luminosité de nuit tant que l'heure n'est pas connue.
bool santeNuitAuDemarrage();

// Mesures pour l'interface web
const char* santeCauseDemarrage();
uint32_t santeMinTasLibre();       // plus petite mémoire libre depuis le démarrage
uint32_t santeMinBlocLibre();      // plus petit « plus grand bloc libre »
uint32_t santeRedemarragesPlanifies();   // depuis la dernière mise sous tension
