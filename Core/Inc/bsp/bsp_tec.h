#ifndef BSP_TEC_H
#define BSP_TEC_H

#include <stdbool.h>

typedef enum {
    TEC_CRYSTAL = 0,
    TEC_LASER   = 1,
} TecChannel_t;

/* Enable the TEC buck converter and zero the output. Call once per channel at startup. */
void BSP_TEC_Init(TecChannel_t ch);

/* Set TEC drive level.
 * value ∈ [-1.0, +1.0]: sign selects direction via the POLARITY GPIO,
 * magnitude maps to the internal DAC amplitude output. */
void BSP_TEC_SetOutput(TecChannel_t ch, float value);

/* Zero the output and disable the buck converter. */
void BSP_TEC_Disable(TecChannel_t ch);

/* Read the buck converter's PG (power-good) pin.
 * Returns true when the converter reports healthy output, false on fault. */
bool BSP_TEC_PowerGood(void);

#endif /* BSP_TEC_H */
