# The messages of a gateway node (contract version 0)

This is the shape the library produces (`core/solar_json.hpp`) and `tests/check_samples.py` checks. It is **not yet part of ARMOR-COMMON**: when the
server starts to read it, the messages move into the shared contract with their schema and their conformance vectors, like the radar's.

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

## Battery stack

```json
{"kind":"battery","node_id":"solar-1","device":"us3000-1","timestamp_ms":3000,"modules":2,"state":"discharging",
 "voltage_v":49.87,"current_a":-2.59,"temperature_min_c":19.5,"temperature_max_c":25.0,"cell_min_v":3.328,"cell_max_v":3.349,
 "soc_percent":88,"alarm":false,
 "stack":[{"n":1,"present":true,"voltage_v":49.872,"current_a":-1.28,"temperature_c":22.0,"soc_percent":88,"state":"Dischg"},{"n":2,"present":false}]}
```

With no module present the message has `modules: 0` and the empty `stack`, and no other reading: the node says the battery does not answer instead of inventing numbers.
`current_a` is the sum of the modules (negative discharging), `voltage_v` their mean, `soc_percent` the mean state of charge (null when unknown).
