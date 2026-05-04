#include "laser_control.h"
#include "bsp_laser.h"

LaserState_t g_laser_state = {
    .current_A     = 0.0f,
    .max_current_A = 1.5f,  /* default software limit — change via CLI */
    .enabled       = 0,
};

void Laser_Control_Init(void)
{
    BSP_Laser_Init();
}

void Laser_Control_SetCurrent(float amps)
{
    if (amps < 0.0f) amps = 0.0f;
    if (amps > g_laser_state.max_current_A) amps = g_laser_state.max_current_A;
    g_laser_state.current_A = amps;
    if (g_laser_state.enabled)
        BSP_Laser_SetCurrent(amps);
}

void Laser_Control_SetMaxCurrent(float amps)
{
    if (amps < 0.0f) amps = 0.0f;
    g_laser_state.max_current_A = amps;
    if (g_laser_state.current_A > amps) {
        g_laser_state.current_A = amps;
        if (g_laser_state.enabled)
            BSP_Laser_SetCurrent(amps);
    }
}

void Laser_Control_Enable(void)
{
    g_laser_state.enabled = 1;
    BSP_Laser_SetCurrent(g_laser_state.current_A);
}

void Laser_Control_Disable(void)
{
    g_laser_state.enabled = 0;
    BSP_Laser_Disable();
}

const LaserState_t *Laser_Control_GetState(void)
{
    return &g_laser_state;
}
