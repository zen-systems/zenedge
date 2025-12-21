/* kernel/include/oracle.h */
#ifndef ZENEDGE_ORACLE_H
#define ZENEDGE_ORACLE_H

#include <stdint.h>

typedef enum {
    VERDICT_PASS = 0,
    VERDICT_THROTTLE = 1,
    VERDICT_KILL = 2
} verdict_t;

verdict_t get_job_verdict(uint64_t job_id);

#endif /* ZENEDGE_ORACLE_H */
