# VDH protocol — v0 (draft)

Status: draft, 2026-09-09. Derived from RPT-031 (sound-byte-labs) findings F3–F8, all
accepted. Nothing here is implemented yet.

## 1. Model

A **host** owns the physical audio and MIDI devices (or runs with none, offline), a set of
shared-memory **regions** per attached app, a **routing table**, and the **clock**. A
**client** is one of the `linux-arm-host` drivers inside an app process. An app attaches by
having `SBL_VDH` in its environment when its drivers initialize; the host may have spawned
it or not (a debugger may have).

Regions hold **device codes** — what the driver would see from real hardware: `uint16`
ADC counts, `0/1` pin states, 24-bit audio in `int32`, MIDI bytes. The host converts between
electrically different ports along a route using each end's SHIM electrics
(source code → volts → destination code). The virtual cable is volts; the conversion is the
host's and happens once. Clients never convert.

This mirrors virtio's basic facilities (virtio v1.2 §2, p. 21): a status sequence, feature
bits, a configuration space with a generation counter, notifications, and typed queues.

## 2. Discovery and handshake

- `SBL_VDH=<path>` — a Unix domain socket the host listens on. Absent → the client runs
  standalone and this document does not apply.
- The **control channel** is that socket: newline-delimited JSON messages, with file
  descriptors passed via `SCM_RIGHTS` where noted. JSON is chosen for debuggability; the
  data path never touches it.

Sequence (client states in brackets, after virtio's status field):

```
client → host   hello    {proto: 0, features: [..], pid, app, shim_sha256}          [HELLO]
host   → client welcome  {proto: 0, features: [..acked..], regions: [...], doorbell: {tick, done}}
                          + fds: one memfd per region, two eventfds (tick, done)       [ACKED]
client → host   ready    {}                                                          [READY]
...
either → other  bye      {reason}
```

- `shim_sha256` is the hash of the SHIM the app was built with. The host already has the
  SHIM (from launch or from `attach --shim`); a mismatch is a hard `bye`.
- `features` are strings. v0 defines: `offline_clock`, `port_config`, `midi_out`,
  `cross_kind_routes`. A client MUST NOT use a feature the host did not ack.
- Each `regions[]` entry: `{kind, ports: [id...], fd_index, size}`. Kinds are listed in §4.
  A client MUST accept regions in any order and MAY ignore kinds it does not drive.
- The host MUST NOT tick a client before `ready` (virtio: no buffers before `DRIVER_OK`).

Region memory is `memfd`-backed. Lifetime is fd lifetime: when either side dies, the
kernel closes its fds and the other side observes it (`recv` returns 0 on the control
socket; `read` on the doorbell fails). No named `/dev/shm` objects, nothing to clean up.

## 3. Region header

Every region starts with a 64-byte header (`include/sbl_vdh/regions.h`):

```c
struct sbl_vdh_region_hdr {
    char     magic[4];        // "SBLV"
    uint32_t version;         // header layout version, 0 for this draft
    uint32_t kind;            // SBL_VDH_KIND_*
    uint32_t flags;
    uint32_t channel_count;   // channels or pins
    uint32_t block_size;      // frames per block (audio) / bytes per slot (midi) / 0
    uint32_t sample_rate;     // audio only, else 0
    uint32_t stride;          // bytes per element
    uint32_t data_offset;     // from region start, >= 64, 64-byte aligned
    _Atomic uint32_t seq;     // state regions: seqlock counter
    _Atomic uint32_t head;    // stream regions: producer cursor (elements)
    _Atomic uint32_t tail;    // stream regions: consumer cursor (elements)
    uint32_t capacity;        // stream regions: elements in ring (power of two)
    _Atomic uint32_t config_generation;  // bumps on port_config (§7)
    uint32_t reserved[2];
};
```

Sizes are in the header so one layout serves every kind and a Python reader needs no
per-kind struct. Readers MUST check `magic`, `version`, and that `size >= data_offset +
capacity*stride`.

## 4. Region kinds and semantics

Two semantics, two mechanisms (RPT-031 §F4):

**State** — latest value wins, readers may skip. `ADC_IN`, `GPIO_IN`, `GPIO_OUT`, `DAC_OUT`.
Elements are `uint16` (analog) or `uint8` (digital), one per channel, at `data_offset`.
Per-element stores are atomic on every target we run on, and the SBL app reads the ADC
scan buffer as plain memory (`hal/adc/scan.hpp`), so the data area is a **flat array of
atomically-stored elements** — exactly the DMA scan buffer. `seq` is provided for readers
that need a consistent multi-channel snapshot: writer increments to odd, writes, increments
to even; reader retries while odd or changed. Clients MAY ignore `seq`; hosts MUST maintain
it.

**Stream** — every element consumed exactly once. `AUDIO_IN`, `AUDIO_OUT`, `MIDI_IN`,
`MIDI_OUT`. A ring of `capacity` elements with `head`/`tail`. For audio an element is one
block of `channel_count * block_size` interleaved `int32` samples; `capacity` is 2 under
the clock (§5). For MIDI an element is one byte; `capacity` is at least 1024. Producer
writes then publishes `head` (release); consumer reads then publishes `tail` (release);
both acquire the other's cursor. A full ring is a producer error the host logs (audio:
xrun; MIDI: overflow) — data is never silently overwritten.

