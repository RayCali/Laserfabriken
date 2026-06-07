#include "bsp_tec.h"
#include "bsp_config.h"
#include "dac.h"
#include "stm32g4xx_hal.h"
#include <stdint.h>

/*
 * DRV8873S control — SPI Mode 1 (CPOL=0, CPHA=1).
 * All SPI peripherals on this bus (ADS1220, DRV8873S, DAC8562S) use Mode 1 —
 * no runtime mode switching needed.
 *
 * Direction: IN1/IN2 GPIOs.
 * Amplitude: STM32 internal DAC1 (ch1 = Crystal TEC, ch2 = Laser TEC).
 */

/* ── Per-channel hardware description ────────────────────────────────────── */

typedef struct {
    GPIO_TypeDef *cs_port;
    uint16_t      cs_pin;
    GPIO_TypeDef *in1_port;
    uint16_t      in1_pin;
    GPIO_TypeDef *in2_port;
    uint16_t      in2_pin;
    GPIO_TypeDef *nsleep_port;
    uint16_t      nsleep_pin;
} DRV_HW_t;

static const DRV_HW_t s_drv[2] = {
    [TEC_CRYSTAL] = {
        .cs_port     = BSP_DRV1_CS_PORT,    .cs_pin     = BSP_DRV1_CS_PIN,
        .in1_port    = BSP_DRV1_IN1_PORT,   .in1_pin    = BSP_DRV1_IN1_PIN,
        .in2_port    = BSP_DRV1_IN2_PORT,   .in2_pin    = BSP_DRV1_IN2_PIN,
        .nsleep_port = BSP_DRV1_NSLEEP_PORT,.nsleep_pin = BSP_DRV1_NSLEEP_PIN,
    },
    [TEC_LASER] = {
        .cs_port     = BSP_DRV2_CS_PORT,    .cs_pin     = BSP_DRV2_CS_PIN,
        .in1_port    = BSP_DRV2_IN1_PORT,   .in1_pin    = BSP_DRV2_IN1_PIN,
        .in2_port    = BSP_DRV2_IN2_PORT,   .in2_pin    = BSP_DRV2_IN2_PIN,
        .nsleep_port = BSP_DRV2_NSLEEP_PORT,.nsleep_pin = BSP_DRV2_NSLEEP_PIN,
    },
};

/* ── DRV8873S SPI register write (Mode 1, 16-bit frame) ─────────────────── */

static void drv_write_reg(TecChannel_t ch, uint8_t reg, uint8_t val)
{
    /* DRV8873S SPI frame: [15]=0, [14]=RW(0=write), [13:9]=address, [8]=X, [7:0]=data */
    uint8_t tx[2] = {
        (uint8_t)((reg & 0x7Fu) << 1),
        val
    };
    HAL_GPIO_WritePin(s_drv[ch].cs_port, s_drv[ch].cs_pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(BSP_SPI_BUS, tx, 2, 10);
    HAL_GPIO_WritePin(s_drv[ch].cs_port, s_drv[ch].cs_pin, GPIO_PIN_SET);
}

/* ── STM32 internal DAC (12-bit, ch1 = Crystal TEC, ch2 = Laser TEC) ─────── */
static void internal_dac_set(TecChannel_t ch, uint16_t code_12bit)
{
    uint32_t dac_ch = (ch == TEC_CRYSTAL) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    HAL_DAC_SetValue(BSP_DAC, dac_ch, DAC_ALIGN_12B_R, code_12bit);
    HAL_DAC_Start(BSP_DAC, dac_ch);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void BSP_TEC_Init(TecChannel_t ch)
{
    /* Start with both inputs low (coast/brake) before waking the driver */
    HAL_GPIO_WritePin(s_drv[ch].in1_port, s_drv[ch].in1_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(s_drv[ch].in2_port, s_drv[ch].in2_pin, GPIO_PIN_RESET);

    /* Assert nSLEEP to exit sleep mode (t_sleep ≥ 1 ms per datasheet) */
    HAL_GPIO_WritePin(s_drv[ch].nsleep_port, s_drv[ch].nsleep_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* IC1_CTRL (0x02): CLR_FLT=1 clears any latched fault at startup.
     * All other bits left at power-on defaults (OCP latch, 0.6 µs deglitch).
     * Verify register map against DRV8873S datasheet if behaviour is unexpected. */
    drv_write_reg(ch, 0x04, 0x80); /* IC3_CTRL: CLR_FLT=1 (bit 7), LOCK default=100b (unlocked) */

    /* Set DAC to 0 output initially */
    internal_dac_set(ch, 0);
}

void BSP_TEC_SetOutput(TecChannel_t ch, float value)
{
    if (value >  1.0f) value =  1.0f;
    if (value < -1.0f) value = -1.0f;

    /* DRV8873S truth table (IN1/IN2 → H-bridge state):
     *  IN1=H, IN2=L → forward current (heat or cool depending on TEC polarity)
     *  IN1=L, IN2=H → reverse current
     *  IN1=L, IN2=L → coast (free-wheel)
     *  IN1=H, IN2=H → brake (both low-side FETs active)
     */
    if (value >= 0.0f) {
        HAL_GPIO_WritePin(s_drv[ch].in1_port, s_drv[ch].in1_pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(s_drv[ch].in2_port, s_drv[ch].in2_pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(s_drv[ch].in1_port, s_drv[ch].in1_pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_drv[ch].in2_port, s_drv[ch].in2_pin, GPIO_PIN_SET);
        value = -value; /* magnitude for DAC */
    }

    /* Map [0.0, 1.0] → [0, 4095] for 12-bit internal DAC */
    uint16_t dac_code = (uint16_t)(value * 4095.0f);
    internal_dac_set(ch, dac_code);
}

void BSP_TEC_Disable(TecChannel_t ch)
{
    /* Coast the H-bridge first, then pull nSLEEP low */
    HAL_GPIO_WritePin(s_drv[ch].in1_port, s_drv[ch].in1_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(s_drv[ch].in2_port, s_drv[ch].in2_pin, GPIO_PIN_RESET);
    internal_dac_set(ch, 0);
    HAL_GPIO_WritePin(s_drv[ch].nsleep_port, s_drv[ch].nsleep_pin, GPIO_PIN_RESET);
}
