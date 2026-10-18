#include "dashboard_parse.h"

#include <ArduinoJson.h>

// For logging - use common include path
#include "app_logger.h"

namespace dashboard {

namespace {

#if defined(DASHBOARD_PARSE_DEBUG)
#define PARSE_LOG(tag, msg) LOG.printf("[dashboard:%s] %s\n", tag, msg)
#else
#define PARSE_LOG(tag, msg) (void)0
#endif

// Helper to copy from JsonVariant to char buffer with logging
inline void copyJsonStr(const JsonVariant& src, char* dst, size_t cap, const char* fieldName) {
  const char* str = src | "";
  if (str[0] == '\0') {
    PARSE_LOG("copyJsonStr", (String("empty field: ") + fieldName).c_str());
  }
  copyStr(dst, cap, str);
  if (strlen(dst) >= cap - 1) {
    PARSE_LOG("copyJsonStr", (String("truncated: ") + fieldName).c_str());
  }
}

void parseWeek(const JsonObject& o, WeekSummary& w) {
  if (o.isNull()) {
    PARSE_LOG("parseWeek", "null week object");
    return;
  }
  copyJsonStr(o["merkelapp"], w.merkelapp, sizeof(w.merkelapp), "merkelapp");
  w.total_km = o["total_km"] | 0.0f;
  w.antall = o["antall"] | 0;
  w.maal_pct = o["maal_pct"] | 0;
  copyJsonStr(o["mot_forrige_km"], w.mot_forrige_km, sizeof(w.mot_forrige_km), "mot_forrige_km");
  w.total_tid_s = o["total_tid_s"] | 0u;
  copyJsonStr(o["total_tid"], w.total_tid, sizeof(w.total_tid), "total_tid");
  w.elevation_m = o["elevation_m"] | 0;
}

void parseHistorikk(const JsonArray& arr, std::vector<HistoryEntry>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parseHistorikk", "null historikk array");
    return;
  }
  out.reserve(arr.size());
  for (size_t i = 0; i < arr.size(); ++i) {
    const JsonVariant& v = arr[i];
    const JsonObject& o = v.as<JsonObject>();
    if (o.isNull()) {
      PARSE_LOG("parseHistorikk", (String("null object at index ") + String(i)).c_str());
      continue;
    }
    HistoryEntry e;
    copyJsonStr(o["uke"], e.uke, sizeof(e.uke), (String("uke[") + String(i) + "]").c_str());
    e.km = o["km"] | 0.0f;
    e.naa = o["naa"] | false;
    out.push_back(std::move(e));
  }
}

void parseTyper(const JsonArray& arr, std::vector<TypeEntry>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parseTyper", "null typer array");
    return;
  }
  out.reserve(arr.size());
  for (size_t i = 0; i < arr.size(); ++i) {
    const JsonVariant& v = arr[i];
    const JsonObject& o = v.as<JsonObject>();
    if (o.isNull()) {
      PARSE_LOG("parseTyper", (String("null object at index ") + String(i)).c_str());
      continue;
    }
    TypeEntry e;
    copyJsonStr(o["type"], e.type, sizeof(e.type), (String("type[") + String(i) + "]").c_str());
    e.antall = o["antall"] | 0;
    e.km = o["km"] | 0.0f;
    out.push_back(std::move(e));
  }
}

void parseSisteLop(const JsonArray& arr, std::vector<RunEntry>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parseSisteLop", "null siste_lop array");
    return;
  }
  out.reserve(arr.size());
  for (size_t i = 0; i < arr.size(); ++i) {
    const JsonVariant& v = arr[i];
    const JsonObject& o = v.as<JsonObject>();
    if (o.isNull()) {
      PARSE_LOG("parseSisteLop", (String("null object at index ") + String(i)).c_str());
      continue;
    }
    RunEntry e;
    copyJsonStr(o["dato"], e.dato, sizeof(e.dato), (String("dato[") + String(i) + "]").c_str());
    copyJsonStr(o["type"], e.type, sizeof(e.type), (String("type[") + String(i) + "]").c_str());
    e.km = o["km"] | 0.0f;
    copyJsonStr(o["pace"], e.pace, sizeof(e.pace), (String("pace[") + String(i) + "]").c_str());
    e.elevation_m = o["elevation_m"] | 0;
    out.push_back(std::move(e));
  }
}

void parseJournal(const JsonArray& arr, std::vector<JournalEntry>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parseJournal", "null journal array");
    return;
  }
  out.reserve(arr.size());
  for (size_t i = 0; i < arr.size(); ++i) {
    const JsonVariant& v = arr[i];
    const JsonObject& o = v.as<JsonObject>();
    if (o.isNull()) {
      PARSE_LOG("parseJournal", (String("null object at index ") + String(i)).c_str());
      continue;
    }
    JournalEntry e;
    copyJsonStr(o["dato"], e.dato, sizeof(e.dato), (String("dato[") + String(i) + "]").c_str());
    copyJsonStr(o["type"], e.type, sizeof(e.type), (String("type[") + String(i) + "]").c_str());
    copyJsonStr(o["note"], e.note, sizeof(e.note), (String("note[") + String(i) + "]").c_str());
    out.push_back(std::move(e));
  }
}

