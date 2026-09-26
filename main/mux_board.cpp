// ARMOR-SOLAR - the base board with multiplexers.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One hardware UART per group, on the group's own TX and RX pins, and the select pins of the 74HC4052 in front of it: choosing a channel is setting S0 and S1 (S1 is tied to
// ground on a board whose group has two ports or fewer). A 74HC4052 switches in nanoseconds; the short wait after a change is for the lines behind it to settle.
// Nothing here has run on a board.
#include "mux_board.hpp"

extern "C" {
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
}
#include "uart_ports.hpp"

namespace armor::muxboard {
namespace {
constexpr char kTag[] = "armor-mux";

class Board final : public solar::MuxLine {
 public:
  Board(std::unique_ptr<ports::Port> port, int s0, int s1) : port_(std::move(port)), s0_(s0), s1_(s1) {}

  void select(std::size_t channel) override {
    gpio_set_level(static_cast<gpio_num_t>(s0_), (channel & 1u) != 0);
    if (s1_ >= 0) gpio_set_level(static_cast<gpio_num_t>(s1_), (channel & 2u) != 0);
    esp_rom_delay_us(50);
  }
  bool configure(int baud, bool invert) override { return port_->set_line(baud, invert); }
  void discard() override { port_->discard(); }
  std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) override { return port_->read(out, capacity, wait_ms); }
  bool write(const std::uint8_t* data, std::size_t length) override { return port_->write(data, length); }

 private:
  std::unique_ptr<ports::Port> port_;
  int s0_, s1_;
};

bool output_low(int gpio) {
  gpio_config_t io{};
  io.pin_bit_mask = 1ULL << gpio;
  io.mode = GPIO_MODE_OUTPUT;
  if (gpio_config(&io) != ESP_OK) return false;
  return gpio_set_level(static_cast<gpio_num_t>(gpio), 0) == ESP_OK;
}
}  // namespace

std::unique_ptr<solar::MuxLine> open_group(std::size_t group, const config::MuxGroup& pins, int first_baud, std::string& error) {
  if (pins.s0 < 0 || !output_low(pins.s0) || (pins.s1 >= 0 && !output_low(pins.s1))) { error = "pins"; return nullptr; }
  ports::Options options;
  options.index = group;            // the port index of the UART layer: 0, 1 and 2 are the three hardware UARTs
  options.baud = first_baud;
  options.rx = pins.rx;
  options.tx = pins.tx;
  std::unique_ptr<ports::Port> port = ports::open(options, error);
  if (!port) return nullptr;
  ESP_LOGI(kTag, "group %c: UART on tx %d, rx %d, select %d and %d", static_cast<char>('A' + group), pins.tx, pins.rx, pins.s0, pins.s1);
  return std::make_unique<Board>(std::move(port), pins.s0, pins.s1);
}

}  // namespace armor::muxboard
