#include "../console.h"
#include "../include/api/actuator.h"
#include "../include/api/collector.h"

/* Mock State */
static uint32_t current_clock = 1000;
static uint32_t current_power = 250;
static uint32_t current_temp = 65;
static uint32_t current_util = 40;

/* Actuator Implementation */
static int mock_set_clock(actuator_t *self, uint32_t clock_mhz) {
  (void)self;
  current_clock = clock_mhz;
  /* Simulate thermal effect */
  current_temp += (clock_mhz > 1500) ? 5 : 1;
  return ACT_OK;
}

static int mock_set_power(actuator_t *self, uint32_t watts) {
  (void)self;
  current_power = watts;
  return ACT_OK;
}

static int mock_reset(actuator_t *self) {
  (void)self;
  current_clock = 1000;
  current_power = 250;
  current_temp = 60;
  return ACT_OK;
}

static actuator_t mock_actuator = {.name = "MockGPU-A100",
                                   .capabilities =
                                       ACT_CAP_CLOCK_LOCK | ACT_CAP_POWER_LIMIT,
                                   .set_clock_limit = mock_set_clock,
                                   .set_power_limit = mock_set_power,
                                   .reset_defaults = mock_reset,
                                   .priv = 0};

/* Collector Implementation */
static int mock_get_snapshot(collector_t *self, metric_snapshot_t *out) {
  (void)self;
  out->timestamp = 0; // TODO: rdtsc
  out->gpu_temp_c = current_temp;
  out->gpu_util_pct = current_util;
  out->ecc_errors = 0;
  out->xid_errors = 0;

  /* Simulate cooling */
  if (current_temp > 60)
    current_temp--;

  /* Simulate utilization jitter */
  current_util = (current_util + 1) % 100;

  return 0;
}

static collector_t mock_collector = {
    .name = "MockGPU-A100", .get_snapshot = mock_get_snapshot, .priv = 0};

/* Registry Stubs for MVP */
actuator_t *actuator_get_default(void) { return &mock_actuator; }

collector_t *collector_get_default(void) { return &mock_collector; }
