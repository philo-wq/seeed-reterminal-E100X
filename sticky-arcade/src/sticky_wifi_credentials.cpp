#include "sticky_wifi_credentials.h"

#include <Arduino.h>
#include <SD.h>

#include "config_storage.h"
#include "secrets.h"
#include "wifi_schema.h"

namespace sticky_wifi {

namespace {

String g_ssid;
String g_password;
bool g_nvsEmpty = true;
bool g_loaded = false;

bool isPlaceholder(const String& ssid) {
  return ssid == "YOUR_WIFI_NAME" || ssid.length() == 0;
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  PrefsStorage prefs;
  if (prefs.begin(config_portal::kWifiSchema.nvsNamespace,
                  /*readOnly=*/true)) {
    const String nvsSsid = prefs.getString("ssid", "");
    const String nvsPassword = prefs.getString("password", "");
    prefs.end();
    g_nvsEmpty = nvsSsid.length() == 0;
    if (!g_nvsEmpty) {
      g_ssid = nvsSsid;
      g_password = nvsPassword;
      g_loaded = true;
      return;
    }
  }
  g_ssid = WIFI_SSID;
  g_password = WIFI_PASSWORD;
  if (isPlaceholder(g_ssid)) {
    g_ssid = "";
    g_password = "";
  }
  g_loaded = true;
}

bool haveCredentials() {
  if (!g_loaded) load();
  return g_ssid.length() != 0;
}

bool nvsEmpty() {
  if (!g_loaded) load();
  return g_nvsEmpty;
}

const char* ssid() {
  if (!g_loaded) load();
  return g_ssid.c_str();
}

const char* password() {
  if (!g_loaded) load();
  return g_password.c_str();
}

}  // namespace sticky_wifi
