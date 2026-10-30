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
// Pages de cours : Crypto (PG_BTC), Actions, Devises, Commodities (1 à 3 valeurs)
void drawCours(uint8_t page, uint8_t dotIndex, uint8_t dotCount);
// Page de cours affichée depuis une durée de page : dessine la page dédiée
// suivante (valeur à +/- 5 % sur 7 jours) et renvoie true, ou false s'il n'y en a plus.
bool coursEcranSuivant(uint8_t page, uint8_t dotIndex, uint8_t dotCount);
void drawPlex(uint8_t dotIndex, uint8_t dotCount);
// Photos Immich : photo en plein écran (+ pastille « Il y a N ans » pour un souvenir)
void drawPhotos(uint8_t dotIndex, uint8_t dotCount);
void plexProgress();          // Plex affichée : ne redessine que la progression
void drawAlert();

// Page Heure affichée : redessine seulement les chiffres si la minute a changé.
// Renvoie true si la page entière doit être redessinée (changement de jour).
bool clockTick();
