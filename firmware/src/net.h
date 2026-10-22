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
int gwGet(const String& path, JsonDocument& doc, JsonDocument* filter = nullptr);
const char* gwErrorText(int code);

String urlEncode(const char* s);

extern int gwLastCode;
extern uint32_t gwLastOkMs;
