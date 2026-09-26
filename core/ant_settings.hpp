// ARMOR-SOLAR - READING the settings of an ANT-BMS (the newer protocol): its model and version, and its protection, warning, balancing and capacity registers.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// READ ONLY, on purpose. The only request built here is function 0x02 (read) with an address; the write frames (0x51, 0x23, the passwords) exist in the BMS but are not
// built anywhere in this project: changing a BMS's settings can disconnect a battery under load and needs its own study, its password and isolated tests.
//
//   request   7E A1 02 <address lo> <address hi> <bytes> <crc lo> <crc hi> AA 55            (the CRC-16 is the one of the status request)
//   response  7E A1 12 <address lo> <address hi> <bytes> <value, little-endian> <crc lo> <crc hi> AA 55
// The device information (address 0x026C, 32 bytes: the model, then the software version, as text) answers with more bytes than its length byte says (six more, seen in
// a capture); it is read by position and its CRC is not checked. The other registers are asked one at a time (two or four bytes each): those answers are whole frames.
// The frames of the tests are the captures published by the esphome-ant-bms project. Nothing here has run against a BMS.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ant_bms.hpp"
#include "ant_registers.hpp"
#include "json.hpp"

namespace armor::solar::ant {

constexpr std::uint16_t kDeviceInfoAddress = 0x026C;
constexpr std::uint8_t kDeviceInfoBytes = 0x20;

// The request that READS `bytes` bytes at `address`.
inline std::vector<std::uint8_t> settings_request(std::uint16_t address, std::uint8_t bytes) {
  std::vector<std::uint8_t> frame = {0x7E, 0xA1, 0x02, static_cast<std::uint8_t>(address & 0xFF), static_cast<std::uint8_t>(address >> 8), bytes};
  const std::uint16_t crc = crc16(frame.data() + 1, 5);
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<std::uint8_t>(crc >> 8));
  frame.push_back(0xAA);
  frame.push_back(0x55);
  return frame;
}

// A register's answer: whole frame, function 0x12, CRC checked. `raw` is the unsigned little-endian value of its two or four bytes.
inline bool decode_setting(const std::uint8_t* d, std::size_t length, std::uint16_t& address, std::uint32_t& raw) {
  if (length < 12 || d[0] != 0x7E || d[1] != 0xA1 || d[2] != 0x12) return false;
  const std::size_t bytes = d[5];
  if (bytes != 2 && bytes != 4) return false;
  if (length != 6 + bytes + 4 || d[length - 2] != 0xAA || d[length - 1] != 0x55) return false;
  if (crc16(d + 1, length - 5) != detail::u16_le(d + length - 4)) return false;
  address = static_cast<std::uint16_t>(detail::u16_le(d + 3));
  raw = bytes == 2 ? detail::u16_le(d + 6) : detail::u32_le(d + 6);
  return true;
}

// The device information: 16 bytes of model, 16 of software version, each as text ended by a zero. The CRC is not checked (see the header); the text must be printable.
inline bool decode_device_info(const std::uint8_t* d, std::size_t length, std::string& model, std::string& version) {
  if (length < 38 || d[0] != 0x7E || d[1] != 0xA1 || d[2] != 0x12 || detail::u16_le(d + 3) != kDeviceInfoAddress || d[5] < kDeviceInfoBytes) return false;
  const auto text = [&](std::size_t at, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < 16 && d[at + i] != 0; ++i) {
      if (d[at + i] < 0x20 || d[at + i] > 0x7E) return false;
      out += static_cast<char>(d[at + i]);
    }
    return true;
  };
  return text(6, model) && text(22, version);
}

struct SettingValue {
  std::uint16_t address = 0;
  const char* name = "";
  const char* unit = "";
  double value = 0;
};

// Asks the BMS for its device information and then for each register in turn, one request at a time, and collects what it answers. It touches no hardware: the port task feeds
// it the bytes that arrive and the time, and sends what next_tx() returns. A register that does not answer in time is left out (and counted); three silences in a row
// before any answer end it: nothing is listening.
class SettingsReader {
 public:
  static constexpr std::uint64_t kTimeoutMs = 700;

  std::vector<std::uint8_t> next_tx(std::uint64_t now_ms) {
    if (finished_) return {};
    if (waiting_) {
      if (now_ms < deadline_ms_) return {};
      ++missing_;
      ++silent_run_;
      if (answered_ == 0 && silent_run_ >= 3) { fail("silent"); return {}; }
      advance();
      if (finished_) return {};
    }
    waiting_ = true;
    deadline_ms_ = now_ms + kTimeoutMs;
    framer_.clear();
    return step_ == 0 ? settings_request(kDeviceInfoAddress, kDeviceInfoBytes) : settings_request(kRegisters[step_ - 1].address, kRegisters[step_ - 1].bytes);
  }

  void on_rx(const std::uint8_t* data, std::size_t length) {
    if (finished_ || !waiting_) return;
    for (std::size_t i = 0; i < length && waiting_; ++i) {
      std::vector<std::uint8_t> frame;
      Protocol protocol = Protocol::kNew;
      if (!framer_.feed(data[i], frame, protocol) || protocol != Protocol::kNew) continue;
      if (step_ == 0) {
        std::string model, version;
        if (decode_device_info(frame.data(), frame.size(), model, version)) { model_ = model; version_ = version; have_info_ = true; got(); }
      } else {
        std::uint16_t address = 0; std::uint32_t raw = 0;
        if (decode_setting(frame.data(), frame.size(), address, raw) && address == kRegisters[step_ - 1].address) {
          const Register& reg = kRegisters[step_ - 1];
          values_.push_back({reg.address, reg.name, reg.unit, static_cast<double>(raw) * reg.scale});
          got();
        }
      }
    }
  }

  bool done() const { return finished_; }
  bool failed() const { return !error_.empty(); }
  const std::string& error() const { return error_; }
  std::size_t step() const { return step_ > total() ? total() : step_; }
  static constexpr std::size_t total() { return kRegisterCount + 1; }
  std::size_t answered() const { return answered_; }
  std::size_t missing() const { return missing_; }
  const std::vector<SettingValue>& values() const { return values_; }
  const std::string& model() const { return model_; }
  const std::string& version() const { return version_; }

  // {"model":"16ZM","version":"...","asked":57,"answered":52,"missing":5,"settings":[{"address":0,"name":"CellOvervoltageProtection","value":4.15,"unit":"V"},...]}
  std::string result_json() const {
    json::Writer w;
    w.begin_object();
    if (have_info_) w.field("model", model_).field("version", version_);
    w.field("asked", static_cast<int>(total())).field("answered", static_cast<int>(answered_)).field("missing", static_cast<int>(missing_));
    w.key("settings").begin_array();
    for (const SettingValue& v : values_) w.begin_object().field("address", static_cast<int>(v.address)).field("name", v.name).key("value").number(v.value, 3).field("unit", v.unit).end_object();
    w.end_array().end_object();
    return w.str();
  }

 private:
  void got() { ++answered_; silent_run_ = 0; advance(); }
  void advance() {
    waiting_ = false;
    ++step_;
    if (step_ >= total()) finished_ = true;
  }
  void fail(const char* code) { error_ = code; finished_ = true; waiting_ = false; }

  Framer framer_;
  std::size_t step_ = 0;        // 0: the device information, then the registers
  bool waiting_ = false, finished_ = false, have_info_ = false;
  std::uint64_t deadline_ms_ = 0;
  std::size_t answered_ = 0, missing_ = 0, silent_run_ = 0;
  std::string model_, version_, error_;
  std::vector<SettingValue> values_;
};

}  // namespace armor::solar::ant
