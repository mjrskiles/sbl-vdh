# Region layouts — v0 (draft)

Normative layout is `include/sbl_vdh/regions.h`; `protocol.md` §3–§4 gives the semantics.
This file lists the kinds and their element types.

| Kind | Semantics | Element | `channel_count` | Notes |
|---|---|---|---|---|
| `ADC_IN` (1) | state | `uint16` count | ADC channels in scan order | the app's DMA scan buffer |
| `GPIO_IN` (2) | state | `uint8` 0/1 | input pins | polarity is the app's (`active_low` in its handle) |
| `GPIO_OUT` (3) | state | `uint8` 0/1 | output pins | |
| `DAC_OUT` (4) | state | `uint16` code | DAC channels | right-aligned in 16 bits regardless of `bits` |
| `AUDIO_IN` (5) | stream | block of `int32` × channels × block_size | audio channels | `s24_rj_i32` |
| `AUDIO_OUT` (6) | stream | same | | |
| `MIDI_IN` (7) | stream | `uint8` | 1 | raw MIDI byte stream |
| `MIDI_OUT` (8) | stream | `uint8` | 1 | feature `midi_out` |

Channel order within a region is the order of the ports listed for that region in
`welcome.regions[].ports`, which the host takes from the SHIM.
