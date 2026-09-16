# VDH protocol — v0 (draft)

Status: draft, 2026-09-09; proposals for the v0 review added 2026-09-15. Derived from
RPT-031 (sound-byte-labs) findings F3–F8, all accepted. Nothing here is implemented yet.

> **Review guide (AP-039 Phase 2).** Blocks marked *Proposal* are Claude's, written from
> what building the bench needed (FDP-079); each is a decision for Michael. In order of
> consequence: (1) §2 the control-channel encoding — binary structs for the handshake,
> JSON for operators; (2) §10 the operator API sidecar's daemon and any front end need;
> (3) §2 how a bench names its host. Decided (2026-09-15): attached only — no standalone
> driver path; the schedule is pipelined, one block per hop; the host never spawns; the
> host never mixes and never merges — fan-in refused for every kind, a Mixer app sums
> audio and a MIDI utility app merges and splits; the header field is `signature`; a
> stopped client's consumers hear silence. Accepting a proposal means deleting the word
> *Proposal* from it; rejecting one means saying what instead.

## 1. Model

A **host** owns the physical audio and MIDI devices (or runs with none, offline), a set of
shared-memory **regions** per attached app, a **routing table**, and the **clock**. A
**client** is one of the `linux-arm-host` drivers inside an app process. An app attaches by
having `SBL_VDH` in its environment when its drivers initialize. The host never spawns
apps (Michael, FDP-079 markup): whoever starts an app — sidecar, a shell, a debugger —
sets the variable, and the host learns of the app when it connects.

Regions hold **device codes** — what the driver would see from real hardware: `uint16`
ADC counts, `0/1` pin states, 24-bit audio in `int32`, MIDI bytes. The host converts between
electrically different ports along a route using each end's SHIM electrics
(source code → volts → destination code). The virtual cable is volts; the conversion is the
host's and happens once. Clients never convert.

