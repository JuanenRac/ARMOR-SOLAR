// ARMOR-SOLAR - what the panel does over HTTP: the status, the ports, the readings, the settings and the search for Wi-Fi networks.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <string_view>

#include "core/solar_config.hpp"

namespace armor::api {

std::string version_text();
std::string status_json();          // node, network, broker and ports, as the Overview page shows them
std::string ports_json();           // {"ports":[...],"catalog":[...]}: the state of the ten ports and the pins the board offers
std::string readings_json();        // the last reading of every port that has one: the messages the node publishes
std::string config_get_json();      // {"config":{...},"channel_auto":n,"firmware":"x.y.z"}: no password ever leaves the node
std::string problems_json(const config::Problems& problems);   // [{"path":..,"code":..}]

enum class PutResult { kSaved, kInvalid, kStorage };
// Applies a (partial) settings document on top of the stored one: checked in full, and stored only when nothing is wrong.
PutResult put_config(std::string_view document, config::Problems& problems, bool& restart_required);

// The Wi-Fi networks in range: {"networks":[{"ssid","rssi","channel","security"}]}, strongest first. False, with a code, when the radio is busy.
bool wifi_scan_json(std::string& data, std::string& error);

}  // namespace armor::api
