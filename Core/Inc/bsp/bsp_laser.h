#ifndef BSP_LASER_H
#define BSP_LASER_H

/*
 * BSP layer for laser diode current control via DAC8562SDGS.
 * DAC8562S channel A drives the laser current set-point.
 * SPI Mode 0 (CPOL=0, CPHA=0), 24-bit frames.
 *
 * Hardware max current is set by BSP_LASER_MAX_CURRENT_A in bsp_config.h.
 * Software limit is enforced by laser_control.c on top of this layer.
 */

void BSP_Laser_Init(void);
void BSP_Laser_SetCurrent(float amps);
void BSP_Laser_Disable(void);

#endif /* BSP_LASER_H */
