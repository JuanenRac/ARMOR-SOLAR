// ARMOR-SOLAR - the LEDs of the ports: the task that shifts the pattern of core/port_leds.hpp out to the 74HC595.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// Nothing here has run on a board.
#include "port_leds_hw.hpp"

#include <mutex>
extern "C" {
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/port_leds.hpp"

namespace armor::leds {
namespace {
constexpr char kTag[] = "armor-leds";
std::mutex g_lock;
solar::PortLeds g_leds;
bool g_running = false;
config::LedPins g_pins;

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

struct Pins {
  void set_data(bool v) { gpio_set_level(static_cast<gpio_num_t>(g_pins.data), v); esp_rom_delay_us(1); }
  void set_clock(bool v) { gpio_set_level(static_cast<gpio_num_t>(g_pins.clock), v); esp_rom_delay_us(1); }
  void set_latch(bool v) { gpio_set_level(static_cast<gpio_num_t>(g_pins.latch), v); esp_rom_delay_us(1); }
};

bool output(int gpio) {
  gpio_config_t io{};
  io.pin_bit_mask = 1ULL << gpio;
  io.mode = GPIO_MODE_OUTPUT;
  return gpio_config(&io) == ESP_OK && gpio_set_level(static_cast<gpio_num_t>(gpio), 0) == ESP_OK;
}

void led_task(void*) {
  Pins pins;
  int shown = -1;   // nothing shown yet
  for (;;) {
    std::uint16_t pattern;
    { std::lock_guard<std::mutex> guard(g_lock); pattern = g_leds.pattern(now_ms()); }
    if (pattern != shown) { solar::shift_out16(pins, pattern); shown = pattern; }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}  // namespace

bool start(const config::LedPins& pins, const config::Settings& settings) {
  if (!pins.any() || pins.data < 0 || pins.clock < 0 || pins.latch < 0 || g_running) return false;
  if (!output(pins.data) || !output(pins.clock) || !output(pins.latch)) { ESP_LOGE(kTag, "the LED pins could not be set up"); return false; }
  g_pins = pins;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    for (std::size_t i = 0; i < solar::PortLeds::kCount; ++i) g_leds.set_enabled(i, i < config::kPortCount && settings.ports[i].enabled);
  }
  g_running = true;
  xTaskCreate(led_task, "port-leds", 3072, nullptr, 3, nullptr);
  ESP_LOGI(kTag, "LEDs on data %d, clock %d, latch %d", pins.data, pins.clock, pins.latch);
  return true;
}

void good(std::size_t port) { if (!g_running) return; std::lock_guard<std::mutex> guard(g_lock); g_leds.good(port, now_ms()); }
void bad(std::size_t port) { if (!g_running) return; std::lock_guard<std::mutex> guard(g_lock); g_leds.bad(port, now_ms()); }

}  // namespace armor::leds
