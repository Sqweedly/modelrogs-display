#pragma once
#include <Arduino.h>

// =====================================================
// Interface web interne (point d'accès 192.168.4.1, sans mot de passe)
// + OTA /update inchangée + portail captif
// =====================================================

void webBegin();
void webTick();

// Implémentées dans main.cpp
void schedulerForce(int8_t page);
void schedulerConfigChanged(bool villeChanged);

