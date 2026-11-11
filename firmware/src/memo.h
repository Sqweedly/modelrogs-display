#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// =====================================================
// Réponses « non dynamiques » gardées en mémoire (3.4.0)
// =====================================================
// Météo (pages Heure et Météo), pages de cours et pages 30 jours : la réponse
// brute de la passerelle est gardée 10 minutes et réutilisée sans requête.
// Au-delà, elle est redemandée ; si la passerelle ne répond pas, la dernière
// copie reste affichée, marquée périmée (doc["p"] = 1), jusqu'à 30 minutes
// après sa réception. Ensuite, le code d'erreur est rendu comme avant.
//
// La clé est le chemin complet (paramètres compris) : changer les valeurs
// d'une page change le chemin, donc la copie n'est pas réutilisée.

static const uint32_t MEMO_FRAIS_MS   = 10UL * 60 * 1000;   // réutilisée sans requête
static const uint32_t MEMO_SECOURS_MS = 30UL * 60 * 1000;   // gardée si la passerelle ne répond pas
static const uint8_t  MEMO_ENTREES    = 12;                 // nombre de réponses gardées au plus
static const uint16_t MEMO_OCTETS     = 6144;               // octets gardés au plus (toutes réponses)
static const uint16_t MEMO_REPONSE    = 1536;               // réponse plus grande : pas gardée

// Remplace gwGet pour ces pages. Mêmes codes de retour que gwGet.
int memoGet(const String& path, JsonDocument& doc);

// Retire les copies de plus de 30 minutes (appelée dans loop()).
void memoPurger();
void memoVider();

struct MemoStats {
  uint8_t  entrees;
  uint16_t octets;
  uint32_t servies;    // réponses servies depuis la mémoire (fraîches)
  uint32_t secours;    // copies périmées servies (passerelle sans réponse)
  uint32_t demandees;  // requêtes envoyées à la passerelle
};
MemoStats memoStats();

// Fourni par net.cpp (et par les bancs d'essai) : comme gwGet, mais rend aussi
// le texte brut de la réponse quand elle fait au plus MEMO_REPONSE octets
// (sinon `brut` reste vide et la réponse n'est pas gardée).
int gwGetBrut(const String& path, JsonDocument& doc, String& brut);

// Fourni par net.cpp (et par les bancs d'essai) : analyse un texte JSON gardé,
// avec le même analyseur que les réponses réseau. 200 si valide, sinon -4.
int gwRelire(JsonDocument& doc, const String& texte);
