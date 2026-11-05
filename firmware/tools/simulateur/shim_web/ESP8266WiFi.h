// Shim de l'interface web (test_web.cpp) : Wi-Fi et puce ESP figés.
#pragma once
#include <functional>
#include <memory>
#include "Arduino.h"
struct IPAddress { const char* a; String toString() const { return a; } };
struct WiFiEventStationModeDisconnected { uint8_t reason = 0; };
using WiFiEventHandler = std::shared_ptr<int>;
struct WiFiShim {
  int32_t RSSI() { return -72; }
  int32_t channel() { return 8; }
  IPAddress localIP() { return {"192.168.1.107"}; }
  IPAddress softAPIP() { return {"192.168.4.1"}; }
  WiFiEventHandler onStationModeDisconnected(std::function<void(const WiFiEventStationModeDisconnected&)>) { return {}; }
};
inline WiFiShim WiFi;
struct EspShim {
  uint32_t getFreeHeap() { return 30216; }
  uint32_t getMaxFreeBlockSize() { return 28896; }
  uint8_t getHeapFragmentation() { return 4; }
  uint32_t getFreeSketchSpace() { return 413696; }
};
inline EspShim ESP;
inline void yield() {}
