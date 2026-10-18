#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace config {

constexpr char SUPABASE_URL[] =
    "https://tifeelcjwaydbspbabej.supabase.co/rest/v1/rpc/dashboard";

constexpr uint32_t FALLBACK_SLEEP_SECONDS = 6ULL * 60ULL * 60ULL;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30ULL * 1000ULL;
constexpr uint32_t HTTP_TIMEOUT_MS = 10ULL * 1000ULL;  // Reduced from 30s

constexpr size_t MAX_DASHBOARD_BODY_BYTES = 64ULL * 1024ULL;

// HTTP retry configuration
constexpr int HTTP_MAX_ATTEMPTS = 3;
constexpr uint32_t HTTP_RETRY_BACKOFF_MS = 2000;

// How long the paging view stays interactive before timing out and
// returning to deep sleep (5 minutes of button inactivity).
constexpr uint32_t PAGING_TIMEOUT_MS = 5ULL * 60ULL * 1000ULL;

}  // namespace config

#include "system_config.h"
