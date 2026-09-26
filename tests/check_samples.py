#!/usr/bin/env python3
"""Check the messages ARMOR-SOLAR's emit_samples prints against ARMOR-COMMON's published contract.

Usage:  emit_samples | python tests/check_samples.py

Each line is a topic and a JSON payload. The firmware's serialiser is hand-written C++; this is the proof that what it prints is what the shared contract
(schemas, topic rules, ranges) accepts.
"""
import json
import sys
from pathlib import Path

COMMON = Path(__file__).resolve().parents[2] / "ARMOR-COMMON" / "src"
sys.path.insert(0, str(COMMON))
from armor_common import ContractError, validate_solar_message  # noqa: E402


def main() -> int:
    checked = 0
    kinds = set()
    for line in sys.stdin.read().splitlines():
        topic, _, body = line.partition(" ")
        payload = json.loads(body)
        try:
            validate_solar_message(topic, payload)
        except ContractError as error:
            print(f"{topic} violates the contract: {error}\n  {body}", file=sys.stderr)
            return 1
        kinds.add(payload["kind"])
        checked += 1
    if checked < 4 or kinds != {"inverter", "battery"}:
        print(f"expected at least 4 messages of both kinds, got {checked} ({sorted(kinds)})", file=sys.stderr)
        return 1
    print(f"SOLAR_MESSAGES=PASS {checked} messages accepted by ARMOR-COMMON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
