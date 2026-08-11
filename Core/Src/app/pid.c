#include "pid.h"

void PID_Init(PID_t *pid,
              float Kp, float Ki, float Kd,
              float setpoint,
              float out_min, float out_max,
              float d_alpha)
{
    pid->Kp         = Kp;
    pid->Ki         = Ki;
    pid->Kd         = Kd;
    pid->setpoint   = setpoint;
    pid->output_min = out_min;
    pid->output_max = out_max;
    pid->d_alpha    = d_alpha;
    pid->enabled    = 0;
    PID_Reset(pid);
}

void PID_Reset(PID_t *pid)
{
    pid->integral   = 0.0;
    pid->prev_error = 0.0f;
    pid->d_filtered = 0.0f;
}

float PID_Update(PID_t *pid, float measured, float dt_s)
{
    float error = pid->setpoint - measured;

    /* Proportional term */
    float p = pid->Kp * error;

    /* Integral term — accumulated in double to avoid drift over long runtimes */
    pid->integral += (double)(pid->Ki * error * dt_s);

    /* Derivative term with EMA low-pass filter (suppresses ADC noise) */
    float d_raw = (dt_s > 0.0f)
                  ? pid->Kd * (error - pid->prev_error) / dt_s
                  : 0.0f;
    pid->d_filtered = pid->d_alpha * d_raw
                    + (1.0f - pid->d_alpha) * pid->d_filtered;
    pid->prev_error = error;

    float output = p + (float)pid->integral + pid->d_filtered;

    /* Clamp output and back-calculate integral (anti-windup).
     * Keeps integral at the value that produces exactly the clamped output,
     * so it resumes normally as soon as the plant leaves saturation. */
    if (output > pid->output_max) {
        output        = pid->output_max;
        pid->integral = (double)(pid->output_max - p - pid->d_filtered);
    } else if (output < pid->output_min) {
        output        = pid->output_min;
        pid->integral = (double)(pid->output_min - p - pid->d_filtered);
    }

    return output;
}
