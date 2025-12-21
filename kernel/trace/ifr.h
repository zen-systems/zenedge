/* kernel/trace/ifr.h */
#ifndef ZENEDGE_IFR_H
#define ZENEDGE_IFR_H

#include <stdint.h>
#include <stddef.h>

#define IFR_MAGIC 0x30465249u /* "IFR0" */
#define IFR_VERSION 2
#define IFR_PROFILE_MAX 16
#define IFR_RECORD_SIZE 136u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t job_id;
    uint32_t episode_id;
    uint32_t model_id;
    uint32_t record_size;
    uint64_t ts_usec;
    float goodput;
    uint16_t profile_len;
    uint16_t reserved;
    float profile[IFR_PROFILE_MAX];
    uint8_t hash[32];
} ifr_record_t;

typedef char ifr_size_check[(sizeof(ifr_record_t) == IFR_RECORD_SIZE) ? 1 : -1];

void ifr_build(ifr_record_t *out,
               uint32_t job_id,
               uint32_t episode_id,
               uint32_t model_id,
               const float *profile,
               uint16_t profile_len,
               float goodput);

int ifr_verify(const ifr_record_t *rec);

#endif
