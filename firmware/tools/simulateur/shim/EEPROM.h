// Imitation de l'EEPROM ESP8266 (rien n'est conservé).
#pragma once
#include <Arduino.h>
struct EEPROMClass {
  uint8_t data[4096] = {0};
  void begin(size_t) {}
  template <class T> void get(int a, T& v) { memcpy(&v, data + a, sizeof(T)); }
  template <class T> void put(int a, const T& v) { memcpy(data + a, &v, sizeof(T)); }
  bool commit() { return true; }
};
inline EEPROMClass EEPROM;
