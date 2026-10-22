#pragma once
#include <Arduino.h>

// =====================================================
// Rendu des pages (240 x 240) — fidèle aux maquettes
// =====================================================

// Écran d'information générique (démarrage, Wi-Fi, rien à afficher...).
void drawSplash(const String& line1, const String& line2, const String& line3 = "");

// dotIndex / dotCount : position dans la rotation (points en bas d'écran).
void drawHeure(uint8_t dotIndex, uint8_t dotCount);
void drawMeteo(uint8_t dotIndex, uint8_t dotCount);
void drawDiscord(uint8_t dotIndex, uint8_t dotCount);
void drawSupervision(uint8_t dotIndex, uint8_t dotCount);
void drawBtc(uint8_t dotIndex, uint8_t dotCount);
void drawAlert();

// Page Heure affichée : redessine seulement les chiffres si la minute a changé.
// Renvoie true si la page entière doit être redessinée (changement de jour).
bool clockTick();
