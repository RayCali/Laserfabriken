#ifndef TEC_CONTROL_H
#define TEC_CONTROL_H

#include "pid.h"
#include "bsp_tec.h"

/* Exposed so CLI can read/write Kp, Ki, Kd, setpoint directly. */
extern PID_t g_crystal_pid;
extern PID_t g_laser_pid;

/* Init both TEC channels and PID controllers with default parameters. */
void TEC_Control_Init(void);

/* Run one control iteration (call every 100 ms from main loop). */
void TEC_Control_Tick(void);

/* Read last measured temperature and PID output for a channel. */
void TEC_Control_GetState(TecChannel_t ch, float *temp_out, float *output_out);

#endif /* TEC_CONTROL_H */
