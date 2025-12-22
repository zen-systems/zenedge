/* kernel/trace/ifr.h */
#ifndef ZENEDGE_IFR_H
#define ZENEDGE_IFR_H

#include <stdint.h>
#include <stddef.h>

#define IFR_MAGIC 0x30465249u /* "IFR0" */
#define IFR_VERSION 2
#define IFR_VERSION_V2 2
#define IFR_VERSION_V3 3
#define IFR_PROFILE_MAX 16
#define IFR_RECORD_SIZE 136u

#define IFR_V3_RECORD_SIZE 324u

/* IFR flags (shared across versions) */
#define IFR_FLAG_SIG_UNAVAILABLE          0x0001
#define IFR_FLAG_MODEL_DIGEST_MISSING     0x0002
#define IFR_FLAG_POLICY_DIGEST_PLACEHOLDER 0x0004
#define IFR_FLAG_SEAL_HASH_MISSING        0x0008

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

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t record_size;
    uint32_t job_id;
    uint32_t episode_id;
    uint32_t model_id;
    uint64_t ts_usec;
    float goodput;
    uint8_t nonce[32];
    uint8_t model_digest[32];
    uint8_t policy_digest[32];
    uint8_t flightrec_seal_hash[32];
    uint8_t prev_chain_hash[32];
    uint8_t ifr_hash[32];
    uint8_t chain_hash[32];
    uint8_t sig_classical[64];
} ifr_record_v3_t;

typedef char ifr_v3_size_check[(sizeof(ifr_record_v3_t) == IFR_V3_RECORD_SIZE) ? 1 : -1];

void ifr_build(ifr_record_t *out,
               uint32_t job_id,
               uint32_t episode_id,
               uint32_t model_id,
               const float *profile,
               uint16_t profile_len,
               float goodput);

int ifr_verify(const ifr_record_t *rec);
void ifr_build_v3(ifr_record_v3_t *out,
                  const uint8_t prev_chain_hash[32],
                  uint32_t job_id,
                  uint32_t episode_id,
                  uint32_t model_id,
                  float goodput);
int ifr_verify_v3(const ifr_record_v3_t *rec);

#endif