This mirrors virtio's basic facilities (virtio v1.2 §2, p. 21): a status sequence, feature
bits, a configuration space with a generation counter, notifications, and typed queues.
The transport is the one QEMU's vhost-user protocol uses between a VM and a userspace
device backend — a Unix socket for control, memory regions passed as file descriptors
via `SCM_RIGHTS`, eventfds as kick/call doorbells, feature negotiation — with no VM and
no virtqueues (https://qemu.readthedocs.io/en/master/interop/vhost-user.html).

## 2. Discovery and handshake

- `SBL_VDH=<path>` — a Unix domain socket the host listens on.

**Attached only** (decided, Michael 2026-09-15). A `linux-arm-host` app has no standalone
path: with `SBL_VDH` unset the driver logs one line and exits nonzero, the same rule as
a host that goes away (§8). The reasoning: a standalone path (miniaudio, rawmidi, idle
counts) beside an attached one is two paths in four drivers, forever, and the kind of
drift the golden render exists to catch. Miniaudio and rawmidi live in the host, which
needs them for real-time mode anyway; `usb_stubs.cpp`'s rawmidi bridge is deleted rather
than kept beside its replacement. Consequences: the client drivers and the reference
host land in one phase, the bench stays on virmidi until they do, the quick check
(`SBL_AUDIO_DEVICE=H5studio ./davis_jr`) becomes `vdh --solo ./davis_jr` — a host that
patches one app's audio and MIDI straight to the interface, which is what
`sidecar new -a` already means — and CI always needs the host, which it needs for the
render regardless.
- The **control channel** is that socket, with file descriptors passed via `SCM_RIGHTS`
  where noted. The data path never touches it.

*Proposal (2026-09-15) — encoding.* Two peers, two encodings, by role. **Clients**
(drivers inside apps) speak fixed-size, little-endian binary structs with a common
header, so the C++ driver parses `welcome` with a cast and no JSON parser lives in a
driver (RPT-031 §F5's rule); Python reads them with `struct`. **Operators** (sidecar, a
front end, a test) speak newline-delimited JSON on the same socket (§10). The first bytes
decide: a client's first message is the binary `hello` whose header begins `"SBLV"`; an
operator's first line begins `{`. Every binary message:

```c
struct sbl_vdh_msg_hdr { char signature[4]; /* "SBLV" */ uint16_t version; /* 0 */ uint16_t type; uint32_t length; /* bytes after the header */ };
// type 1 hello:    { uint32_t proto; uint32_t features; uint32_t pid; char app[32]; uint8_t shim_sha256[32]; }
// type 2 welcome:  { uint32_t proto; uint32_t features; uint32_t region_count; uint32_t doorbell_count; }
//                  followed by region_count × { uint32_t kind; uint32_t fd_index; uint32_t size; uint32_t port_count; char ports[port_count][32]; }
//                  with the fds (regions, then tick, done) attached to this message
// type 3 ready:    {}
// type 4 bye:      { char reason[64]; }
// type 5 port_config: { char port[32]; float lo_v, hi_v; uint16_t bits; uint8_t inverted; float idle_v; }
```

Features are a bit set in this encoding (§2 lists them by name in feature-bit order:
`offline_clock` = bit 0, `port_config` = 1, `midi_out` = 2, `cross_kind_routes` = 3).
A host that does not recognise the signature closes the socket.

*Proposal (2026-09-15) — naming a host.* `SBL_VDH` is the whole of discovery. Whoever
starts apps (sidecar, a script, a shell) sets it in their environment; a bench that
names its own host install points `SBL_VDH` at that host's socket and nothing else
changes. The host announces nothing, registers nowhere, and there is one host per
socket path. Session-scoped paths (`$XDG_RUNTIME_DIR/sbl-vdh/<bench>.sock`) are a
convention of the operator, not of this protocol.

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
- `features` are strings (bits in the binary encoding). v0 defines: `offline_clock`,
  `port_config`, `midi_out`, `cross_kind_routes`. A client MUST NOT use a feature the
  host did not ack.
- Each `regions[]` entry: `{kind, ports: [id...], fd_index, size}`. Kinds are listed in §4.
  A client MUST accept regions in any order and MAY ignore kinds it does not drive.
- The host MUST NOT tick a client before `ready` (virtio: no buffers before `DRIVER_OK`).

Region memory is `memfd`-backed. Lifetime is fd lifetime: when either side dies, the
kernel closes its fds and the other side observes it (`recv` returns 0 on the control
socket; `read` on the doorbell fails). No named `/dev/shm` objects, nothing to clean up.
The host MUST apply `F_SEAL_SHRINK | F_SEAL_GROW` to every region memfd before sending it,
so no client can resize a region under the host; a client MAY verify the seals with
`F_GET_SEALS` and treat their absence as a protocol error.

## 3. Region header

Every region starts with a 64-byte header (`include/sbl_vdh/regions.h`):

```c
struct sbl_vdh_region_hdr {
    char     signature[4];    // "SBLV"
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
per-kind struct. Readers MUST check `signature`, `version`, and that `size >= data_offset +
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
xrun; MIDI: overflow) — data is never silently overwritten. Cursors are shared memory the
other side wrote: both sides MUST reduce `head` and `tail` modulo `capacity` before
indexing and MUST NOT trust a cursor delta larger than `capacity` (treat it as a
protocol error). Seals stop a resize; nothing but this rule stops a bad cursor.

State regions are written element by element. In offline mode the host writes all state
routes before ticking, so an app never observes a partial update. In realtime mode a
multi-channel write can interleave with an app read, exactly as a DMA scan does on
hardware; this is intended, and `seq` exists for the readers that need better.

Audio sample format is **`s24_rj_i32`**: 24-bit two's complement, right-aligned in an
`int32`, upper byte not sign-extended (`sound-byte-libs/src/sbl/dsp/types/convert.hpp`).
Readers MUST sign-extend (`(x << 8) >> 8`). This is the SAI DMA memory format, not the
MSB-justified serial frame (RM0433 Rev 8 §51.4.5, p. 2242).

## 5. Clock and doorbell

The host is the clock master. Each client gets two eventfds: `tick` (host → client) and
`done` (client → host).

