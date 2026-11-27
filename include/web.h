#pragma once

#include <Arduino.h>

// Wi-Fi and the web UI.
// No saved network (or it cannot connect): the board opens its own network
// "PowderReload" for setup at http://192.168.4.1. Otherwise it joins the saved
// network and serves http://powderreload.local.
namespace Web {
constexpr const char* SetupSsid = "PowderReload";
constexpr const char* SetupPassword = "powderreload";
constexpr const char* Hostname = "powderreload";

enum class Mode { Connecting, Connected, Setup };

void begin();
void loop();
Mode mode();
String network();  // joined network, or the setup network name
String address();  // IP address
void forget();     // erase the saved network and restart in setup mode
}
