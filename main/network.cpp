// ARMOR-SOLAR - the node's network: the Wi-Fi station and the Wi-Fi access point.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Nothing here has run on a board. The station joins the network of the settings and joins it again 3 seconds after any loss; the access point (the setup
// one, or the one the settings ask for) has its own address range (192.168.4.x) and the node's own panel is on 192.168.4.1.
#include "network.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
extern "C" {
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
}
#include "core/net_text.hpp"

namespace armor::network {
namespace {
constexpr char kTag[] = "armor-net";

std::mutex g_lock;
Status g_status;
netplan::Plan g_plan;
config::Settings g_settings;
esp_timer_handle_t g_reconnect_timer = nullptr;
bool g_wifi_running = false;   // the driver was started by start()
std::mutex g_scan_lock;

std::string text_of(const esp_ip4_addr_t& address) {
  char text[16];
  std::snprintf(text, sizeof text, IPSTR, IP2STR(&address));
  return text;
}

std::string mac_text(const std::uint8_t* mac) {
  char text[18];
  std::snprintf(text, sizeof text, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return text;
}

void refresh_ip(esp_netif_t* netif) {
  esp_netif_ip_info_t info{};
  if (netif == nullptr || esp_netif_get_ip_info(netif, &info) != ESP_OK) return;
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.ip = text_of(info.ip);
  g_status.netmask = text_of(info.netmask);
  g_status.gateway = text_of(info.gw);
  esp_netif_dns_info_t dns{};
  if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) g_status.dns = text_of(dns.ip.u_addr.ip4);
  g_status.has_ip = info.ip.addr != 0;
}

void on_ip_event(void*, esp_event_base_t, int32_t, void* data) {
  const auto* event = static_cast<ip_event_got_ip_t*>(data);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_status.ip = text_of(event->ip_info.ip);
    g_status.netmask = text_of(event->ip_info.netmask);
    g_status.gateway = text_of(event->ip_info.gw);
    g_status.has_ip = true;
  }
  refresh_ip(event->esp_netif);
  ESP_LOGI(kTag, "address %s, gateway " IPSTR ", netmask " IPSTR, g_status.ip.c_str(), IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask));
}

void on_ip_lost(void*, esp_event_base_t, int32_t, void*) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.has_ip = false;
  g_status.ip.clear();
}

void reconnect_station(void*) { esp_wifi_connect(); }

void on_wifi_event(void*, esp_event_base_t, int32_t event_id, void* data) {
  switch (event_id) {
    case WIFI_EVENT_AP_START: ESP_LOGI(kTag, "access point \"%s\" started on channel %d", g_plan.ap.ssid.c_str(), g_plan.ap.channel); break;
    case WIFI_EVENT_AP_STACONNECTED: {
      const auto* event = static_cast<wifi_event_ap_staconnected_t*>(data);
      ESP_LOGI(kTag, "a client joined the access point: %s", mac_text(event->mac).c_str());
      break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
      const auto* event = static_cast<wifi_event_ap_stadisconnected_t*>(data);
      ESP_LOGI(kTag, "a client left the access point: %s", mac_text(event->mac).c_str());
      break;
    }
    case WIFI_EVENT_STA_START: esp_wifi_connect(); break;
    case WIFI_EVENT_STA_CONNECTED: {
      std::lock_guard<std::mutex> guard(g_lock);
      g_status.link_up = true;
      g_status.sta_connected = true;
      g_status.sta_ssid = g_settings.sta.ssid;
      ESP_LOGI(kTag, "joined the Wi-Fi network \"%s\"", g_settings.sta.ssid.c_str());
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      {
        std::lock_guard<std::mutex> guard(g_lock);
        g_status.link_up = false;
        g_status.sta_connected = false;
        g_status.has_ip = false;
      }
      ESP_LOGW(kTag, "the Wi-Fi network is not reachable: trying again in 3 s");
      if (g_reconnect_timer != nullptr) esp_timer_start_once(g_reconnect_timer, 3 * 1000 * 1000);
      break;
    }
    default: break;
  }
}

wifi_auth_mode_t auth_mode(config::WifiSecurity security) {
  switch (security) {
    case config::WifiSecurity::kOpen: return WIFI_AUTH_OPEN;
    case config::WifiSecurity::kWpa2: return WIFI_AUTH_WPA2_PSK;
    case config::WifiSecurity::kWpa3: return WIFI_AUTH_WPA3_PSK;
    case config::WifiSecurity::kWpa2Wpa3: return WIFI_AUTH_WPA2_WPA3_PSK;
  }
  return WIFI_AUTH_WPA2_PSK;
}

