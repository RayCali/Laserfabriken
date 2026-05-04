#include "tec_control.h"
#include "bsp_temp.h"

PID_t g_crystal_pid;
PID_t g_laser_pid;

static float s_crystal_temp   = 0.0f;
static float s_laser_temp     = 0.0f;
static float s_crystal_output = 0.0f;
static float s_laser_output   = 0.0f;

/* Control loop period — must match the calling interval in main.c */
#define PID_PERIOD_S  0.1f

/* D-term EMA coefficient: 0.1 gives ~10 sample smoothing, good starting point.
 * Increase toward 1.0 for faster D response (more noise); decrease for less. */
#define D_ALPHA       0.1f

void TEC_Control_Init(void)
{
    BSP_Temp_Init();
    BSP_TEC_Init(TEC_CRYSTAL);
    BSP_TEC_Init(TEC_LASER);

    /* Default PID parameters — these are conservative starting values.
     * Tune Kp/Ki/Kd via serial terminal once real hardware is connected. */
    PID_Init(&g_crystal_pid, 0.5f, 0.02f, 0.1f, 25.0f, -1.0f, 1.0f, D_ALPHA);
    PID_Init(&g_laser_pid,   0.5f, 0.02f, 0.1f, 25.0f, -1.0f, 1.0f, D_ALPHA);
}

void TEC_Control_Tick(void)
{
    s_crystal_temp = BSP_Temp_Read(TEMP_CH_CRYSTAL);
    s_laser_temp   = BSP_Temp_Read(TEMP_CH_LASER);

    s_crystal_output = PID_Update(&g_crystal_pid, s_crystal_temp, PID_PERIOD_S);
    s_laser_output   = PID_Update(&g_laser_pid,   s_laser_temp,   PID_PERIOD_S);

    BSP_TEC_SetOutput(TEC_CRYSTAL, s_crystal_output);
    BSP_TEC_SetOutput(TEC_LASER,   s_laser_output);
}

void TEC_Control_GetState(TecChannel_t ch, float *temp_out, float *output_out)
{
    if (ch == TEC_CRYSTAL) {
        *temp_out   = s_crystal_temp;
        *output_out = s_crystal_output;
    } else {
        *temp_out   = s_laser_temp;
        *output_out = s_laser_output;
    }
}
