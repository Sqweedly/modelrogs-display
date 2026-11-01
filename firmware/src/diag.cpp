#include "diag.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <new>

static const uint8_t TAILLE_JOURNAL = 32;

static DiagCompteurs cpt = {};
static uint32_t prochainId = 0;
static DiagEntree* journal = nullptr;   // alloué seulement en mode diagnostic
static uint8_t debut = 0, nb = 0;       // tampon circulaire
static WiFiEventHandler surDeconnexion;

static void copierChemin(char* dst, size_t taille, const char* chemin) {
  size_t i = 0;
  for (; chemin[i] && chemin[i] != '?' && i < taille - 1; i++) dst[i] = chemin[i];
  dst[i] = '\0';
}

void diagBegin() {
  surDeconnexion = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    cpt.deconnexions++;
    cpt.raison = (uint8_t)e.reason;
    cpt.deconnexionMs = millis();
  });
}

uint32_t diagNouvelId() { return ++prochainId; }

void diagRequete(uint8_t type, uint32_t id, const char* chemin, int16_t code,
                 uint32_t enTetesMs, uint32_t totalMs, uint32_t octets) {
  if (type >= DG_NB) return;
  cpt.requetes[type]++;
  if (totalMs > cpt.maxMs[type]) cpt.maxMs[type] = totalMs > 0xFFFF ? 0xFFFF : totalMs;
  if (code == 200 || code == DG_SOURCE) {
    cpt.reussies++;
  } else {
    if (code == HTTPC_ERROR_CONNECTION_FAILED) cpt.connexion++;
    else if (code == HTTPC_ERROR_READ_TIMEOUT) cpt.delai++;
    else if (code == HTTPC_ERROR_CONNECTION_LOST) cpt.perdue++;
    else if (code == DG_JSON_KO) cpt.jsonKo++;
    else if (code == DG_TAILLE || code == DG_INCOMPL) cpt.imageKo++;
    else if (code > 0) cpt.http++;
    else cpt.autresReseau++;
    cpt.dernierCode = code;
    cpt.dernierEchecMs = millis();
    copierChemin(cpt.dernierChemin, sizeof(cpt.dernierChemin), chemin);
  }
  if (type == DG_PING) {
    cpt.sondes++;
    if (code == 200) {
      uint16_t ms = totalMs > 0xFFFF ? 0xFFFF : totalMs;
      if (cpt.sondes - cpt.sondesKo == 1 || ms < cpt.sondeMin) cpt.sondeMin = ms;
      if (ms > cpt.sondeMax) cpt.sondeMax = ms;
      cpt.sondeSomme += ms;
    } else {
      cpt.sondesKo++;
    }
  }
  if (!journal) return;
  uint8_t i = (debut + nb) % TAILLE_JOURNAL;
  if (nb == TAILLE_JOURNAL) debut = (debut + 1) % TAILLE_JOURNAL; else nb++;
  DiagEntree& e = journal[i];
  e.id = id;
  e.quandMs = millis();
  e.octets = octets;
  e.enTetesMs = enTetesMs > 0xFFFF ? 0xFFFF : enTetesMs;
  e.totalMs = totalMs > 0xFFFF ? 0xFFFF : totalMs;
  e.code = code;
  int32_t rssi = WiFi.RSSI();
  e.rssi = (int8_t)(rssi < -128 ? -128 : (rssi > 0 ? 0 : rssi));
  e.type = type;
  copierChemin(e.route, sizeof(e.route), chemin);
}

void diagTempo(const char* chemin, int16_t code) {
  cpt.tempos++;
  cpt.tempoCode = code;
  cpt.tempoMs = millis();
  copierChemin(cpt.tempoChemin, sizeof(cpt.tempoChemin), chemin);
}

void diagSaute(bool image) {
  if (image) cpt.sautesImages++; else cpt.sautesAppels++;
}

bool diagActif() { return journal != nullptr; }

bool diagActiver(bool actif) {
  if (actif && !journal) {
    journal = new (std::nothrow) DiagEntree[TAILLE_JOURNAL];
    debut = nb = 0;
    if (!journal) return false;
  } else if (!actif && journal) {
    delete[] journal;
    journal = nullptr;
    debut = nb = 0;
  }
  return true;
}

const DiagCompteurs& diagCompteurs() { return cpt; }
uint8_t diagNbEntrees() { return journal ? nb : 0; }

const DiagEntree* diagEntree(uint8_t rang) {
  if (!journal || rang >= nb) return nullptr;
  return &journal[(debut + nb - 1 - rang) % TAILLE_JOURNAL];
}

String diagTexteCode(int16_t code) {
  switch (code) {
    case 200:                             return F("OK");
    case HTTPC_ERROR_CONNECTION_FAILED:   return F("connexion impossible");
    case HTTPC_ERROR_SEND_HEADER_FAILED:  return F("envoi de la requête en échec");
    case HTTPC_ERROR_NOT_CONNECTED:       return F("non connecté");
    case HTTPC_ERROR_CONNECTION_LOST:     return F("connexion perdue");
    case HTTPC_ERROR_NO_HTTP_SERVER:      return F("pas de serveur HTTP");
    case HTTPC_ERROR_TOO_LESS_RAM:        return F("mémoire insuffisante");
    case HTTPC_ERROR_READ_TIMEOUT:        return F("délai dépassé");
    case DG_ADRESSE:                      return F("adresse refusée");
    case DG_JSON_KO:                      return F("JSON illisible");
    case DG_SOURCE:                       return F("source indisponible (ok=0)");
    case DG_TAILLE:                       return F("taille inattendue");
    case DG_INCOMPL:                      return F("réception incomplète");
    default: return code > 0 ? String(F("HTTP ")) + code : String(F("erreur ")) + code;
  }
}

const __FlashStringHelper* diagNomType(uint8_t type) {
  switch (type) {
    case DG_JSON: return F("JSON");
    case DG_IMG:  return F("image");
    case DG_PING: return F("sonde");
    default:      return F("?");
  }
}
