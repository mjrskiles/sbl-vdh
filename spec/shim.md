# SHIM — host requirements, v0 (draft)

The SHIM (SBL Hardware Interface Manifest) is an app's I/O surface: every port it can
drive or read, by the name the app's own code uses. sloth generates it from the same
resolution that produces the app's C++ config headers, and the build copies it next to
the binary as `<target>.shim.json`. Its schema is owned by `sbl-schema` in sound-byte-libs
(`shim.schema.json`, exported from `models/shim.py`, ADR-006); this document states what
a host needs from it. Anything the schema has that this document does not name is
documentation for people, not contract.

## Document

```jsonc
{
  "shim_version": 1,
  "device": "sbl:devices/virtual-patch-init",
  "mcu": "linux-arm-host",
  "audio": { "in": 2, "out": 2, "sample_rate": 48000, "block_size": 48, "format": "s24_rj_i32" },
  "ports": [
    { "id": "knob_1",   "kind": "analog_in",  "component": "pot",
      "electrical": { "range_v": [-5.0, 5.0], "bits": 16, "inverted": true }, "idle_v": 2.485 },
    { "id": "cv_5",     "kind": "analog_in",  "component": "cv_input",
      "electrical": { "range_v": [-5.0, 5.0], "bits": 16, "inverted": true }, "idle_v": 0.0 },
    { "id": "cv_out_1", "kind": "analog_out",
      "electrical": { "range_v": [0.0, 5.0], "bits": 12, "inverted": false } },
    { "id": "gate_in_1", "kind": "digital_in",  "active_low": false },
    { "id": "gate_out_1", "kind": "digital_out", "active_low": false },
    { "id": "midi_in",   "kind": "midi_in",  "transport": "host" },
    { "id": "midi_out",  "kind": "midi_out", "transport": "host" },
    { "id": "midi_thru", "kind": "midi_out", "transport": "host" }
  ]
}
```

`shim_version` is independent of `protocol_version`; a host declares which SHIM versions
it reads. Ports are listed in `id` order. The host computes `sha256` over the SHIM file
bytes and compares it with the client's `hello.shim_sha256`.

## What a host MUST read

For each port:

- `id` — equal to the generated handle name (`cv_5`, not `cv5`). This is the routing
  vocabulary: a route names `app.port` and the host resolves `port` here.
- `kind` — one of `analog_in`, `analog_out`, `digital_in`, `digital_out`, `midi_in`,
  `midi_out`. Kinds map to region kinds (`regions.md`): analog in → `ADC_IN`, analog
  out → `DAC_OUT`, digital in/out → `GPIO_IN`/`GPIO_OUT`, MIDI in/out → `MIDI_IN`/`MIDI_OUT`.
  Channel order within a region is the order of the ports the host lists for it in
  `welcome.regions[].ports`.
- Analog ports: `electrical.range_v`, `electrical.bits`, `electrical.inverted`, and for
  inputs `idle_v`. Conversion along a route goes code → volts → code through each end's
  electrics (`protocol.md` §6); an unpatched input holds `idle_v`, encoded.
- Digital ports: `active_low`. The region carries the pin level; polarity is the app's.

For audio, when `audio` is not null: `in`, `out` (channel counts), `sample_rate`,
`block_size`, and `format` (`s24_rj_i32`). An app whose `audio` is null declares no
audio ports and the host MUST NOT wait on its `done`.

## MIDI ports

MIDI ports are declared by the app (`app.sbl.json`, `midi: {in, out, thru}`), not by the
hardware manifests, and are named by convention: `midi_in`; `midi_out` for one output or
`midi_out_1`..`midi_out_n` for several (n ≤ 4); `midi_thru`. The client driver maps
`MidiPort::write(..., port)` indices 0..3 to the outputs in that order and index 4 to
`midi_thru`, so a host assigns one `MIDI_OUT` region per output port by id. `transport`
(`usb` or `host`) is documentation.

## Known gaps in v0

- `cv_out_1`/`cv_out_2` are `analog_out` with 0–5 V, 12-bit electrics, but the app
  drives them as GPIO until the DAC driver exists (FDP-076 open question 4). A host
  receiving a `GPIO_OUT` write for an `analog_out` port SHOULD treat 1 as `range_v[1]`
  and 0 as `range_v[0]`.
- Pins a panel claims as GPIO for a bus (the Patch.init()'s `sdmmc_*` lines) appear as
  `digital_out` ports. They are not jacks; a manifest claim kind for bus lines would
  remove them.
- `component` (`pot`, `cv_input`, `button`) is documentation; the region layouts are
  the same either way.
