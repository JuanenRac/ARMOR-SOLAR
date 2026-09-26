#!/usr/bin/env python3
"""Check the messages ARMOR-SOLAR's emit_samples prints: valid JSON, the naming rules, the fields of contract version 0.

Usage:  emit_samples | python tests/check_samples.py
"""
import json
import re
import sys

NAME = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")
COMMON = {"kind", "node_id", "device", "timestamp_ms"}
INVERTER = COMMON | {"mode", "grid_v", "grid_hz", "out_v", "out_hz", "out_va", "out_w", "load_percent", "battery_v", "battery_a", "battery_percent", "pv_v", "pv_a", "pv_w",
                     "heatsink_c", "ac_charging", "pv_charging", "load_on", "warnings"}
BATTERY_ALWAYS = COMMON | {"modules", "stack"}
BATTERY_WITH_MODULES = BATTERY_ALWAYS | {"state", "voltage_v", "current_a", "temperature_min_c", "temperature_max_c", "cell_min_v", "cell_max_v", "soc_percent", "alarm"}


def main() -> int:
    checked = 0
    for line in sys.stdin.read().splitlines():
        kind, _, body = line.partition(" ")
        message = json.loads(body)
        if message["kind"] != kind or not NAME.match(message["node_id"]) or not NAME.match(message["device"]) or not isinstance(message["timestamp_ms"], int):
            print("bad common fields:", line, file=sys.stderr)
            return 1
        keys = set(message)
        if kind == "inverter":
            ok = keys == INVERTER and isinstance(message["warnings"], list) and all(isinstance(w, str) for w in message["warnings"])
        else:
            ok = keys == (BATTERY_WITH_MODULES if message["modules"] > 0 else BATTERY_ALWAYS) and isinstance(message["stack"], list)
        if not ok:
            print("unexpected fields:", sorted(keys), file=sys.stderr)
            return 1
        checked += 1
    if checked < 4:
        print(f"expected 4 messages, got {checked}", file=sys.stderr)
        return 1
    print(f"SOLAR_MESSAGES=PASS {checked} messages well formed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
