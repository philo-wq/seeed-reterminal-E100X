#pragma once

#include <Arduino.h>

namespace sticky_wifi {

void load();
bool haveCredentials();
bool nvsEmpty();
const char* ssid();
const char* password();

}  // namespace sticky_wifi
