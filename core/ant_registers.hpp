// ARMOR-SOLAR - the settings registers of an ANT-BMS of the newer protocol that can be READ (function 0x02): address, name, scale, unit, and how many bytes the register holds.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The table follows the esphome-ant-bms project (Apache License 2.0), which lists the registers and their scales; its captured request frames are the test vectors
// (tests/ant_settings_frames.hpp). Generated once from that table, not edited. READ ONLY: nothing here builds a request that writes.
#pragma once
#include <cstddef>
#include <cstdint>

namespace armor::solar::ant {

struct Register {
  std::uint16_t address;
  const char* name;
  double scale;
  const char* unit;
  std::uint8_t bytes;   // 2, or 4 for the three capacities
};

constexpr Register kRegisters[] = {
    {0x0000, "CellOvervoltageProtection", 0.001, "V", 2},
    {0x0002, "CellOvervoltageRecovery", 0.001, "V", 2},
    {0x0004, "CellOvervoltageProtectionL2", 0.001, "V", 2},
    {0x0006, "CellOvervoltageRecoveryL2", 0.001, "V", 2},
    {0x0008, "PackOvervoltageProtection", 0.1, "V", 2},
    {0x000A, "PackOvervoltageRecovery", 0.1, "V", 2},
    {0x000C, "CellUndervoltageProtection", 0.001, "V", 2},
    {0x000E, "CellUndervoltageRecovery", 0.001, "V", 2},
    {0x0010, "CellUndervoltageProtectionL2", 0.001, "V", 2},
    {0x0012, "CellUndervoltageRecoveryL2", 0.001, "V", 2},
    {0x0014, "PackUndervoltageProtection", 0.1, "V", 2},
    {0x0016, "PackUndervoltageRecovery", 0.1, "V", 2},
    {0x0018, "CellVoltageDifferenceProtection", 0.001, "V", 2},
    {0x001A, "CellVoltageDifferenceRecovery", 0.001, "V", 2},
    {0x0020, "CellOvervoltageWarning", 0.001, "V", 2},
    {0x0022, "CellOvervoltageWarningRecovery", 0.001, "V", 2},
    {0x0024, "PackOvervoltageWarning", 0.1, "V", 2},
    {0x0026, "PackOvervoltageWarningRecovery", 0.1, "V", 2},
    {0x0028, "CellUndervoltageWarning", 0.001, "V", 2},
    {0x002A, "CellUndervoltageWarningRecovery", 0.001, "V", 2},
    {0x002C, "PackUndervoltageWarning", 0.1, "V", 2},
    {0x002E, "PackUndervoltageWarningRecovery", 0.1, "V", 2},
    {0x0030, "CellVoltageDifferenceWarning", 0.001, "V", 2},
    {0x0032, "CellVoltageDifferenceWarningRecovery", 0.001, "V", 2},
    {0x0068, "ChargeOvercurrentProtection", 0.1, "A", 2},
    {0x006A, "ChargeOvercurrentProtectionDelay", 1.0, "s", 2},
    {0x006C, "DischargeOvercurrentProtection", 0.1, "A", 2},
    {0x006E, "DischargeOvercurrentProtectionDelay", 1.0, "s", 2},
    {0x0070, "DischargeOvercurrentProtectionL2", 0.1, "A", 2},
    {0x0072, "DischargeOvercurrentProtectionDelayL2", 1.0, "ms", 2},
    {0x0074, "ShortCircuitProtection", 1.0, "A", 2},
    {0x0076, "ShortCircuitProtectionDelay", 1.0, "us", 2},
    {0x007C, "ChargeOvercurrentWarning", 0.1, "A", 2},
    {0x007E, "ChargeOvercurrentWarningRecovery", 0.1, "A", 2},
    {0x0080, "DischargeOvercurrentWarning", 0.1, "A", 2},
    {0x0082, "DischargeOvercurrentWarningRecovery", 0.1, "A", 2},
    {0x0084, "SOCLowLevel1Warning", 1.0, "%", 2},
    {0x0086, "SOCLowLevel2Warning", 1.0, "%", 2},
    {0x008C, "CellBalancingVoltage", 0.001, "V", 2},
    {0x008E, "CellBalancingStartVoltage", 0.001, "V", 2},
    {0x0090, "CellVoltageDifferenceBalancingOn", 0.001, "V", 2},
    {0x0092, "CellVoltageDifferenceBalancingOff", 0.001, "V", 2},
    {0x0094, "BalancingCurrent", 1.0, "mA", 2},
    {0x0096, "BalancingChargingCurrent", 0.1, "A", 2},
    {0x0098, "CellType", 1.0, "", 2},
    {0x009A, "CellNumber", 1.0, "S", 2},
    {0x009C, "CellInternalResistanceCalibration", 1.0, "mOhm", 2},
    {0x009E, "ShutdownVoltage", 0.001, "V", 2},
    {0x00A0, "RequestChargeCurrent", 0.1, "A", 2},
    {0x00A2, "NominalCapacity", 1.0, "Ah", 4},
    {0x00A6, "RemainingCapacity", 1.0, "Ah", 4},
    {0x00AA, "TotalCycleCapacity", 1.0, "Ah", 4},
    {0x00C4, "StateOfChargeMethod", 1.0, "", 2},
    {0x017A, "TireLength", 1.0, "mm", 2},
    {0x017C, "PulseValue", 1.0, "", 2},
    {0x017E, "SecondaryModuleNum", 1.0, "", 2},
};
constexpr std::size_t kRegisterCount = sizeof(kRegisters) / sizeof(kRegisters[0]);

}  // namespace armor::solar::ant
