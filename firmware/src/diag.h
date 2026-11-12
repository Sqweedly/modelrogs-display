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

// =====================================================
// Mesures réseau (3.5.0, backlog n° 32) : relevés seulement, aucun
// changement de fonctionnement. Valeurs « max » et « pire » depuis le
// démarrage. Servent à décider des n° 31, 33, 34 et 35 du backlog.
// =====================================================
struct DiagReseau {
  // Requêtes de fond (liste des modules, heure, Discord, Supervision, Plex,
  // Tesla) envoyées dans un même tour de loop()
  uint32_t tours;             // tours avec au moins une requête de fond
  uint32_t requetes;          // requêtes comptées dans ces tours (nouveaux essais compris)
  uint32_t sommeMs;           // durée cumulée de ces tours
  uint16_t dernierMs;         // dernier tour : durée
  uint8_t  dernierN;          //   nombre de requêtes
  uint16_t maxMs;             // tour le plus long : durée
  uint8_t  maxN;              //   nombre de requêtes
  uint32_t maxQuandMs;        //   millis() ; 0 = aucun
  // Mémoire libre juste avant et juste après le dernier tour, et pire baisse ;
  // plus grand bloc après seulement (le relever parcourt tout le tas, on ne
  // le fait donc pas à chaque tour de loop())
  uint32_t tasAvant, tasApres;
  uint16_t blocApres;
  uint32_t pireBaisseTas;     // tasAvant - tasApres, le plus grand constaté
  uint32_t pireBaisseQuandMs;
  // Connexions TCP (lwIP), relevées une fois par seconde et après chaque tour
  uint8_t  timeWait;          // connexions fermées encore en TIME_WAIT
  uint8_t  timeWaitMax;
  uint8_t  actives;           // connexions ouvertes (actives)
  uint16_t pcbOctets;         // mémoire d'une connexion (sizeof(struct tcp_pcb))
  uint8_t  veille;            // WiFi.getSleepMode() : 0 aucune, 1 légère, 2 modem ; 255 = non relevé
  // Interface web : temps écoulé entre deux passages de webTick()
  uint32_t pauseMaxMs;
  uint32_t pauseMaxQuandMs;
  uint32_t pauses1s;          // pauses de plus de 1 s
  uint32_t pauses5s;          // pauses de plus de 5 s
};
void diagFond(uint8_t n, uint32_t ms, uint32_t tasAvant, uint32_t tasApres, uint16_t blocApres);
void diagTcp(uint8_t timeWait, uint8_t actives, uint16_t pcbOctets, uint8_t veille);
void diagPause(uint32_t ms);
const DiagReseau& diagReseau();
const __FlashStringHelper* diagNomType(uint8_t type);
