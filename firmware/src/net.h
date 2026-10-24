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

// Image annoncée par la passerelle (jeton « i » d'une réponse JSON), dessinée
// en (x, y) au fur et à mesure de sa réception : la passerelle envoie w*h
// pixels RGB565 bruts, déjà recadrés, arrondis (rayon ; w/2 = cercle) et posés
// sur la couleur `fond`. `ajuster` : image entière centrée (logo) au lieu d'un
// recadrage. Mémoire utilisée : une ligne (2*w octets). false si indisponible :
// l'appelant dessine alors un remplaçant. w et h : 160 au plus.
bool gwImage(const char* jeton, int x, int y, int w, int h, uint16_t fond, int rayon,
             bool ajuster = false);
const char* gwErrorText(int code);

String urlEncode(const char* s);

extern int gwLastCode;
extern uint32_t gwLastOkMs;
