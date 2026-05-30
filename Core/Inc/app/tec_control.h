#ifndef TEC_CONTROL_H
#define TEC_CONTROL_H

#include "pid.h"
#include "bsp_tec.h"

/* Exposed so CLI can read/write Kp, Ki, Kd, setpoint directly. */
extern PID_t g_crystal_pid;
extern PID_t g_laser_pid;

/*
 * Wavelength–temperature linear model for the crystal: λ = k·T + m
 * Inverse: T = (λ − m) / k
 * Default values are from characterisation data (k=0.1929 nm/°C, m=1548.6 nm).
 */
extern float g_crystal_wl_k;  /* slope  (nm/°C), min 4 decimal places */
extern float g_crystal_wl_m;  /* offset (nm),    min 2 decimal places */

/* Init both TEC channels and PID controllers with default parameters. */
void TEC_Control_Init(void);

/* Run one control iteration (call every 100 ms from main loop). */
void TEC_Control_Tick(void);

/* Read last measured temperature and PID output for a channel. */
void TEC_Control_GetState(TecChannel_t ch, float *temp_out, float *output_out);

/* Set crystal PID setpoint by wavelength; converts via T = (λ − m) / k. */
void TEC_Crystal_SetWavelength(float nm);

#endif /* TEC_CONTROL_H */
