#include "app.h"
#include "config.h"
#include "net.h"

#include <ArduinoJson.h>

TimeState    gTime;
DiscordState gDisc;
SupState     gSup;
ModulesState gMods;

static void copyStr(char* dst, size_t size, const char* src) {
  strncpy(dst, src ? src : "", size - 1);
  dst[size - 1] = '\0';
}

// --------------------------------------------------------------- Modules

void fetchModules() {
  gMods.lastFetch = millis();
  JsonDocument filter;
  filter["ok"] = true;
  filter["v"] = true;
  filter["m"][0]["id"] = true;
  filter["m"][0]["ok"] = true;
  JsonDocument doc;
  if (gwGet("/modules", doc, &filter) != 200) return;   // on garde la dernière connaissance

  uint8_t present = 0, ok = 0;
  for (JsonObject m : doc["m"].as<JsonArray>()) {
    const char* id = m["id"] | "";
    for (uint8_t p = 0; p < PG_COUNT; p++) {
      if (strcmp(id, PAGE_IDS[p]) == 0) {
        present |= 1 << p;
        if (m["ok"] | 0) ok |= 1 << p;
      }
    }
  }
  gMods.present = present;
  gMods.ok = ok;
  gMods.known = true;
  copyStr(gMods.version, sizeof(gMods.version), doc["v"] | "?");
}

bool moduleUsable(uint8_t page) {
  if (!gMods.known) return true;          // passerelle pas encore interrogée
  return gMods.ok & (1 << page);
}

bool discordConfigured() { return modParam(PG_DISCORD, "salon").length() > 0; }
bool supConfigured()     { return isdigit((unsigned char)modParam(PG_SUP, "ids")[0]); }

String modulePath(uint8_t page) {
  String path = "/";
  path += PAGE_IDS[page];
  path += '?';
  path += modParams(page);
  return path;
}

// ---------------------------------------------------------------- Heure

void syncTime() {
  gTime.lastTry = millis();
  JsonDocument doc;
  if (gwGet("/time?ville=" + urlEncode(cfg.ville), doc) != 200) return;

  gTime.epoch = doc["e"] | 0UL;
  gTime.offset = doc["o"] | 0L;
  gTime.atMs = millis();
  gTime.valid = gTime.epoch > 1700000000UL;
  copyStr(gTime.jour, sizeof(gTime.jour), doc["j"] | "");
  copyStr(gTime.date, sizeof(gTime.date), doc["d"] | "");
  copyStr(gTime.lever, sizeof(gTime.lever), doc["lv"] | "");
  copyStr(gTime.coucher, sizeof(gTime.coucher), doc["cc"] | "");
}

bool timeValid() { return gTime.valid; }

uint32_t localEpoch() {
  return gTime.epoch + gTime.offset + (millis() - gTime.atMs) / 1000UL;
}

int localMinuteOfDay() {
  if (!gTime.valid) return -1;
  return (localEpoch() % 86400UL) / 60;
}

// -------------------------------------------------------------- Discord

static uint32_t hashStr(uint32_t h, const char* s) {
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619UL;
  return h;
}

