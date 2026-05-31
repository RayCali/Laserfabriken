#ifndef TEC_CONTROL_H
#define TEC_CONTROL_H

#include <stdint.h>
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

/*
 * Laser TEC temperature protection.
 *
 * Absolute max:  laser is disabled immediately if temp exceeds this value.
 * Deviation guard: after the laser temp has stayed within ±max_dev_C of the
 *   setpoint for temp_timer_s seconds (settling), any deviation larger than
 *   max_dev_C disables the laser.
 * The guard resets (re-enters settling phase) on laser enable or setpoint change.
 */
extern float   g_laser_temp_max_dev_C;       /* max deviation once settled (°C) */
extern float   g_laser_temp_abs_max_C;       /* absolute max temp (°C) */
extern float   g_laser_temp_timer_s;         /* seconds within deviation before guard arms */
extern uint8_t g_laser_temp_guard_armed;     /* 1 = guard active */
extern float   g_laser_temp_settle_elapsed_s;/* seconds spent within deviation so far */

#define LASER_TEMP_TRIP_NONE    0u
#define LASER_TEMP_TRIP_ABS_MAX 1u
#define LASER_TEMP_TRIP_DEV     2u
extern uint8_t g_laser_temp_trip_reason;     /* last trip cause, cleared on guard reset */
extern uint8_t g_laser_temp_trip_pending;    /* set when tripped, cleared after CLI prints */

/* Simulation overrides — inject a fixed temperature without real hardware. */
extern uint8_t g_sim_laser_temp_enable;
extern float   g_sim_laser_temp_C;

/* Init both TEC channels and PID controllers with default parameters. */
void TEC_Control_Init(void);

/* Run one control iteration (call every 100 ms from main loop). */
void TEC_Control_Tick(void);

/* Read last measured temperature and PID output for a channel. */
void TEC_Control_GetState(TecChannel_t ch, float *temp_out, float *output_out);

/* Set crystal PID setpoint by wavelength; converts via T = (λ − m) / k. */
void TEC_Crystal_SetWavelength(float nm);

/* Reset laser temp guard to unsettled state (call on laser enable or setpoint change). */
void TEC_LaserTempGuard_Reset(void);

#endif /* TEC_CONTROL_H */
