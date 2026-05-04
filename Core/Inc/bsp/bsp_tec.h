#ifndef BSP_TEC_H
#define BSP_TEC_H

typedef enum {
    TEC_CRYSTAL = 0,
    TEC_LASER   = 1,
} TecChannel_t;

/* Wake DRV8873S and configure via SPI. Call once per channel at startup. */
void BSP_TEC_Init(TecChannel_t ch);

/* Set TEC drive level.
 * value ∈ [-1.0, +1.0]: positive = forward current, negative = reverse.
 * Magnitude maps to DAC8562S output (0–full scale).
 * Direction is controlled via DRV8873S IN1/IN2 GPIOs (already functional).
 * DAC amplitude output requires SPI implementation (see bsp_tec.c TODO). */
void BSP_TEC_SetOutput(TecChannel_t ch, float value);

/* Coast the H-bridge and put DRV8873S to sleep. */
void BSP_TEC_Disable(TecChannel_t ch);

#endif /* BSP_TEC_H */