**The schedule is pipelined** (decided, Michael 2026-09-15): every app is ticked at once,
each consuming what its sources published for the previous block. Every hop between
apps therefore costs one block of latency — 1 ms at 48 frames; the bench's chain of
Maestro, a voice and a mixer adds about 3 ms on top of the interface's own ~6 ms — and
that is a consequence of the simplest host that runs five apps on four cores, not a
goal. The alternatives, for the record: a serial walk (tick A, wait, copy to B, tick B)
adds no hop latency but runs apps one after another, so the sum of their block times
must fit one period, which four Davis Jr. voices at ~37 % of a core cannot; a wave
schedule (JACK2's) runs independent branches in parallel and chains serially, with no
forward-hop latency and a one-block delay only on cycles, at the price of a topological
sort per patch change. Waves is a contained upgrade if a long chain ever matters — the
clients see the same `tick` and `done` either way — and the render is bit-exact under
every one of them. A hardware rack of digital modules has the pipelined shape already:
each module runs its own block. Per block the host:

1. Copy every route — state (ADC/GPIO/DAC) and stream (audio, MIDI) — from what each
   source published for the previous block. Fan-in and conversion are §6.
2. Write 1 to every audio client's `tick`; wait for every `done` (realtime: or the
   deadline, §Timeouts).
3. Deliver the physical output ports' blocks to the device (real-time mode) or discard
   or record them (offline mode).

A client's audio driver opens no device. It blocks on `tick`, pulls one block from
`AUDIO_IN`, runs the app callback, pushes to `AUDIO_OUT`, writes 1 to `done`. Clients
that do not do audio still receive `tick` and MUST NOT need to answer it (the host waits
only on clients that declared audio ports). Route order no longer exists: a cycle is
just a route like any other, one block late by construction.

