// ARMOR-SOLAR - the node's serial ports.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Hardware ports use the chip's UART drivers with the pins of the settings (any GPIO, through the chip's matrix); a port with a driver-enable pin runs in the
// driver's RS485 half-duplex mode, which raises that pin while sending. Emulated ports time the line in software (core/soft_uart.hpp for the arithmetic):
// an interrupt on every edge of the RX pin notes the time, and a hardware timer changes the TX pin at every bit. The chip has only four timers, so all the emulated
// ports share one, and only one of them sends at a time (a request is a few bytes at a slow speed: the others keep listening meanwhile). Nothing here has run on a board.
#include "uart_ports.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>
extern "C" {
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/board_s3.hpp"
#include "core/soft_uart.hpp"

namespace armor::ports {
namespace {
constexpr char kTag[] = "armor-uart";
bool g_taken[board::kPortCount] = {};

// The UART behind each hardware port: port 1 is UART1, port 2 is UART2 and port 3 is UART0 (the console is on the native USB, so it is free).
constexpr uart_port_t kHardware[board::kHardwarePorts] = {UART_NUM_1, UART_NUM_2, UART_NUM_0};

// ---- hardware ---------------------------------------------------------------------------------------------------------------------------

class HardwarePort final : public Port {
 public:
  explicit HardwarePort(uart_port_t uart) : uart_(uart) {}
  ~HardwarePort() override { uart_driver_delete(uart_); }

  bool start(const Options& o, std::string& error) {
    uart_config_t config{};
    config.baud_rate = o.baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(uart_, 2048, 0, 0, nullptr, 0) != ESP_OK) { error = "driver"; return false; }
    if (uart_param_config(uart_, &config) != ESP_OK) { error = "baud"; uart_driver_delete(uart_); return false; }
    if (uart_set_pin(uart_, o.tx >= 0 ? o.tx : UART_PIN_NO_CHANGE, o.rx, o.de >= 0 ? o.de : UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) { error = "pins"; uart_driver_delete(uart_); return false; }
    if (o.de >= 0 && uart_set_mode(uart_, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK) { error = "driver"; uart_driver_delete(uart_); return false; }
    return true;
  }

  std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) override {
    const int n = uart_read_bytes(uart_, out, capacity, pdMS_TO_TICKS(wait_ms));
    return n > 0 ? static_cast<std::size_t>(n) : 0;
  }

  bool write(const std::uint8_t* data, std::size_t length) override {
    uart_flush_input(uart_);   // whatever came before the request is not its answer
    if (uart_write_bytes(uart_, data, length) < 0) return false;
    return uart_wait_tx_done(uart_, pdMS_TO_TICKS(2000)) == ESP_OK;
  }

  bool soft() const override { return false; }
  std::uint32_t overruns() const override { return 0; }
  std::uint32_t framing_errors() const override { return 0; }

 private:
  uart_port_t uart_;
};

// ---- emulated ---------------------------------------------------------------------------------------------------------------------------

struct EdgeRecord {
  std::uint64_t time_us;
  std::uint8_t level;
};

// What the interrupts share with the port's task: a ring of edges filled by the RX pin's interrupt, and the bits of the message being sent.
struct SoftShared {
  static constexpr std::size_t kRing = 1024;
  EdgeRecord ring[kRing];
  std::atomic<std::uint32_t> head{0}, tail{0};
  std::atomic<std::uint32_t> overruns{0};
  volatile bool sending = false;
  int rx = -1, tx = -1, de = -1;
  // sending
  const bool* bits = nullptr;
  std::size_t bit_count = 0, bit_index = 0;
  TaskHandle_t waiter = nullptr;
};

void IRAM_ATTR on_rx_edge(void* argument) {
  auto* shared = static_cast<SoftShared*>(argument);
  if (shared->sending) return;   // what an RS485 transceiver hears of our own sending is not an answer
  const std::uint32_t head = shared->head.load(std::memory_order_relaxed);
  const std::uint32_t next = (head + 1) % SoftShared::kRing;
  if (next == shared->tail.load(std::memory_order_acquire)) { shared->overruns.fetch_add(1, std::memory_order_relaxed); return; }
  shared->ring[head] = {static_cast<std::uint64_t>(esp_timer_get_time()), static_cast<std::uint8_t>(gpio_get_level(static_cast<gpio_num_t>(shared->rx)))};
  shared->head.store(next, std::memory_order_release);
}

// The one timer that shifts out the bits of whichever emulated port is sending, and the lock that lets one port send at a time.
gptimer_handle_t g_tx_timer = nullptr;
std::mutex g_tx_lock;
SoftShared* volatile g_tx_active = nullptr;

bool IRAM_ATTR on_tx_bit(gptimer_handle_t timer, const gptimer_alarm_event_data_t*, void*) {
  SoftShared* shared = g_tx_active;
  if (shared == nullptr) { gptimer_stop(timer); return false; }
  if (shared->bit_index < shared->bit_count) {
    gpio_set_level(static_cast<gpio_num_t>(shared->tx), shared->bits[shared->bit_index++] ? 1 : 0);
    return false;
  }
  gptimer_stop(timer);
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(shared->waiter, &woken);
  return woken == pdTRUE;
}

// Creates the shared timer the first time an emulated port needs to send.
bool ensure_tx_timer() {
  std::lock_guard<std::mutex> guard(g_tx_lock);
  if (g_tx_timer != nullptr) return true;
  gptimer_config_t timer{};
  timer.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer.direction = GPTIMER_COUNT_UP;
  timer.resolution_hz = 1000000;
  gptimer_handle_t handle = nullptr;
  if (gptimer_new_timer(&timer, &handle) != ESP_OK) return false;
  gptimer_event_callbacks_t callbacks{};
  callbacks.on_alarm = on_tx_bit;
  if (gptimer_register_event_callbacks(handle, &callbacks, nullptr) != ESP_OK || gptimer_enable(handle) != ESP_OK) { gptimer_del_timer(handle); return false; }
  g_tx_timer = handle;
  return true;
}

class SoftPort final : public Port {
 public:
  explicit SoftPort(unsigned baud) : receiver_(baud), baud_(baud) {}
  ~SoftPort() override {
    if (shared_.rx >= 0) { gpio_isr_handler_remove(static_cast<gpio_num_t>(shared_.rx)); gpio_reset_pin(static_cast<gpio_num_t>(shared_.rx)); }
    if (shared_.tx >= 0) gpio_reset_pin(static_cast<gpio_num_t>(shared_.tx));
    if (shared_.de >= 0) gpio_reset_pin(static_cast<gpio_num_t>(shared_.de));
  }

  bool start(const Options& o, std::string& error) {
    shared_.rx = o.rx; shared_.tx = o.tx; shared_.de = o.de;
    gpio_config_t input{};
    input.pin_bit_mask = 1ULL << o.rx;
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;   // an idle line is high
    input.intr_type = GPIO_INTR_ANYEDGE;
    if (gpio_config(&input) != ESP_OK) { error = "pins"; return false; }
    const esp_err_t service = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (service != ESP_OK && service != ESP_ERR_INVALID_STATE) { error = "driver"; return false; }
    if (gpio_isr_handler_add(static_cast<gpio_num_t>(o.rx), on_rx_edge, &shared_) != ESP_OK) { error = "driver"; return false; }
    if (o.tx >= 0) {
      gpio_config_t output{};
      output.pin_bit_mask = 1ULL << o.tx;
      output.mode = GPIO_MODE_OUTPUT;
      if (gpio_config(&output) != ESP_OK) { error = "pins"; return false; }
      gpio_set_level(static_cast<gpio_num_t>(o.tx), 1);   // idle high
      if (!ensure_tx_timer()) { error = "driver"; return false; }
    }
    if (o.de >= 0) {
      gpio_config_t direction{};
      direction.pin_bit_mask = 1ULL << o.de;
      direction.mode = GPIO_MODE_OUTPUT;
      if (gpio_config(&direction) != ESP_OK) { error = "pins"; return false; }
      gpio_set_level(static_cast<gpio_num_t>(o.de), 0);
    }
    return true;
  }

  std::size_t read(std::uint8_t* out, std::size_t capacity, unsigned wait_ms) override {
    const std::uint64_t deadline = static_cast<std::uint64_t>(esp_timer_get_time()) + static_cast<std::uint64_t>(wait_ms) * 1000ULL;
    for (;;) {
      drain();
      receiver_.poll(static_cast<std::uint64_t>(esp_timer_get_time()), bytes_);
      if (!bytes_.empty() || static_cast<std::uint64_t>(esp_timer_get_time()) >= deadline) break;
      vTaskDelay(1);
    }
    const std::size_t n = std::min(capacity, bytes_.size());
    std::memcpy(out, bytes_.data(), n);
    bytes_.erase(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(n));
    return n;
  }

  bool write(const std::uint8_t* data, std::size_t length) override {
    if (shared_.tx < 0 || g_tx_timer == nullptr || length == 0 || length > 64) return false;
    std::lock_guard<std::mutex> one_at_a_time(g_tx_lock);
    const std::vector<bool> packed = softuart::message_bits(data, length);
    // The bits are held in a plain array the interrupt can read; vector<bool> is packed.
    const std::size_t total_bits = packed.size();
    bool* copy = new (std::nothrow) bool[total_bits];
    if (copy == nullptr) return false;
    for (std::size_t i = 0; i < total_bits; ++i) copy[i] = packed[i];
    const std::uint64_t bit_us = 1000000ULL / baud_;
    shared_.bits = copy;
    shared_.bit_count = total_bits;
    shared_.bit_index = 1;               // the first bit (the start bit) is put on the line now
    shared_.waiter = xTaskGetCurrentTaskHandle();
    shared_.sending = true;
    ulTaskNotifyTake(pdTRUE, 0);         // forget an old notification
    if (shared_.de >= 0) gpio_set_level(static_cast<gpio_num_t>(shared_.de), 1);
    gptimer_alarm_config_t alarm{};
    alarm.alarm_count = bit_us;
    alarm.reload_count = 0;
    alarm.flags.auto_reload_on_alarm = true;
    gptimer_set_raw_count(g_tx_timer, 0);
    gptimer_set_alarm_action(g_tx_timer, &alarm);
    g_tx_active = &shared_;
    gpio_set_level(static_cast<gpio_num_t>(shared_.tx), copy[0] ? 1 : 0);
    gptimer_start(g_tx_timer);
    const bool done = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(total_bits * bit_us / 1000 + 250)) > 0;
    if (!done) gptimer_stop(g_tx_timer);
    g_tx_active = nullptr;
    gpio_set_level(static_cast<gpio_num_t>(shared_.tx), 1);
    if (shared_.de >= 0) { esp_rom_delay_us(static_cast<std::uint32_t>(bit_us)); gpio_set_level(static_cast<gpio_num_t>(shared_.de), 0); }
    shared_.sending = false;
    shared_.bits = nullptr;
    delete[] copy;
    // what came before, and what the transceiver heard of us, is not the answer
    shared_.tail.store(shared_.head.load(std::memory_order_acquire), std::memory_order_release);
    receiver_ = softuart::Receiver(baud_);
    bytes_.clear();
    return done;
  }

  bool soft() const override { return true; }
  std::uint32_t overruns() const override { return shared_.overruns.load(std::memory_order_relaxed); }
  std::uint32_t framing_errors() const override { return receiver_.framing_errors(); }

 private:
  void drain() {
    std::uint32_t tail = shared_.tail.load(std::memory_order_relaxed);
    const std::uint32_t head = shared_.head.load(std::memory_order_acquire);
    while (tail != head) {
      receiver_.edge(shared_.ring[tail].time_us, shared_.ring[tail].level != 0);
      tail = (tail + 1) % SoftShared::kRing;
    }
    shared_.tail.store(tail, std::memory_order_release);
  }

  SoftShared shared_;
  softuart::Receiver receiver_;
  unsigned baud_;
  std::vector<std::uint8_t> bytes_;
};
}  // namespace

std::unique_ptr<Port> open(const Options& o, std::string& error) {
  error.clear();
  if (o.index >= static_cast<std::size_t>(board::kPortCount)) { error = "pins"; return nullptr; }
  if (g_taken[o.index]) { error = "busy"; return nullptr; }
  const bool soft = o.index >= static_cast<std::size_t>(board::kHardwarePorts);
  if (o.rx < 0 || !board::assignable(o.rx) || (o.tx >= 0 && !board::assignable(o.tx)) || (o.de >= 0 && !board::assignable(o.de))) { error = "pins"; return nullptr; }
  if (o.baud < 1200 || (soft && o.baud > static_cast<int>(softuart::kMaxBaud))) { error = "baud"; return nullptr; }
  std::unique_ptr<Port> port;
  if (soft) {
    auto p = std::make_unique<SoftPort>(static_cast<unsigned>(o.baud));
    if (!p->start(o, error)) return nullptr;
    port = std::move(p);
  } else {
    auto p = std::make_unique<HardwarePort>(kHardware[o.index]);
    if (!p->start(o, error)) return nullptr;
    port = std::move(p);
  }
  g_taken[o.index] = true;
  ESP_LOGI(kTag, "port %u open: %s, %d baud, rx %d, tx %d%s", static_cast<unsigned>(o.index + 1), soft ? "emulated" : "UART", o.baud, o.rx, o.tx, o.de >= 0 ? ", RS485" : "");
  return port;
}

}  // namespace armor::ports
