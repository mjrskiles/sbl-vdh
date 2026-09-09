# sbl-vdh specification

Normative documents for the Virtual Device Host. They live here, not in `docs/`, because
they are contracts with a version number, not explanations: a driver built against
protocol v1 must work with any host speaking v1.

| Document | Covers |
|---|---|
| `protocol.md` | Discovery, handshake, feature bits, region assignment, clock and doorbell semantics, runtime port configuration, failure semantics |
| `regions.md` | Binary layout of shared-memory regions (state and stream kinds); mirrored by `include/sbl_vdh/regions.h` |
| `shim.md` | What a SHIM must contain for a host to route it. The schema itself is owned by `sbl-schema` in sound-byte-libs; this document states the host's requirements on it |

## Versioning

One number, `protocol_version`, covers all three documents together. Optional capabilities
are **feature bits** negotiated at hello time, so the version only moves when something
mandatory changes. Drafts are `0`; the first stable release is `1`.

## Conventions

RFC 2119 keywords (MUST, SHOULD, MAY) are used in their usual sense. "Host" is the VDH.
"Client" is a driver inside an app process. "App" is the SBL application, which never sees
any of this.