bool pollDiscord() {
  gDisc.lastPoll = millis();
  bool wasOk = gDisc.ok, wasStale = gDisc.stale;
  if (!discordConfigured()) {
    gDisc.ok = false;
    copyStr(gDisc.err, sizeof(gDisc.err), "Salon vocal à choisir");
    if (wasOk) gDisc.version++;
    return false;
  }
  JsonDocument doc;
  int code = gwGet(modulePath(PG_DISCORD), doc);

  if (code != 200) {
    if (code == -5) {             // la passerelle répond, mais le bot est en défaut
      gDisc.ok = false;
      copyStr(gDisc.err, sizeof(gDisc.err), doc["err"] | "Discord indisponible");
    } else if (gDisc.ok) {
      gDisc.stale = true;         // on garde la dernière liste connue
    } else {
      copyStr(gDisc.err, sizeof(gDisc.err), gwErrorText(code));
    }
    if (gDisc.ok != wasOk || gDisc.stale != wasStale) gDisc.version++;
    return false;
  }

  uint32_t before = hashStr(2166136261UL, gDisc.canal) ^ gDisc.n;
  for (uint8_t i = 0; i < gDisc.count; i++) before = hashStr(before, gDisc.names[i]) + gDisc.isNew[i];

  gDisc.ok = true;
  gDisc.stale = doc["p"] | 0;
  gDisc.n = doc["n"] | 0;
  copyStr(gDisc.canal, sizeof(gDisc.canal), doc["c"] | "");
  gDisc.count = 0;
  for (JsonObject m : doc["m"].as<JsonArray>()) {
    if (gDisc.count >= MAX_DISC_NAMES) break;
    copyStr(gDisc.names[gDisc.count], NAME_BYTES, m["n"] | "?");
    gDisc.isNew[gDisc.count] = (m["w"] | 0) != 0;
    gDisc.count++;
  }

  uint32_t after = hashStr(2166136261UL, gDisc.canal) ^ gDisc.n;
  for (uint8_t i = 0; i < gDisc.count; i++) after = hashStr(after, gDisc.names[i]) + gDisc.isNew[i];
  if (after != before || gDisc.ok != wasOk || gDisc.stale != wasStale) gDisc.version++;

  uint32_t ev = doc["ev"] | 0UL;
  bool arrival = gDisc.evKnown && ev > gDisc.ev && gDisc.n > 0;
  gDisc.ev = ev;
  gDisc.evKnown = true;
  return arrival;
}

// ---------------------------------------------------------- Supervision

static uint32_t supHash() {
  uint32_t h = 2166136261UL;
  uint8_t nums[9] = { gSup.ok, gSup.stale, gSup.resume, gSup.tot, gSup.okn,
                      gSup.pa, gSup.in, gSup.ma, gSup.extra };
  for (uint8_t v : nums) h = (h ^ v) * 16777619UL;
  for (uint8_t i = 0; i < gSup.count; i++) h = hashStr(h, gSup.names[i]) + gSup.st[i];
  return h;
}

void pollSup() {
  uint32_t before = supHash();
  gSup.lastPoll = millis();
  if (!supConfigured()) {
    gSup.ok = false;
    gSup.alert = false;
    if (supHash() != before) gSup.version++;
    return;
  }

  JsonDocument doc;
  int code = gwGet(modulePath(PG_SUP), doc);
  if (code != 200) {
    if (gSup.ok) {
      gSup.stale = true;          // dernière situation connue conservée
    } else {
      copyStr(gSup.err, sizeof(gSup.err), code == -5 ? (doc["err"] | "Kuma indisponible") : gwErrorText(code));
    }
    if (supHash() != before) gSup.version++;
    return;
  }

  gSup.ok = true;
  gSup.stale = doc["p"] | 0;
  gSup.tot = doc["tot"] | 0;
  gSup.okn = doc["okn"] | 0;
  gSup.pa = doc["pa"] | 0;
  gSup.in = doc["in"] | 0;
  gSup.ma = doc["ma"] | 0;
  gSup.resume = strcmp(doc["md"] | "t", "r") == 0;
  gSup.extra = doc["x"] | 0;
  gSup.count = 0;
  for (JsonObject s : doc["s"].as<JsonArray>()) {
    if (gSup.count >= MAX_SUP_ITEMS) break;
    copyStr(gSup.names[gSup.count], NAME_BYTES, s["n"] | "?");
    gSup.st[gSup.count] = s["e"] | 0;
    gSup.count++;
  }

  JsonObject al = doc["al"];
  gSup.alert = !al.isNull();
  if (gSup.alert) {
    copyStr(gSup.alName, sizeof(gSup.alName), al["n"] | "?");
    gSup.alMin = al["m"] | 0;
    gSup.alCount = al["k"] | 1;
  }
  if (supHash() != before) gSup.version++;
}
