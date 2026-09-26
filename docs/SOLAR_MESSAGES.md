# The messages of a gateway node (contract version 0)

This is the shape the library produces (`core/solar_json.hpp`). The messages are part of ARMOR-COMMON (schemas `solar_inverter` and `solar_battery`, with
conformance vectors), ARMOR-SERVER reads them, and `tests/check_samples.py` validates what this library prints against that contract.

```
armor/solar/<node>/<device>/state        JSON, one message per device, every few seconds and on a change
```

`<node>` and `<device>` are lowercase letters, digits, `-` and `_`, at most 32 characters, not starting with `-` or `_`. `timestamp_ms` is the node's clock.

## Inverter

```json
{"kind":"inverter","node_id":"solar-1","device":"axpert-1","timestamp_ms":1000,"mode":"line",
 "grid_v":232.0,"grid_hz":50.0,"out_v":230.0,"out_hz":50.0,"out_va":161,"out_w":119,"load_percent":3,
 "battery_v":57.50,"battery_a":12.0,"battery_percent":100,"pv_v":103.8,"pv_a":14.0,"pv_w":856,"heatsink_c":69,
 "ac_charging":false,"pv_charging":true,"load_on":true,"warnings":["line_fail"]}
```

`mode` is `power_on`, `standby`, `line`, `battery`, `fault`, `power_saving`, `shutdown` or `unknown`. `battery_a` is positive while charging.
`warnings` are the active flags of `QPIWS` by name (see PROTOCOLS.md).

Optional, when the port asks for them (see PROTOCOLS.md): `pv2_v`, `pv2_a` and `pv2_w` (a second PV input; `pv_w` is then the sum of both), and for a parallel system `units` (up to ten entries with `unit`, `mode` and, when known, `serial`, `fault_code`, `grid_v`, `out_v`, `out_va`, `out_w`, `load_percent`, `battery_v`, `battery_percent`, `pv_v` and `charging_a`) with `total_out_w`, `total_out_va`, `total_load_percent` and `total_charging_a`.

## Battery stack

```json
{"kind":"battery","node_id":"solar-1","device":"us3000-1","timestamp_ms":3000,"modules":2,"state":"discharging",
 "voltage_v":49.87,"current_a":-2.59,"temperature_min_c":19.5,"temperature_max_c":25.0,"cell_min_v":3.328,"cell_max_v":3.349,
 "soc_percent":88,"alarm":false,
 "stack":[{"n":1,"present":true,"voltage_v":49.872,"current_a":-1.28,"temperature_c":22.0,"soc_percent":88,"state":"Dischg"},{"n":2,"present":false}]}
```

A module may also carry `cells_v` (the voltage of each cell, in order), `temperatures_c` (each temperature sensor), `capacity_ah` and `full_capacity_ah` (what is left
and what the module holds when full), and `cycles`; the stack carries `model`, `capacity_ah`, `full_capacity_ah` (the sums of its modules), `energy_kwh` (the remaining
energy at the mean voltage) and `cycles` (the highest of its modules). A field the battery did not report is left out.

With no module present the message has `modules: 0` and the empty `stack`, and no other reading: the node says the battery does not answer instead of inventing numbers.
`current_a` is the sum of the modules (negative discharging), `voltage_v` their mean, `soc_percent` the mean state of charge (left out when unknown: the contract has no nulls).
