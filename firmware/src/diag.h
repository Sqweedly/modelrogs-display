#pragma once
#include <Arduino.h>

// =====================================================
// Diagnostic de bout en bout (afficheur)
//
// - Chaque appel à la passerelle porte un en-tête X-Req (numéro croissant
//   depuis le démarrage) : la passerelle le journalise, ce qui permet de
//   retrouver la même requête des deux côtés.
// - Compteurs toujours actifs (quelques centaines d'octets) : requêtes,
//   échecs par cause, temporisations, appels et images sautés, Wi-Fi.
// - Mode diagnostic, activé à la demande depuis l'interface (page
//   /debug) et gardé en mémoire vive seulement (éteint au redémarrage) :
//   journal des 32 dernières requêtes (1 Ko, alloué à l'activation) et
//   sonde réseau /ping toutes les 30 s.
// Rien ici ne modifie le comportement des appels (codes, temporisation).
// =====================================================

enum DiagType : uint8_t { DG_JSON, DG_IMG, DG_PING, DG_NB };

// Codes propres à l'afficheur (les codes < 0 au-dessus de -100 viennent
// d'HTTPClient : -1 connexion impossible, -11 délai dépassé, etc.)
static const int16_t DG_ADRESSE = -100;     // http.begin() a refusé l'adresse
static const int16_t DG_JSON_KO = -200;     // réponse JSON illisible
static const int16_t DG_SOURCE  = -201;     // JSON lu, mais ok=0 (source indisponible)
static const int16_t DG_TAILLE  = -202;     // image : taille annoncée inattendue
static const int16_t DG_INCOMPL = -203;     // image : réception incomplète

struct DiagEntree {          // 32 octets
  uint32_t id;               // numéro X-Req
  uint32_t quandMs;          // millis() à la fin de la requête
  uint32_t octets;           // octets annoncés (JSON) ou reçus (image)
  uint16_t enTetesMs;        // de la demande à la réception des en-têtes
  uint16_t totalMs;          // de la demande à la fin de la lecture
  int16_t  code;             // 200, code HTTP, ou code d'erreur (voir ci-dessus)
  int8_t   rssi;             // signal Wi-Fi (dBm) au moment de la requête
  uint8_t  type;             // DiagType
  char     route[12];        // début du chemin (sans les paramètres)
};

struct DiagCompteurs {
  uint32_t requetes[DG_NB];
  uint32_t reussies;          // réponse 200 lue en entier (JSON ok=0 compris)
  uint32_t connexion;         // -1 connexion impossible
  uint32_t delai;             // -11 délai dépassé
  uint32_t perdue;            // -5 connexion perdue
  uint32_t autresReseau;      // autres erreurs HTTPClient, adresse refusée
  uint32_t http;              // code HTTP autre que 200
  uint32_t jsonKo;            // JSON illisible
  uint32_t imageKo;           // image : taille inattendue ou réception incomplète
  uint32_t tempos;            // temporisations déclenchées
  uint32_t sautesAppels;      // appels refusés pendant une temporisation
  uint32_t sautesImages;      // images non demandées pendant une temporisation
  uint16_t maxMs[DG_NB];      // pire durée totale par type
  uint32_t deconnexions;      // déconnexions Wi-Fi (station)
  uint8_t  raison;            // raison de la dernière (code du SDK)
  uint32_t deconnexionMs;     // millis() de la dernière ; 0 = aucune
  int16_t  dernierCode;       // dernier échec : code
  uint32_t dernierEchecMs;    //   millis() ; 0 = aucun
  char     dernierChemin[28]; //   chemin (tronqué)
  int16_t  tempoCode;         // dernière temporisation : code de l'appel
  uint32_t tempoMs;           //   millis() ; 0 = aucune
  char     tempoChemin[28];   //   chemin (tronqué)
  uint32_t sondes, sondesKo;  // sonde /ping (mode diagnostic)
  uint16_t sondeMin, sondeMax;
  uint32_t sondeSomme;        // somme des durées des sondes réussies
  uint32_t relances;          // 3.0.0 : nouveaux essais immédiats après un raté réseau
  uint32_t relancesOk;        //   dont réussis
};

void diagBegin();                         // à appeler au démarrage (événements Wi-Fi)
uint32_t diagNouvelId();                  // prochain numéro X-Req
uint32_t diagDernierId();                 // dernier numéro X-Req donné
void diagRequete(uint8_t type, uint32_t id, const char* chemin, int16_t code,
                 uint32_t enTetesMs, uint32_t totalMs, uint32_t octets);
void diagTempo(const char* chemin, int16_t code);
void diagSaute(bool image);
void diagRelance(bool reussie);           // nouvel essai immédiat (net.cpp)
uint32_t diagDerniersOctets();            // taille annoncée de la dernière réponse JSON (3.3.0)
bool diagActif();
bool diagActiver(bool actif);             // false si la mémoire manque
const DiagCompteurs& diagCompteurs();
uint8_t diagNbEntrees();
const DiagEntree* diagEntree(uint8_t rang);   // 0 = la plus récente
String diagTexteCode(int16_t code);
const __FlashStringHelper* diagNomType(uint8_t type);
