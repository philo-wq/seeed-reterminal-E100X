#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>

namespace dashboard {

// Helper for NUL-guaranteed string copy with truncation
inline void copyStr(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  while (i < cap - 1 && src[i] != '\0') {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

struct WeekSummary {
  char merkelapp[24] = "";
  float total_km = 0.0f;
  int antall = 0;
  int maal_pct = 0;
  char mot_forrige_km[16] = "";
  uint32_t total_tid_s = 0;
  char total_tid[12] = "";
  int elevation_m = 0;
};

struct YearSummary {
  float total_km = 0.0f;
};

struct HistoryEntry {
  char uke[12] = "";
  float km = 0.0f;
  bool naa = false;
};

struct TypeEntry {
  char type[24] = "";
  int antall = 0;
  float km = 0.0f;
};

struct RunEntry {
  char dato[16] = "";
  char type[24] = "";
  float km = 0.0f;
  char pace[12] = "";
  int elevation_m = 0;
};

struct JournalEntry {
  char dato[16] = "";
  char type[24] = "";
  char note[256] = "";
};

struct RoutePoint {
  float lat = 0.0f;
  float lng = 0.0f;
};

struct RouteEntry {
  char navn[48] = "";
  char type[24] = "";
  std::vector<RoutePoint> rute;
};

struct KartBilde {
  int bredde = 0;
  int hoyde = 0;
  float min_lat = 0, max_lat = 0, min_lng = 0, max_lng = 0;
  String data;  // Exception: stays String (up to 64KB RLE payload)
  bool finnes = false;
};

struct KartUnderlag {
  std::vector<std::vector<RoutePoint>> stier;
  std::vector<std::vector<RoutePoint>> vann;
  std::vector<std::vector<RoutePoint>> skog;
  bool finnes = false;
};

struct DashboardData {
  char oppdatert[32] = "";
  uint32_t neste_oppvakning_s = 0;
  char maal_km[12] = "";
  WeekSummary uke;
  YearSummary aar;
  std::vector<HistoryEntry> historikk;
  std::vector<TypeEntry> typer;
  std::vector<RunEntry> siste_lop;
  std::vector<JournalEntry> journal;
  std::vector<RouteEntry> ruter;
  KartUnderlag kart;
  KartBilde kart_bilde;
};

}  // namespace dashboard
