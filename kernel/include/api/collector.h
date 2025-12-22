#ifndef _API_COLLECTOR_H
#define _API_COLLECTOR_H

#include <stdint.h>

/* Normalized Telemetry Snapshot */
typedef struct {
  uint64_t timestamp;
  uint32_t gpu_temp_c;   /* Degrees C */
  uint32_t gpu_util_pct; /* 0-100 */
  uint32_t mem_used_mb;
  uint32_t pcie_bw_mbps;

  /* Errors (Counters) */
  uint32_t ecc_errors;
  uint32_t pcie_retries;
  uint32_t xid_errors;
} metric_snapshot_t;

/* Abstract Collector Interface */
typedef struct collector {
  const char *name;

  /* Methods */
  int (*get_snapshot)(struct collector *self, metric_snapshot_t *out);

  /* Private driver data */
  void *priv;
} collector_t;

/* Global registry */
void collector_register(collector_t *col);
collector_t *collector_get_default(void);

#endif /* _API_COLLECTOR_H */
