/* kernel/trace/ifr.c */
#include "ifr.h"
#include "../time/time.h"
#include "../lib/sha256.h"
#include "../ipc/heap.h"
#include "flightrec.h"
#include <string.h>

void ifr_build(ifr_record_t *out,
               uint32_t job_id,
               uint32_t episode_id,
               uint32_t model_id,
               const float *profile,
               uint16_t profile_len,
               float goodput) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->magic = IFR_MAGIC;
    out->version = IFR_VERSION_V2;
    out->flags = 0;
    out->job_id = job_id;
    out->episode_id = episode_id;
    out->model_id = model_id;
    out->record_size = sizeof(*out);
    out->ts_usec = time_usec();
    out->goodput = goodput;

    if (profile && profile_len > 0) {
        if (profile_len > IFR_PROFILE_MAX)
            profile_len = IFR_PROFILE_MAX;
        out->profile_len = profile_len;
        for (uint16_t i = 0; i < profile_len; i++) {
            out->profile[i] = profile[i];
        }
    }

    sha256_hash((const uint8_t *)out, offsetof(ifr_record_t, hash), out->hash);
}

int ifr_verify(const ifr_record_t *rec) {
    if (!rec)
        return 0;
    if (rec->magic != IFR_MAGIC)
        return 0;
    if (rec->version != IFR_VERSION_V2)
        return 0;
    if (rec->record_size != sizeof(*rec))
        return 0;
    if (rec->profile_len > IFR_PROFILE_MAX)
        return 0;

    uint8_t expected[32];
    sha256_hash((const uint8_t *)rec, offsetof(ifr_record_t, hash), expected);
    return (memcmp(expected, rec->hash, sizeof(expected)) == 0) ? 1 : 0;
}

static void fill_nonce(uint8_t nonce[32]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    uint64_t t = time_usec();
    uint64_t c = time_cycles();
    sha256_update(&ctx, (const uint8_t *)&t, sizeof(t));
    sha256_update(&ctx, (const uint8_t *)&c, sizeof(c));
    sha256_final(&ctx, nonce);
}

static void compute_model_digest(uint32_t model_id, uint8_t out[32], uint16_t *flags) {
    void *data = heap_get_data((uint16_t)model_id);
    uint32_t size = heap_get_blob_size((uint16_t)model_id);
    if (!data || size == 0) {
        memset(out, 0, 32);
        if (flags) {
            *flags |= IFR_FLAG_MODEL_DIGEST_MISSING;
        }
        return;
    }
    sha256_hash((const uint8_t *)data, size, out);
}

static void compute_policy_digest(uint8_t out[32], uint16_t *flags) {
    const char *policy = "zenedge-policy-v1";
    sha256_hash((const uint8_t *)policy, strlen(policy), out);
    if (flags) {
        *flags |= IFR_FLAG_POLICY_DIGEST_PLACEHOLDER;
    }
}

void ifr_build_v3(ifr_record_v3_t *out,
                  const uint8_t prev_chain_hash[32],
                  uint32_t job_id,
                  uint32_t episode_id,
                  uint32_t model_id,
                  float goodput) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->magic = IFR_MAGIC;
    out->version = IFR_VERSION_V3;
    out->flags = 0;
    out->record_size = sizeof(*out);
    out->job_id = job_id;
    out->episode_id = episode_id;
    out->model_id = model_id;
    out->ts_usec = time_usec();
    out->goodput = goodput;

    fill_nonce(out->nonce);
    compute_model_digest(model_id, out->model_digest, &out->flags);
    compute_policy_digest(out->policy_digest, &out->flags);

    if (prev_chain_hash) {
        memcpy(out->prev_chain_hash, prev_chain_hash, 32);
    }

    flightrec_seal_hash(out->flightrec_seal_hash);

    /* IFR core hash excludes ifr_hash, chain_hash, and signature fields. */
    sha256_hash((const uint8_t *)out, offsetof(ifr_record_v3_t, ifr_hash), out->ifr_hash);

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, out->prev_chain_hash, 32);
    sha256_update(&ctx, out->ifr_hash, 32);
    sha256_update(&ctx, out->flightrec_seal_hash, 32);
    sha256_update(&ctx, out->nonce, 32);
    sha256_update(&ctx, out->model_digest, 32);
    sha256_update(&ctx, out->policy_digest, 32);
    sha256_final(&ctx, out->chain_hash);

    memset(out->sig_classical, 0, sizeof(out->sig_classical));
    out->flags |= IFR_FLAG_SIG_UNAVAILABLE;
}

int ifr_verify_v3(const ifr_record_v3_t *rec) {
    if (!rec)
        return 0;
    if (rec->magic != IFR_MAGIC)
        return 0;
    if (rec->version != IFR_VERSION_V3)
        return 0;
    if (rec->record_size != sizeof(*rec))
        return 0;

    uint8_t expected_ifr[32];
    sha256_hash((const uint8_t *)rec, offsetof(ifr_record_v3_t, ifr_hash), expected_ifr);
    if (memcmp(expected_ifr, rec->ifr_hash, 32) != 0)
        return 0;

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, rec->prev_chain_hash, 32);
    sha256_update(&ctx, rec->ifr_hash, 32);
    sha256_update(&ctx, rec->flightrec_seal_hash, 32);
    sha256_update(&ctx, rec->nonce, 32);
    sha256_update(&ctx, rec->model_digest, 32);
    sha256_update(&ctx, rec->policy_digest, 32);
    uint8_t expected_chain[32];
    sha256_final(&ctx, expected_chain);

    return (memcmp(expected_chain, rec->chain_hash, 32) == 0) ? 1 : 0;
}
