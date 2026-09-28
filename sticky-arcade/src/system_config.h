#pragma once

#include <Arduino.h>

#ifndef RETERMINAL_MODEL
#define RETERMINAL_MODEL 1005
#endif

#include "panel_traits.h"

namespace config {

constexpr int MODEL = panel_traits::MODEL;
constexpr int PANEL_WIDTH = panel_traits::WIDTH;
constexpr int PANEL_HEIGHT = panel_traits::HEIGHT;
// E1005 is mounted opposite Seeed_GFX's default portrait orientation.
constexpr int PANEL_ROTATION = panel_traits::DISPLAY_ROTATION;
// POSIX timezone for NTP/local time. Norway (CET/CEST).
constexpr char TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
// Arduino-ESP32 can defer the initial SNTP request by up to five seconds.
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 6000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;

}  // namespace config
