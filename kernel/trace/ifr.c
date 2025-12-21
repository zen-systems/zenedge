/* kernel/trace/ifr.c */
#include "ifr.h"
#include "../time/time.h"
#include "../lib/sha256.h"
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
    out->version = IFR_VERSION;
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
    if (rec->version != IFR_VERSION)
        return 0;
    if (rec->record_size != sizeof(*rec))
        return 0;
    if (rec->profile_len > IFR_PROFILE_MAX)
        return 0;

    uint8_t expected[32];
    sha256_hash((const uint8_t *)rec, offsetof(ifr_record_t, hash), expected);
    return (memcmp(expected, rec->hash, sizeof(expected)) == 0) ? 1 : 0;
}
