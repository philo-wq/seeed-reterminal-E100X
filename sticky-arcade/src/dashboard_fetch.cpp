#include "dashboard_fetch.h"

#include <Arduino.h>
#include <HTTPClient.h>

#include "app_logger.h"
#include "runners_journal_config.h"
#include "secrets.h"
#include "trusted_client.h"

namespace dashboard_fetch {

namespace {

// Single attempt fetch - returns false on any error
bool fetchOnce(HTTPClient& http, String& body, String& failureReason) {
  // Check Content-Length before downloading
  if (http.hasHeader("Content-Length")) {
    const size_t contentLength = static_cast<size_t>(http.header("Content-Length").toInt());
    if (contentLength > config::MAX_DASHBOARD_BODY_BYTES) {
      failureReason = "Content-Length exceeds maximum";
      return false;
    }
  }

  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Content-Type", "application/json");

  const int status = http.POST("{}");

  // No retry for auth/permission errors
  if (status == 401 || status == 403 || status == 404) {
    failureReason = String("HTTP ") + status;
    return false;
  }

  if (status != 200) {
    const String errBody = http.getString().substring(0, 200);
    failureReason = String("HTTP ") + status + ": " + errBody;
    return false;
  }

  const String response = http.getString();

  if (response.length() == 0) {
    failureReason = "Empty body";
    return false;
  }

  if (static_cast<size_t>(response.length()) > config::MAX_DASHBOARD_BODY_BYTES) {
    failureReason = "Body too large";
    return false;
  }

  body = response;
  return true;
}

}  // namespace

bool fetch(String& body, String& failureReason) {
  failureReason = "";

  for (int attempt = 1; attempt <= config::HTTP_MAX_ATTEMPTS; ++attempt) {
    if (attempt > 1) {
      // Exponential backoff: 2s, 4s for attempts 2 and 3
      const uint32_t backoffMs = config::HTTP_RETRY_BACKOFF_MS * (1 << (attempt - 2));
      LOG.printf("[dashboard] fetch attempt %d/%d failed, retrying in %lu ms\n",
                 attempt - 1, config::HTTP_MAX_ATTEMPTS, backoffMs);
      delay(backoffMs);
    }

    tls_client::DefaultRootClient client;
    client.setTimeout(config::HTTP_TIMEOUT_MS);

    HTTPClient http;
    http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
    http.setTimeout(config::HTTP_TIMEOUT_MS);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, config::SUPABASE_URL)) {
      LOG.printf("[dashboard] fetch attempt %d: could not start HTTPS request\n", attempt);
      if (attempt == config::HTTP_MAX_ATTEMPTS) {
        failureReason = "Could not start HTTPS request";
      }
      http.end();
      continue;
    }

    const bool success = fetchOnce(http, body, failureReason);
    http.end();

    if (success) {
      return true;
    }

    if (attempt == config::HTTP_MAX_ATTEMPTS) {
      LOG.printf("[dashboard] fetch failed after %d attempts: %s\n",
                 config::HTTP_MAX_ATTEMPTS, failureReason.c_str());
    }
  }

  return false;
}

}  // namespace dashboard_fetch
