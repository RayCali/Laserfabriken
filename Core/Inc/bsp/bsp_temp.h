#ifndef BSP_TEMP_H
#define BSP_TEMP_H

typedef enum {
    TEMP_CH_CRYSTAL = 0,
    TEMP_CH_LASER   = 1,
} TempChannel_t;

/* Configure ADS1220 for continuous conversion at lowest noise setting. */
void  BSP_Temp_Init(void);

/* Read temperature in °C for the given channel.
 * Returns 0.0f until ADS1220 SPI is implemented (see bsp_temp.c TODOs). */
float BSP_Temp_Read(TempChannel_t ch);

#endif /* BSP_TEMP_H */
