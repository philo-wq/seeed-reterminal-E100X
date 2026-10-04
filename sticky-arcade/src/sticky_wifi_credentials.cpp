#include "sticky_wifi_credentials.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include "config_storage.h"
#include "secrets.h"
#include "wifi_schema.h"

namespace sticky_wifi {

namespace {

struct Slot {
  const char* ssidKey;
  const char* passwordKey;
};

// NVS keys must be <= 15 chars; the wifi namespace is shared with the
// legacy portal schema (keys "ssid"/"password"), so extras use indexed keys.
constexpr Slot kSlots[kMaxProfiles] = {
    {"ssid", "password"},   // legacy keys: primary profile
    {"ssid1", "password1"},
    {"ssid2", "password2"},
};

Profile g_profiles[kMaxProfiles];
int g_active = kInvalidProfile;
int g_count = 0;
bool g_nvsEmpty = true;
bool g_loaded = false;

void loadSlot(config_portal::storage::PrefsStorage& prefs, int index) {
  Profile p;
  p.ssid = prefs.getString(kSlots[index].ssidKey, "");
  p.password = prefs.getString(kSlots[index].passwordKey, "");
  if (p.valid()) {
    g_profiles[index] = p;
    ++g_count;
  }
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  PrefsStorage prefs;
  if (prefs.begin(config_portal::kWifiSchema.nvsNamespace,
                  /*readOnly=*/true)) {
    loadSlot(prefs, 0);
    loadSlot(prefs, 1);
    loadSlot(prefs, 2);
    const int active = prefs.getString("active", "").toInt();
    prefs.end();
    g_nvsEmpty = g_count == 0;
    if (!g_nvsEmpty) {
      g_active = (active >= 0 && active < kMaxProfiles &&
                  g_profiles[active].valid())
                     ? active
                     : 0;
      g_loaded = true;
      return;
    }
  }
  // NVS empty: fall back to the compile-time secrets for the primary slot.
  g_profiles[0].ssid = WIFI_SSID;
  g_profiles[0].password = WIFI_PASSWORD;
  if (g_profiles[0].valid()) {
    g_count = 1;
    g_active = 0;
    g_nvsEmpty = false;
  } else {
    g_profiles[0] = Profile();
    g_count = 0;
    g_active = kInvalidProfile;
  }
  g_loaded = true;
}

bool haveCredentials() {
  if (!g_loaded) load();
  return g_count > 0;
}

bool nvsEmpty() {
  if (!g_loaded) load();
  return g_nvsEmpty;
}

int profileCount() {
  if (!g_loaded) load();
  return g_count;
}

Profile profile(int index) {
  if (!g_loaded) load();
  if (index < 0 || index >= kMaxProfiles) return Profile();
  return g_profiles[index];
}

int activeProfile() {
  if (!g_loaded) load();
  return g_active;
}

void markActive(int index) {
  if (index < 0 || index >= kMaxProfiles) return;
  g_active = index;
  Preferences prefs;
  if (prefs.begin(config_portal::kWifiSchema.nvsNamespace,
                  /*readOnly=*/false)) {
    prefs.putString("active", String(index));
    prefs.end();
  }
}

// Legacy single-value accessors used by the config portal fallback and
// status text: expose the active profile (falling back to slot 0).
const char* ssid() {
  if (!g_loaded) load();
  const int index = g_active >= 0 ? g_active : 0;
  return index < kMaxProfiles ? g_profiles[index].ssid.c_str() : "";
}

const char* password() {
  if (!g_loaded) load();
  const int index = g_active >= 0 ? g_active : 0;
  return index < kMaxProfiles ? g_profiles[index].password.c_str() : "";
}

}  // namespace sticky_wifi