An eventfd accumulates writes. A client that reads a `tick` value greater than 1 has
missed ticks: it MUST run exactly one block, count the remainder as xruns, and log them.
It MUST NOT run the missed blocks late. The thread that waits on `tick` SHOULD run with
the same real-time scheduling the standalone audio thread uses (SCHED_FIFO below the
system audio server's priority, memory locked); without it a busy host starves the app.

**Modes.** `realtime`: ticks come from the physical device callback. `offline` (feature
`offline_clock`): the host ticks as fast as the slowest client completes, for a requested
number of blocks; there is no wall-clock timeout. Offline mode is what makes bit-exact
golden renders possible. The reference host does both (Michael, AP-039 Q1, 2026-09-14):
it owns the physical interface in realtime mode, through an audio library it declares as
a dependency, and needs none offline.

**Timeouts.** In realtime mode the host MUST NOT block the device callback on a slow
client; it skips the client's block (logging an xrun) and continues. What the client's
consumers receive for that block is **silence** (decided 2026-09-15): a halted MCU's
codec would keep clocking its last DMA buffer, but silence is the one thing nobody
mistakes for a working app. A client waiting on `tick` for more than 1 s in realtime mode
SHOULD treat the host as gone (§8).

## 6. Routing and conversion

A route is `(src_app, src_port) → (dst_app, dst_port)`. The host validates on `patch`:

| Rule | Behaviour |
|---|---|
| kinds | `analog_out→analog_in`, `digital_out→digital_in`, `audio_out→audio_in`, `midi_out→midi_in` only, unless `cross_kind_routes` is acked |
| direction | outputs feed inputs; never in→in or out→out |
| audio shape | `sample_rate` and `channel_count` must match, per port — a Mixer's inputs each have the codec's channel count |
| range | analog: allowed; the host warns when the source range exceeds the destination's and **clips** at the destination's rails, as hardware would |

Analog conversion per copy: `v = decode(src_code, src.electrical)`,
`dst_code = encode(clamp(v, dst.range), dst.electrical)`, where `decode`/`encode` use
`range_v`, `bits`, and `inverted` from the SHIMs. Digital and MIDI are copied unchanged.
Fan-out is always allowed. An unpatched input holds its SHIM `idle_v`, encoded.

*Fan-in, revised after the review (2026-09-15; the first proposal made the host "most of
a mixer" with per-route gain and audio summing, and Michael did not like that).* The
host is a patch bay, not a desk: it copies and converts, never mixes. There is no
per-route gain. Fan-in by kind:

| Kind | Fan-in (several routes into one input) |
|---|---|
| audio | **refused**. Summing is a Mixer app's job — `modules::Mixer` wrapped as an SBL app, patched like any module (`sketchbook/bench-mixing.md`). The bench's four voices go through one such app to the interface, each into its own input: an app declares several audio inputs, `audio_in_1`..`audio_in_n`, one region each (`shim.md`, audio inputs; decided 2026-09-15). |
| analog | **refused**: two CV outputs into one jack is a short on hardware |
| digital | **refused** for the same reason |
| midi | **refused** (decided, Michael 2026-09-15): a MIDI cable carries one jack to another, and a merge box is a module you buy |

Fan-out stays free for every kind. MIDI merging (two controllers into one app) and
channel filtering or splitting (one controller to four voices by channel) are a small
utility app — merge in, split or filter out by channel, thru — patched like any module,
not features of the host; the earlier `midi_channel_filter` feature is withdrawn. With
per-app ports the bench no longer needs a channel bus at all: Maestro gets its four
outputs back (FDP-078's first drawing), one route per voice.

*Cross-kind routes* (feature `cross_kind_routes`; decided on the bench, Michael
2026-09-10, no longer open): analog→digital is a Schmitt trigger, high at ≥ 1.0 V, low
at < 0.5 V (the common Eurorack trigger convention); digital→analog emits 0 V for low and
5 V for high, then `encode` at the destination. The Patch SM's real comparator threshold
is unmeasured (`patch-sm-ds v1.0.5` p. 7 says only "0 to 5 V typical"); the convention
ships and gets fixed only if it proves a problem.

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
- **Protocol error** (bad signature, version mismatch, unknown kind required). Whoever detects
  it sends `bye {reason}` and closes.
- **Liveness beyond fds.** The host MAY hold a `pidfd` for every attached app (works
  whether or not the host spawned it) and MAY set `PR_SET_PDEATHSIG` on apps it spawns.
  Neither replaces the fd-lifetime rules above; they shorten detection.

## 9. Conformance

The reference host in `vdh/` and the fixtures in `tests/` are the conformance suite. A
client is conformant if it completes the handshake, honours `ready` ordering, sign-extends
audio, never writes into a full ring, and exits nonzero on host loss. A host is conformant
if it never ticks before `ready`, never blocks its device callback on a client, converts
analog routes as §6, and cleans up on client loss.

## 10. Operator API — *Proposal (2026-09-15)*

A host without an operator is a rack with no hands. Sidecar's daemon (FDP-079 Phase 5),
a web front end (Phase 6) and a test fixture all need the same few verbs, so they are
part of the protocol rather than of any one host. Operators connect to the same socket
and speak newline-delimited JSON: one request line, one response line (`{ok: true, ...}`
or `{ok: false, error}`), and after `subscribe`, event lines as they happen. The host
serves any number of operators; it does not spawn apps (Michael, FDP-079 markup) and
knows nothing about how they were started.

| Request | Response | Meaning |
|---|---|---|
| `{op: "list"}` | `{apps: [{name, pid, state, ports: [{id, kind, ...from the SHIM}]}], routes: [{src, dst}]}` | the rack as it stands |
| `{op: "patch", src: "maestro.midi_out_1", dst: "voice1.midi_in"}` | `{ok}` | a route, validated as §6 |
| `{op: "unpatch", src, dst}` | `{ok}` | |
| `{op: "status"}` | `{mode, block_size, sample_rate, blocks, xruns: {app: n}, overflows: {app: n}, routes: [{src, dst, bytes, messages}]}` | the numbers the dashboard draws |
| `{op: "read", app, port}` | `{value}` | one state port's current value, decoded to volts or 0/1: the dashboard's real gauges |
| `{op: "subscribe"}` | events until the socket closes | `{event: "attached"\|"detached"\|"xrun"\|"patched"\|"unpatched"\|"clip", ...}` |
| `{op: "record", ports: ["voice1.audio_out", ...], path, blocks?}` | `{ok, take}` | the host writes those ports' blocks to one file per port from the next tick, until `blocks` or `{op: "stop", take}`; the files line up to the sample by construction |
| `{op: "run", blocks}` | `{ok}` after the last block | offline mode only: tick `blocks` blocks |
| `{op: "quit"}` | `{ok}` | the host says `bye` to every client and exits |

Ports are named `app.port` with the SHIM's port ids (`spec/shim.md`), which is also the
route vocabulary in sidecar's session files. `read` and `record` give a bench everything
the virmidi taps and the PipeWire recorder gave it, from the host's own buffers.

## Open items

- Multi-host on one machine: session-scoped socket paths are sufficient; not specified further.
- **Control channel encoding.** Now a proposal in §2 (binary for clients, JSON for
  operators). The alternative — a header-only JSON parser confined to the host driver —
  stays on the table if the struct route reads badly.
