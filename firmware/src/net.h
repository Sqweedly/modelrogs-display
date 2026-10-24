#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// =====================================================
// Réseau : point d'accès permanent + Wi-Fi maison + passerelle
// =====================================================

void netBegin();
void netTick();                // à appeler dans loop()
bool staConnected();
void netApplyConfig(bool wifiChanged, bool apChanged);

// Appel HTTP GET à la passerelle, réponse JSON désérialisée en flux.
// Renvoie 200 si OK, sinon un code < 0 (réseau) ou le code HTTP.
//  -1 Wi-Fi non connecté  -2 passerelle non configurée  -3 connexion
//  -4 JSON invalide       -5 la passerelle a répondu ok=0 (doc["err"])
//
// Temporisation : après un échec de connexion, les appels automatiques sont
// suspendus 5 s, puis 15 s, 30 s et 60 s (renvoient -3 sans attendre), pour
// que l'afficheur ne reste pas bloqué 2,5 s à chaque essai. `interactif`
// (interface web) ignore la temporisation.
int gwGet(const String& path, JsonDocument& doc, JsonDocument* filter = nullptr,
          bool interactif = false);
uint32_t gwRetryInSeconds();   // 0 si aucune temporisation en cours
const char* gwErrorText(int code);

String urlEncode(const char* s);

extern int gwLastCode;
extern uint32_t gwLastOkMs;
