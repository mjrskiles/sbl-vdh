/* sbl-vdh region layouts — v0 (draft). See spec/protocol.md §3–§4 and spec/regions.md.
 * C11. Included by the linux-arm-host drivers (C++) and readable from Python via struct. */
#ifndef SBL_VDH_REGIONS_H_
#define SBL_VDH_REGIONS_H_

#include <stdint.h>
#ifdef __cplusplus
#include <atomic>
#define SBL_VDH_ATOMIC_U32 std::atomic<uint32_t>
#else
#include <stdatomic.h>
#define SBL_VDH_ATOMIC_U32 _Atomic uint32_t
#endif

#define SBL_VDH_MAGIC          "SBLV"
#define SBL_VDH_HDR_VERSION    0u
#define SBL_VDH_HDR_SIZE       64u

enum sbl_vdh_kind {
    SBL_VDH_KIND_ADC_IN    = 1,
    SBL_VDH_KIND_GPIO_IN   = 2,
    SBL_VDH_KIND_GPIO_OUT  = 3,
    SBL_VDH_KIND_DAC_OUT   = 4,
    SBL_VDH_KIND_AUDIO_IN  = 5,
    SBL_VDH_KIND_AUDIO_OUT = 6,
    SBL_VDH_KIND_MIDI_IN   = 7,
    SBL_VDH_KIND_MIDI_OUT  = 8
};

struct sbl_vdh_region_hdr {
    char               signature[4];
    uint32_t           version;
    uint32_t           kind;
    uint32_t           flags;
    uint32_t           channel_count;
    uint32_t           block_size;
    uint32_t           sample_rate;
    uint32_t           stride;
    uint32_t           data_offset;
    SBL_VDH_ATOMIC_U32 seq;
    SBL_VDH_ATOMIC_U32 head;
    SBL_VDH_ATOMIC_U32 tail;
    uint32_t           capacity;
    SBL_VDH_ATOMIC_U32 config_generation;
    uint32_t           reserved[2];
};

#ifdef __cplusplus
static_assert(sizeof(sbl_vdh_region_hdr) == SBL_VDH_HDR_SIZE, "region header must be 64 bytes");
#else
_Static_assert(sizeof(struct sbl_vdh_region_hdr) == SBL_VDH_HDR_SIZE, "region header must be 64 bytes");
#endif

#endif /* SBL_VDH_REGIONS_H_ */