bool wifi_setup(const config::Settings& s, const netplan::Plan& plan) {
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) { ESP_LOGE(kTag, "the Wi-Fi driver could not be initialised"); return false; }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // the settings live in this node's own flash store, not in the driver's
  const std::string& country = plan.ap.enabled ? plan.ap.country : s.ap.country;
  if (esp_wifi_set_country_code(country.c_str(), true) != ESP_OK) ESP_LOGW(kTag, "the country code %s was not accepted: the default channels apply", country.c_str());

  const bool access_point = plan.ap.enabled;
  esp_wifi_set_mode(plan.station && access_point ? WIFI_MODE_APSTA : (plan.station ? WIFI_MODE_STA : WIFI_MODE_AP));
  if (access_point) {
    wifi_config_t ap{};
    std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), plan.ap.ssid.c_str(), sizeof ap.ap.ssid - 1);
    ap.ap.ssid_len = static_cast<std::uint8_t>(plan.ap.ssid.size());
    if (plan.ap.security != config::WifiSecurity::kOpen) std::strncpy(reinterpret_cast<char*>(ap.ap.password), plan.ap.password.c_str(), sizeof ap.ap.password - 1);
    ap.ap.channel = static_cast<std::uint8_t>(plan.ap.channel);
    ap.ap.authmode = auth_mode(plan.ap.security);
    ap.ap.ssid_hidden = plan.ap.hidden ? 1 : 0;
    ap.ap.max_connection = static_cast<std::uint8_t>(plan.ap.max_clients);
    ap.ap.beacon_interval = 100;
    if (plan.ap.security == config::WifiSecurity::kWpa3) ap.ap.pmf_cfg.required = true;
    if (plan.ap.security == config::WifiSecurity::kWpa2Wpa3) ap.ap.pmf_cfg.capable = true;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) { ESP_LOGE(kTag, "the access point settings were refused"); return false; }
  }
  if (plan.station) {
    wifi_config_t sta{};
    std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), s.sta.ssid.c_str(), sizeof sta.sta.ssid - 1);
    std::strncpy(reinterpret_cast<char*>(sta.sta.password), s.sta.password.c_str(), sizeof sta.sta.password - 1);
    sta.sta.threshold.authmode = s.sta.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) { ESP_LOGE(kTag, "the station settings were refused"); return false; }
  }
  esp_wifi_set_ps(WIFI_PS_NONE);  // an access point that sleeps answers late
  return true;
}

void wifi_tune(const netplan::Plan& plan) {
  if (!plan.ap.enabled) return;
  esp_wifi_set_bandwidth(WIFI_IF_AP, plan.ap.bandwidth_mhz == 40 ? WIFI_BW_HT40 : WIFI_BW_HT20);
  esp_wifi_set_max_tx_power(static_cast<std::int8_t>(plan.ap.tx_power_dbm * 4));  // the driver counts in quarter dBm
}
}  // namespace

bool start(const config::Settings& s, const netplan::Plan& plan) {
  g_plan = plan;
  g_settings = s;
  g_status.layout = netplan::to_text(plan.layout);
  g_status.ap_active = plan.ap.enabled;
  g_status.ap_setup = plan.ap.setup;
  g_status.ap_ssid = plan.ap.ssid;
  g_status.ap_channel = plan.ap.enabled ? plan.ap.channel : 0;
  std::uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  g_status.mac = mac_text(mac);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_ip_lost, nullptr));

  if (plan.station) {
    esp_timer_create_args_t timer{};
    timer.callback = &reconnect_station;
    timer.name = "wifi-reconnect";
    esp_timer_create(&timer, &g_reconnect_timer);
  }
  if (plan.ap.enabled) esp_netif_create_default_wifi_ap();
  if (plan.station) {
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta_netif, plan.hostname.c_str());
  }
  if (!wifi_setup(s, plan)) return false;
  ESP_ERROR_CHECK(esp_wifi_start());
  wifi_tune(plan);
  g_wifi_running = true;
  return true;
}

namespace {
const char* security_text(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2wpa3";
    case WIFI_AUTH_WPA2_ENTERPRISE: case WIFI_AUTH_WPA3_ENTERPRISE: case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "enterprise";
    default: return "wpa2";
  }
}
}  // namespace

bool scan(std::vector<ScanEntry>& out, std::string& error) {
  out.clear();
  std::unique_lock<std::mutex> guard(g_scan_lock, std::try_to_lock);
  if (!guard.owns_lock()) { error = "wifi_busy"; return false; }
  if (!g_wifi_running) { error = "wifi_unavailable"; return false; }
  wifi_mode_t previous = WIFI_MODE_NULL;
  esp_wifi_get_mode(&previous);
  if (previous == WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_APSTA);   // an access point alone cannot search
  wifi_scan_config_t config{};
  config.show_hidden = false;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 80;
  config.scan_time.active.max = 200;
  const esp_err_t result = esp_wifi_scan_start(&config, true);   // blocks until the search is over
  bool ok = result == ESP_OK;
  if (ok) {
    std::uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > 40) count = 40;
    std::vector<wifi_ap_record_t> records(count);
    if (count > 0) esp_wifi_scan_get_ap_records(&count, records.data());
    records.resize(count);
    std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) { return a.rssi > b.rssi; });
    for (const wifi_ap_record_t& record : records) {
      const std::string name(reinterpret_cast<const char*>(record.ssid));
      if (name.empty() || out.size() >= 25) continue;
      if (std::any_of(out.begin(), out.end(), [&](const ScanEntry& e) { return e.ssid == name; })) continue;   // the strongest of a name is kept
      out.push_back({name, record.rssi, record.primary, security_text(record.authmode)});
    }
  } else {
    esp_wifi_clear_ap_list();
    error = result == ESP_ERR_WIFI_STATE ? "wifi_busy" : "scan_failed";
  }
  if (previous == WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_AP);
  return ok;
}

bool has_ip() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_status.has_ip;
}

// The node can be reached when its station has an address, or an access point is up.
bool reachable() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_status.has_ip || g_status.ap_active;
}

Status status() {
  Status copy;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    copy = g_status;
  }
  if (copy.ap_active) {
    wifi_sta_list_t list{};
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) copy.ap_clients = list.num;
  }
  if (copy.sta_connected) {
    wifi_ap_record_t record{};
    if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) copy.sta_rssi = record.rssi;
  }
  return copy;
}

}  // namespace armor::network