void parseRuter(const JsonArray& arr, std::vector<RouteEntry>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parseRuter", "null ruter array");
    return;
  }
  out.reserve(arr.size());
  for (size_t i = 0; i < arr.size(); ++i) {
    const JsonVariant& v = arr[i];
    const JsonObject& o = v.as<JsonObject>();
    if (o.isNull()) {
      PARSE_LOG("parseRuter", (String("null object at index ") + String(i)).c_str());
      continue;
    }
    RouteEntry e;
    copyJsonStr(o["navn"], e.navn, sizeof(e.navn), (String("navn[") + String(i) + "]").c_str());
    copyJsonStr(o["type"], e.type, sizeof(e.type), (String("type[") + String(i) + "]").c_str());
    const JsonArray rute = o["rute"];
    if (rute.isNull() || rute.size() < 2) {
      PARSE_LOG("parseRuter", (String("skipping route at index ") + String(i) + " (null or small)").c_str());
      continue;
    }
    e.rute.reserve(rute.size() > 200 ? 200 : rute.size());
    for (const JsonVariant& p : rute) {
      const JsonArray& pair = p.as<JsonArray>();
      if (pair.isNull() || pair.size() < 2) continue;
      const float lat = pair[0] | 0.0f;
      const float lng = pair[1] | 0.0f;
      if (lat == 0.0f && lng == 0.0f) continue;
      e.rute.push_back({lat, lng});
    }
    if (e.rute.size() >= 2) {
      out.push_back(std::move(e));
    }
  }
}

void parsePunktArray(const JsonArray& arr, std::vector<RoutePoint>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parsePunktArray", "null array");
    return;
  }
  out.reserve(arr.size() > 64 ? 64 : arr.size());
  for (const JsonVariant& p : arr) {
    const JsonArray& pair = p.as<JsonArray>();
    if (pair.isNull() || pair.size() < 2) continue;
    const float lat = pair[0] | 0.0f;
    const float lng = pair[1] | 0.0f;
    if (lat == 0.0f && lng == 0.0f) continue;
    out.push_back({lat, lng});
  }
}

void parseKartFeatureList(const JsonArray& arr,
                          std::vector<std::vector<RoutePoint>>& out) {
  out.clear();
  if (arr.isNull()) {
    PARSE_LOG("parseKartFeatureList", "null array");
    return;
  }
  out.reserve(arr.size());
  for (const JsonVariant& v : arr) {
    const JsonArray& line = v.as<JsonArray>();
    if (line.isNull() || line.size() < 2) continue;
    std::vector<RoutePoint> pts;
    parsePunktArray(line, pts);
    if (pts.size() >= 2) out.push_back(std::move(pts));
  }
}

}  // namespace

bool parse(const String& body, DashboardData& out) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err != DeserializationError::Ok) {
    PARSE_LOG("parse", (String("deserialize error: ") + err.c_str()).c_str());
    return false;
  }
  const JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    PARSE_LOG("parse", "null root object");
    return false;
  }

  copyJsonStr(root["oppdatert"], out.oppdatert, sizeof(out.oppdatert), "oppdatert");
  out.neste_oppvakning_s = root["neste_oppvakning_s"] | 0u;
  copyJsonStr(root["maal_km"], out.maal_km, sizeof(out.maal_km), "maal_km");

  const JsonObject ukeObj = root["uke"];
  if (ukeObj.isNull()) {
    PARSE_LOG("parse", "missing uke object");
  } else {
    parseWeek(ukeObj, out.uke);
  }

  const JsonObject aarObj = root["aar"];
  if (aarObj.isNull()) {
    PARSE_LOG("parse", "missing aar object");
  } else {
    out.aar.total_km = aarObj["total_km"] | 0.0f;
  }

  const JsonArray historikk = root["historikk"];
  if (historikk.isNull()) {
    PARSE_LOG("parse", "missing historikk array");
  } else {
    parseHistorikk(historikk, out.historikk);
  }

  const JsonArray typer = root["typer"];
  if (typer.isNull()) {
    PARSE_LOG("parse", "missing typer array");
  } else {
    parseTyper(typer, out.typer);
  }

  const JsonArray sisteLop = root["siste_lop"];
  if (sisteLop.isNull()) {
    PARSE_LOG("parse", "missing siste_lop array");
  } else {
    parseSisteLop(sisteLop, out.siste_lop);
  }

  const JsonArray journal = root["journal"];
  if (journal.isNull()) {
    PARSE_LOG("parse", "missing journal array");
  } else {
    parseJournal(journal, out.journal);
  }

  const JsonArray ruter = root["ruter"];
  if (ruter.isNull()) {
    PARSE_LOG("parse", "missing ruter array");
  } else {
    parseRuter(ruter, out.ruter);
  }

  const JsonObject kart = root["kart"];
  if (!kart.isNull()) {
    out.kart.finnes = true;
    const JsonArray stier = kart["stier"];
    if (!stier.isNull()) parseKartFeatureList(stier, out.kart.stier);
    const JsonArray vann = kart["vann"];
    if (!vann.isNull()) parseKartFeatureList(vann, out.kart.vann);
    const JsonArray skog = kart["skog"];
    if (!skog.isNull()) parseKartFeatureList(skog, out.kart.skog);
  }

  const JsonObject kb = root["kart_bilde"];
  if (!kb.isNull()) {
    out.kart_bilde.finnes = true;
    out.kart_bilde.bredde = kb["bredde"] | 0;
    out.kart_bilde.hoyde = kb["hoyde"] | 0;
    out.kart_bilde.min_lat = kb["min_lat"] | 0.0f;
    out.kart_bilde.max_lat = kb["max_lat"] | 0.0f;
    out.kart_bilde.min_lng = kb["min_lng"] | 0.0f;
    out.kart_bilde.max_lng = kb["max_lng"] | 0.0f;
    out.kart_bilde.data = kb["data"] | "";
  }

  return true;
}

}  // namespace dashboard
