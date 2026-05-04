#ifndef LASER_CONTROL_H
#define LASER_CONTROL_H

#include <stdint.h>

typedef struct {
    float   current_A;       /* current set-point (amps) */
    float   max_current_A;   /* software current limit (amps) */
    uint8_t enabled;         /* 0 = laser off, 1 = laser on */
} LaserState_t;

extern LaserState_t g_laser_state;

void               Laser_Control_Init(void);
void               Laser_Control_SetCurrent(float amps);
void               Laser_Control_SetMaxCurrent(float amps);
void               Laser_Control_Enable(void);
void               Laser_Control_Disable(void);
const LaserState_t *Laser_Control_GetState(void);

#endif /* LASER_CONTROL_H */
