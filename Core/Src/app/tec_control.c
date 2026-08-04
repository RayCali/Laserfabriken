#include "tec_control.h"
#include "laser_control.h"
#include "bsp_temp.h"

PID_t g_crystal_pid;
PID_t g_laser_pid;

float g_crystal_wl_k = 0.1929f;   /* nm/°C — from characterisation graph */
float g_crystal_wl_m = 1548.6f;   /* nm   — from characterisation graph */

/* Laser temperature protection state */
float   g_laser_temp_max_dev_C        = 5.0f;
float   g_laser_temp_abs_max_C        = 60.0f;
float   g_laser_temp_timer_s          = 30.0f;
uint8_t g_laser_temp_guard_armed      = 0;
float   g_laser_temp_settle_elapsed_s = 0.0f;
uint8_t g_laser_temp_trip_reason      = LASER_TEMP_TRIP_NONE;
uint8_t g_laser_temp_trip_pending     = 0;

/* Simulation overrides */
uint8_t g_sim_laser_temp_enable = 0;
float   g_sim_laser_temp_C      = 0.0f;

/* TEC buck power-good fault state */
uint8_t g_tec_pg_fault         = 0;
uint8_t g_tec_pg_fault_pending = 0;

/* Manual DAC bench-test mode — see tec_control.h for the full contract. */
uint8_t g_tec_manual_test      = 0;

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
    s_laser_temp   = g_sim_laser_temp_enable ? g_sim_laser_temp_C
                                             : BSP_Temp_Read(TEMP_CH_LASER);

    s_crystal_output = PID_Update(&g_crystal_pid, s_crystal_temp, PID_PERIOD_S);
    s_laser_output   = PID_Update(&g_laser_pid,   s_laser_temp,   PID_PERIOD_S);

    /* TEC buck power-good check — hold output disabled while the buck reports a fault.
     * Skipped entirely during a manual DAC bench sweep (g_tec_manual_test):
     * the CLI owns the crystal DAC output in that mode, and either branch
     * here (BSP_TEC_Disable or BSP_TEC_SetOutput) would stomp on whatever
     * raw code the sweep just set, on the very next 100 ms tick. */
    if (!g_tec_manual_test) {
        if (!BSP_TEC_PowerGood()) {
            if (!g_tec_pg_fault)
                g_tec_pg_fault_pending = 1;
            g_tec_pg_fault = 1;
            BSP_TEC_Disable(TEC_CRYSTAL);
        } else {
            g_tec_pg_fault = 0;
            BSP_TEC_SetOutput(TEC_CRYSTAL, s_crystal_output);
        }
    } else {
        g_tec_pg_fault = !BSP_TEC_PowerGood(); /* status display only, no action taken */
    }
    BSP_TEC_SetOutput(TEC_LASER, s_laser_output);

    /* Laser temperature protection — only runs when laser is enabled */
    if (Laser_Control_GetState()->enabled) {
        /* Absolute max: hard cutoff regardless of guard state */
        if (s_laser_temp > g_laser_temp_abs_max_C) {
            Laser_Control_Disable();
            g_laser_temp_trip_reason      = LASER_TEMP_TRIP_ABS_MAX;
            g_laser_temp_trip_pending     = 1;
            g_laser_temp_guard_armed      = 0;
            g_laser_temp_settle_elapsed_s = 0.0f;
        } else if (!g_laser_temp_guard_armed) {
            /* Settling phase: accumulate time within deviation */
            float dev = s_laser_temp - g_laser_pid.setpoint;
            if (dev < 0.0f) dev = -dev;
            if (dev <= g_laser_temp_max_dev_C) {
                g_laser_temp_settle_elapsed_s += PID_PERIOD_S;
                if (g_laser_temp_settle_elapsed_s >= g_laser_temp_timer_s)
                    g_laser_temp_guard_armed = 1;
            } else {
                /* Left deviation window during settling — restart timer */
                g_laser_temp_settle_elapsed_s = 0.0f;
            }
        } else {
            /* Guard armed: any deviation beyond limit disables laser */
            float dev = s_laser_temp - g_laser_pid.setpoint;
            if (dev < 0.0f) dev = -dev;
            if (dev > g_laser_temp_max_dev_C) {
                Laser_Control_Disable();
                g_laser_temp_trip_reason      = LASER_TEMP_TRIP_DEV;
                g_laser_temp_trip_pending     = 1;
                g_laser_temp_guard_armed      = 0;
                g_laser_temp_settle_elapsed_s = 0.0f;
            }
        }
    }
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

void TEC_Crystal_SetWavelength(float nm)
{
    if (g_crystal_wl_k == 0.0f) return;
    g_crystal_pid.setpoint = (nm - g_crystal_wl_m) / g_crystal_wl_k;
    PID_Reset(&g_crystal_pid);
}

void TEC_LaserTempGuard_Reset(void)
{
    g_laser_temp_guard_armed      = 0;
    g_laser_temp_settle_elapsed_s = 0.0f;
    g_laser_temp_trip_reason      = LASER_TEMP_TRIP_NONE;
    g_laser_temp_trip_pending     = 0;
}
