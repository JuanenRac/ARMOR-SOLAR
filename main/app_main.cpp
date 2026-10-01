/*
 * ARMOR-SOLAR - ESP32-S3 solar gateway node (ESP32-S3-WROOM-1 N16R8, Wi-Fi only): reads solar inverters and batteries through up to ten serial ports and
 * publishes their readings to the A.R.M.O.R. server.
 * Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
 *
 * The hardware-independent core under core/ (the protocols, the settings, the poller, the emulated UART's arithmetic) is built and tested on a computer
 * (tests/); the files beside this one are built with ESP-IDF 5.4 (tools/build_node.sh does it in a container). None of it has run on a board yet, and no
 * inverter or battery has been connected: docs/NODE_FIRMWARE.md has the bench checklist.
 *
 * What a node is, in one paragraph: everything that differs from node to node (its identity, its Wi-Fi, its broker and what is on each of its ten serial
 * ports) is one settings document in flash, edited from the node's own web panel, so the same firmware image serves every node and no password is compiled
 * in. A node with no user yet is in setup: it opens the network "ARMOR-SETUP-xxxxxx" and shows a setup code on its USB console. A node may read only
 * inverters, only batteries or a mix: every port is independent. The firmware can be replaced from the panel; a new image that does not come up is rolled
 * back by the boot loader.
 */
#include <cstdio>
#include <cstring>
extern "C" {
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
}
#include "core/netplan.hpp"
#include "log_buffer.hpp"
#include "ble_provision.hpp"
#include "mqtt_link.hpp"
#include "network.hpp"
#include "node_store.hpp"
#include "solar_manager.hpp"
#include "web_server.hpp"

namespace {
constexpr char kTag[] = "armor-node";

// While the node has no user, says every 15 seconds how to set it up. The code appears only here, on the USB console.
void setup_reminder_task(void*) {
  for (;;) {
    if (!armor::store::users_empty()) vTaskDelete(nullptr);
    const armor::network::Status n = armor::network::status();
    ESP_LOGW(kTag, "SETUP: this node has no user yet. Join the Wi-Fi \"%s\", open http://192.168.4.1/ and enter the setup code %s",
             n.ap_ssid.empty() ? "ARMOR-SETUP-..." : n.ap_ssid.c_str(), armor::store::setup_code().c_str());
    vTaskDelay(pdMS_TO_TICKS(15000));
  }
}

// Holding the BOOT button for 8 seconds in the first 30 after power-up erases the settings and the users: the node is back in setup.
// (Skipped when the operator gave GPIO 0 to a serial port.)
void boot_button_task(void*) {
  constexpr gpio_num_t kButton = GPIO_NUM_0;
  gpio_config_t io{};
  io.pin_bit_mask = 1ULL << kButton;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);
  int held_ms = 0;
  for (int elapsed_ms = 0; elapsed_ms < 30000; elapsed_ms += 100) {
    held_ms = gpio_get_level(kButton) == 0 ? held_ms + 100 : 0;
    if (held_ms >= 8000) {
      ESP_LOGW(kTag, "BOOT held for 8 s: erasing the settings and the users");
      armor::store::factory_reset();
      esp_restart();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelete(nullptr);
}

// A freshly installed firmware is on probation: once the panel is up and has stayed up for a while, it is confirmed; if the panel could not start, the
// boot loader is told to go back to the previous image now.
void confirm_firmware_task(void* argument) {
  const bool panel_ok = argument != nullptr;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (!panel_ok) {
      ESP_LOGE(kTag, "the new firmware could not start its panel: going back to the previous one");
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    vTaskDelay(pdMS_TO_TICKS(30000));
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(kTag, "the new firmware is confirmed");
  }
  vTaskDelete(nullptr);
}
}  // namespace

extern "C" void app_main() {
  armor::logbuf::start();
  armor::store::init();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  const armor::config::Settings settings = armor::store::settings();
  const bool setup = armor::store::users_empty();
  ESP_LOGI(kTag, "A.R.M.O.R. solar node %s, firmware %s, MAC tail %s%s", settings.node_id.c_str(), esp_app_get_description()->version, armor::store::mac_tail().c_str(), setup ? ", NOT SET UP YET" : "");
  if (armor::store::settings_are_stored()) {
    const armor::config::Problems problems = armor::config::validate(settings);
    for (const armor::config::Problem& problem : problems) ESP_LOGE(kTag, "settings problem: %s (%s)", problem.path.c_str(), problem.code.c_str());
  }

  // The ports run first: on the bench their statistics are the first thing to look at, with or without a network. A node in setup reads nothing yet.
  if (!setup) armor::manager::start(settings, [](const std::string& topic, const std::string& payload) { armor::mqtt_link::publish(topic, payload); });

  const armor::netplan::Plan plan = armor::netplan::plan_network(settings, setup, armor::store::setup_code(), armor::store::mac_tail(), armor::store::mac_sum());
  const bool network_ok = armor::network::start(settings, plan);
  if (network_ok && !plan.ap.enabled) {
    // Without an address after 90 s (no cable, no Wi-Fi it can join) the node would be unreachable: it opens the set-up access point again.
    armor::network::arm_rescue(armor::netplan::plan_network(settings, true, armor::store::setup_code(), armor::store::mac_tail(), armor::store::mac_sum()), 90);
  }
  if (!network_ok) ESP_LOGE(kTag, "the network could not be started: the node stays local");
  const bool panel_ok = network_ok && armor::web::start(settings);
  armor::mqtt_link::start(settings);
  armor::ble_provision::start(settings, setup);   // only when the settings say so: Bluetooth stays unused otherwise

  if (setup) xTaskCreate(setup_reminder_task, "setup-hint", 3072, nullptr, 2, nullptr);
  bool button_is_a_pin = false;
  for (const armor::config::PortConfig& port : settings.ports) if (port.enabled && (port.rx == 0 || port.tx == 0 || port.de == 0)) button_is_a_pin = true;
  if (!button_is_a_pin) xTaskCreate(boot_button_task, "boot-button", 3072, nullptr, 2, nullptr);
  xTaskCreate(confirm_firmware_task, "confirm", 3072, panel_ok ? reinterpret_cast<void*>(1) : nullptr, 2, nullptr);
}
