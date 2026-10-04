#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace sticky_wifi {

constexpr int kMaxProfiles = 3;
constexpr int kInvalidProfile = -1;

struct Profile {
  String ssid;
  String password;
  bool valid() const { return ssid.length() != 0 && ssid != "YOUR_WIFI_NAME"; }
};

// Multi-profile credential store. Profile 0 is the "primary" slot and is
// back-compatible with the legacy single-SSID portal configuration; slots 1
// and 2 are extra networks (e.g. work, cabin). One profile is remembered as
// the last successful connection and tried first.
void load();

bool haveCredentials();
bool nvsEmpty();

// Number of stored profiles (0..kMaxProfiles).
int profileCount();
// Profile by slot index, or an invalid Profile when the slot is empty.
Profile profile(int index);
// Slot index of the last profile that connected, or kInvalidProfile.
int activeProfile();
// Marks which profile connected last (persisted to NVS).
void markActive(int index);

// Config portal plumbing (legacy single-value accessors).
const char* ssid();
const char* password();

}  // namespace sticky_wifi
