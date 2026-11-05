// Shim du serveur web : les routes sont appelées directement par test_web.cpp,
// la réponse est accumulée dans `corps`.
#pragma once
#include <functional>
#include <map>
#include <vector>
#include "Arduino.h"
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct ESP8266WebServer {
  std::map<std::string, std::function<void()>> routes;
  std::vector<std::pair<std::string, std::string>> args_;
  std::string corps, entetes;
  int code = 0;
  explicit ESP8266WebServer(int) {}
  void on(const char* r, HTTPMethod, std::function<void()> f) { routes[r] = f; }
  void onNotFound(std::function<void()> f) { routes["*"] = f; }
  void begin() {}
  void handleClient() {}
  void setContentLength(size_t) {}
  template <class T> void send(int c, T, const String& b) { code = c; corps += b.s; }
  void sendContent(const String& b) { corps += b.s; }
  void sendContent_P(const char* b) { corps += b; }
  void sendContent_P(const __FlashStringHelper* b) { corps += reinterpret_cast<const char*>(b); }
  void sendHeader(const char* k, const String& v, bool = false) { entetes += std::string(k) + ": " + v.s + "\n"; }
  bool hasArg(const String& n) const { for (auto& a : args_) if (a.first == n.s) return true; return false; }
  String arg(const String& n) const { for (auto& a : args_) if (a.first == n.s) return a.second; return ""; }
  String arg(int i) const { return args_[i].second; }
  String argName(int i) const { return args_[i].first; }
  int args() const { return (int)args_.size(); }
  String hostHeader() const { return "192.168.1.107"; }
};
