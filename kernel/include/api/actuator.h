#ifndef _API_ACTUATOR_H
#define _API_ACTUATOR_H

#include <stdint.h>

/* Actuator capability flags */
#define ACT_CAP_CLOCK_LOCK 0x0001
#define ACT_CAP_POWER_LIMIT 0x0002
#define ACT_CAP_FAN_CONTROL 0x0004

/* Result codes */
#define ACT_OK 0
#define ACT_ERR_NOSUPP -1
#define ACT_ERR_LIMIT -2
#define ACT_ERR_HW -3

/* Abstract Actuator Interface */
typedef struct actuator {
  const char *name;
  uint32_t capabilities;

  /* Methods */
  int (*set_clock_limit)(struct actuator *self, uint32_t clock_mhz);
  int (*set_power_limit)(struct actuator *self, uint32_t power_watts);
  int (*reset_defaults)(struct actuator *self);

  /* Private driver data */
  void *priv;
} actuator_t;

/* Global registry (simplification for MVP) */
void actuator_register(actuator_t *act);
actuator_t *actuator_get_default(void);

#endif /* _API_ACTUATOR_H */
