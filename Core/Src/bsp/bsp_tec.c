#include "bsp_tec.h"
#include "bsp_config.h"
#include "dac.h"
#include "stm32g4xx_hal.h"
#include <stdint.h>

/*
 * TEC control — UGN board.
 *
 * Amplitude: STM32 internal DAC1_OUT2 (PA5 = Crystal TEC).
 *            DAC code 0 = no drive, 4095 = full drive.
 *            Buck converter (M5 TPS563252) inverts: VDAC high → VOUT low.
 *            So we invert the code: dac_code = 4095 - magnitude_code.
 *
 * Direction: POLARITY GPIO (PC6).
 *            GPIO_PIN_SET   = heat, GPIO_PIN_RESET = cool (verify on hardware).
 *
 * Enable:    TEC_EN GPIO (PC5) — drives M5.EN on the buck converter.
 */

/* ── Internal DAC helper ────────────────────────────────────────────────────── */

static void internal_dac_set(TecChannel_t ch, uint16_t code_12bit)
{
    uint32_t dac_ch = (ch == TEC_CRYSTAL) ? BSP_DAC_CH_CRYSTAL : BSP_DAC_CH_LASER;
    /* Buck converter output is inversely proportional to VDAC.
     * Invert so that higher |output| → more TEC current. */
    uint16_t inverted = 4095u - code_12bit;
    HAL_DAC_SetValue(BSP_DAC, dac_ch, DAC_ALIGN_12B_R, inverted);
    HAL_DAC_Start(BSP_DAC, dac_ch);
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

void BSP_TEC_Init(TecChannel_t ch)
{
    /* Only one physical TEC channel on UGN (Crystal).
     * Laser TEC channel is a no-op here — Spudkortet will implement it. */
    if (ch != TEC_CRYSTAL)
        return;

    /* Start with zero drive and forward polarity */
    HAL_GPIO_WritePin(BSP_TEC_POLARITY_PORT, BSP_TEC_POLARITY_PIN, GPIO_PIN_RESET);
    internal_dac_set(TEC_CRYSTAL, 0);

    /* Enable buck converter */
    HAL_GPIO_WritePin(BSP_TEC_EN_PORT, BSP_TEC_EN_PIN, GPIO_PIN_SET);
}

void BSP_TEC_SetOutput(TecChannel_t ch, float value)
{
    if (ch != TEC_CRYSTAL)
        return;

    if (value >  1.0f) value =  1.0f;
    if (value < -1.0f) value = -1.0f;

    /* Set direction via POLARITY pin */
    if (value >= 0.0f) {
        HAL_GPIO_WritePin(BSP_TEC_POLARITY_PORT, BSP_TEC_POLARITY_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(BSP_TEC_POLARITY_PORT, BSP_TEC_POLARITY_PIN, GPIO_PIN_RESET);
        value = -value;
    }

    /* Map magnitude [0.0, 1.0] → DAC code [0, 4095] */
    uint16_t dac_code = (uint16_t)(value * 4095.0f);
    internal_dac_set(TEC_CRYSTAL, dac_code);
}

void BSP_TEC_Disable(TecChannel_t ch)
{
    if (ch != TEC_CRYSTAL)
        return;

    internal_dac_set(TEC_CRYSTAL, 0);
    HAL_GPIO_WritePin(BSP_TEC_EN_PORT, BSP_TEC_EN_PIN, GPIO_PIN_RESET);
}

bool BSP_TEC_PowerGood(void)
{
    /* TPS563252 PG is open-drain: pulled high externally when output is good,
     * driven low by the converter on fault (UVLO, OVP, thermal, etc.). */
    return HAL_GPIO_ReadPin(BSP_TEC_PG_PORT, BSP_TEC_PG_PIN) == GPIO_PIN_SET;
}
