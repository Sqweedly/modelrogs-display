// Shim minimal pour compiler le code de rendu du firmware sur PC.
#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#define PROGMEM
#ifdef SHIM_WEB
// Interface web (test_web.cpp) : textes « en flash » distincts des const char*,
// comme sur l'ESP8266 (surcharges de Chunks et de String).
class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper*>(p))
#else
#define F(x) x
class __FlashStringHelper;
#define FPSTR(p) ((const char*)(p))
#endif
#define PGM_P const char*
struct SerialShim { template <class T> void println(T) {} template <class T> void print(T) {} };
inline SerialShim Serial;
#define pgm_read_byte(p) (*(const uint8_t*)(p))
using std::max;
using std::min;
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? lo : (v > hi ? hi : v); }

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(char c) : s(1, c) {}
  String(const __FlashStringHelper* f) : s(reinterpret_cast<const char*>(f)) {}
  String& operator+=(const __FlashStringHelper* f) { s += reinterpret_cast<const char*>(f); return *this; }
  template <class T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, char>::value, int>::type = 0>
  String(T v) : s(std::to_string((long long)v)) {}
  size_t length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  char operator[](size_t i) const { return i < s.size() ? s[i] : 0; }
  String substring(size_t a, size_t b) const { return s.substr(a, b > a ? b - a : 0); }
  String substring(size_t a) const { return s.substr(std::min(a, s.size())); }
  void toUpperCase() { for (auto& c : s) if (c >= 'a' && c <= 'z') c -= 32; }
  void toLowerCase() { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; }
  void trim() {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
  }
  void remove(size_t i) { if (i < s.size()) s.erase(i); }
  void reserve(size_t n) { s.reserve(n); }
  int indexOf(const char* x) const { size_t i = s.find(x); return i == std::string::npos ? -1 : (int)i; }
  int indexOf(char x) const { size_t i = s.find(x); return i == std::string::npos ? -1 : (int)i; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  template <class T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, char>::value, int>::type = 0>
  String& operator+=(T v) { s += std::to_string((long long)v); return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  // Pour l'interface web (test_web.cpp)
  bool operator==(const char* o) const { return s == (o ? o : ""); }
  bool operator!=(const String& o) const { return s != o.s; }
  bool operator!=(const char* o) const { return s != (o ? o : ""); }
  bool startsWith(const String& p) const { return s.rfind(p.s, 0) == 0; }
  bool endsWith(const String& p) const { return s.size() >= p.s.size() && s.compare(s.size() - p.s.size(), p.s.size(), p.s) == 0; }
  int indexOf(char x, size_t from) const { size_t i = s.find(x, from); return i == std::string::npos ? -1 : (int)i; }
  int indexOf(const String& x) const { size_t i = s.find(x.s); return i == std::string::npos ? -1 : (int)i; }
  int lastIndexOf(char x) const { size_t i = s.rfind(x); return i == std::string::npos ? -1 : (int)i; }
  long toInt() const { return atol(s.c_str()); }
};
inline String operator+(const String& a, const String& b) { return a.s + b.s; }
inline String operator+(const String& a, const char* b) { return a.s + b; }
inline String operator+(const char* a, const String& b) { return std::string(a) + b.s; }
inline String operator+(const String& a, const __FlashStringHelper* b) { return a.s + reinterpret_cast<const char*>(b); }
