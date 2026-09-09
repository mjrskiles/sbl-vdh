# sbl-vdh

Virtual Device Host for [Sound Byte Libs](https://github.com/mjrskiles/sound-byte-libs)
applications. Runs SBL firmware builds as ordinary Linux processes and patches them
together like a modular synth: audio, CV, gates, and MIDI flow between apps through a host
that owns the clock and the routing table. The apps cannot tell they are not on hardware.

This repo holds the parts of that system that are meant to be shared:

| Path | What |
|---|---|
| `spec/` | The host↔driver protocol, region layouts, and the SHIM (SBL Hardware Interface Manifest) contract. Versioned independently of ordinary docs; see `spec/README.md`. |
| `include/sbl_vdh/` | C headers for the region layouts. The `linux-arm-host` drivers in sbl-hardware compile against these. |
| `vdh/` | Reference host in Python: clock master (real-time or offline), routing, physical audio/MIDI ingress, a `vdh` CLI. Enough to run a multi-app bench headless and to drive deterministic golden-render tests. |
| `tests/` | Protocol conformance tests, using the reference host against small fixture clients. |

Design history: `docs/planning/reports/RPT-031` and `feature-designs/FDP-076` in the
sound-byte-labs workspace. The paravirtual model follows virtio's shape — status field,
feature bits, a config space with a generation counter, typed queues — scaled down to one
process talking to a handful of others on one machine.

MIT. See `LICENSE`.