Audio sample format is **`s24_rj_i32`**: 24-bit two's complement, right-aligned in an
`int32`, upper byte not sign-extended (`sound-byte-libs/src/sbl/dsp/types/convert.hpp`).
Readers MUST sign-extend (`(x << 8) >> 8`). This is the SAI DMA memory format, not the
MSB-justified serial frame (RM0433 Rev 8 §51.4.5, p. 2242).

## 5. Clock and doorbell

The host is the clock master. Each client gets two eventfds: `tick` (host → client) and
`done` (client → host). Per block the host:

1. Copies state routes (ADC/GPIO/DAC) for all apps.
2. Walks apps in **route order** (topological over audio/MIDI routes; cycles are broken by
   treating the back edge as one block late). For each app: write its `AUDIO_IN` block,
   write 1 to `tick`, wait for `done`, then copy its `AUDIO_OUT` along its routes.
3. Delivers the final mix to the physical device (real-time mode) or discards/records it
   (offline mode).

A client's audio driver, in attached mode, opens no device. It blocks on `tick`, pulls one
block from `AUDIO_IN`, runs the app callback, pushes to `AUDIO_OUT`, writes 1 to `done`.
Latency is one block per hop and deterministic. Clients that do not do audio still receive
`tick` and MUST NOT need to answer it (the host waits only on clients that declared audio
ports).

**Modes.** `realtime`: ticks come from the physical device callback. `offline` (feature
`offline_clock`): the host ticks as fast as the slowest client completes, for a requested
number of blocks; there is no wall-clock timeout. Offline mode is what makes bit-exact
golden renders possible.

**Timeouts.** In realtime mode the host MUST NOT block the device callback on a slow
client; it skips the client's block (logging an xrun) and continues. A client waiting on
`tick` for more than 1 s in realtime mode SHOULD treat the host as gone (§8).

## 6. Routing and conversion

A route is `(src_app, src_port) → (dst_app, dst_port)`. The host validates on `patch`:

| Rule | Behaviour |
|---|---|
| kinds | `analog_out→analog_in`, `digital_out→digital_in`, `audio_out→audio_in`, `midi_out→midi_in` only, unless `cross_kind_routes` is acked |
| direction | outputs feed inputs; never in→in or out→out |
| audio shape | `sample_rate` and `channel_count` must match |
| range | analog: allowed; the host warns when the source range exceeds the destination's and **clips** at the destination's rails, as hardware would |

Analog conversion per copy: `v = decode(src_code, src.electrical)`,
`dst_code = encode(clamp(v, dst.range), dst.electrical)`, where `decode`/`encode` use
`range_v`, `bits`, and `inverted` from the SHIMs. Digital and MIDI are copied unchanged.
Fan-out is always allowed. An unpatched input holds its SHIM `idle_v`, encoded.

## 7. Runtime port configuration (feature `port_config`)

The host MAY send `port_config {app, port, electrical: {...}, idle_v}` at any time after
`ready`. The client updates its mutable electrical record for that port and increments the
region's `config_generation`. The app-visible effect is whatever the driver does with the
record (e.g. `CvConditioning` derived from it). This is virtio's configuration space with
a generation counter (virtio v1.2 §2.5, p. 24) applied to one port. Hardware targets have
no equivalent; their record is `constexpr`.

## 8. Failure semantics

- **App exits or crashes.** Its control socket and fds close. The host MUST remove its
  routes, release its regions, and continue running other apps.
- **Host exits or crashes.** The client's control socket reads EOF and `tick` reads fail.
  The driver MUST log one line naming the cause and terminate the process with a nonzero
  status. Continuing with frozen inputs is not permitted (RPT-031 §F7).
- **Protocol error** (bad magic, version mismatch, unknown kind required). Whoever detects
  it sends `bye {reason}` and closes.

## 9. Conformance

The reference host in `vdh/` and the fixtures in `tests/` are the conformance suite. A
client is conformant if it completes the handshake, honours `ready` ordering, sign-extends
audio, never writes into a full ring, and exits nonzero on host loss. A host is conformant
if it never ticks before `ready`, never blocks its device callback on a client, converts
analog routes as §6, and cleans up on client loss.

## Open items

- Wire format for `hello`/`welcome` fields is illustrative until the reference host lands.
- `cross_kind_routes` thresholds (analog→digital at 1.0 V? hysteresis?) — define when needed.
- Multi-host on one machine: session-scoped socket paths are sufficient; not specified further.
