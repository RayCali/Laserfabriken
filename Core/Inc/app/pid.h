#ifndef PID_H
#define PID_H

#include <stdint.h>

typedef struct {
    float  Kp;
    float  Ki;
    float  Kd;
    float  setpoint;
    float  output_min;
    float  output_max;
    /* EMA coefficient for D-term low-pass filter.
     * 0 < d_alpha <= 1: lower value = heavier filtering.
     * Start around 0.1 to suppress ADC noise on the derivative. */
    float  d_alpha;
    /* double accumulator prevents drift at mK precision over long runtimes. */
    double integral;
    float  prev_error;
    float  d_filtered;
} PID_t;

void  PID_Init(PID_t *pid,
               float Kp, float Ki, float Kd,
               float setpoint,
               float out_min, float out_max,
               float d_alpha);

void  PID_Reset(PID_t *pid);

/* Run one PID iteration. dt_s: elapsed time in seconds since last call. */
float PID_Update(PID_t *pid, float measured, float dt_s);

#endif /* PID_H */
