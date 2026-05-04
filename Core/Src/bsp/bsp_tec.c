#include "bsp_tec.h"
#include "bsp_config.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

/*
 * DRV8873S control — SPI Mode 0 (CPOL=0, CPHA=0), same as the default SPI
 * configuration. No mode switching needed (unlike ADS1220).
 *
 * Direction is controlled via IN1/IN2 GPIOs — fully functional now.
 * Amplitude is controlled via DAC8562S (16-bit SPI DAC) — stub until SPI
 * transactions are wired up (see TODO comments below).
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

/* ── DRV8873S SPI register write (Mode 0, 16-bit frame) ─────────────────── */

static void drv_write_reg(TecChannel_t ch, uint8_t reg, uint8_t val)
{
    /* DRV8873 SPI frame: [15] RW=0 (write), [14:8] address, [7:0] data */
    uint8_t tx[2] = {
        (uint8_t)((reg & 0x7Fu) << 1), /* address in upper byte, RW=0 */
        val
    };
    HAL_GPIO_WritePin(s_drv[ch].cs_port, s_drv[ch].cs_pin, GPIO_PIN_RESET);
    /* TODO: DRV8873S SPI — uncomment when SPI is wired:
     *   HAL_SPI_Transmit(BSP_SPI_BUS, tx, 2, 10);
     */
    (void)tx;
    HAL_GPIO_WritePin(s_drv[ch].cs_port, s_drv[ch].cs_pin, GPIO_PIN_SET);
}

/* ── DAC8562S write (Mode 0, 24-bit frame) ───────────────────────────────── */
/*
 * DAC8562S command byte: [23:19] CMD, [18:16] DAC select, [15:0] data (16-bit)
 * CMD = 011 → write and update DAC register
 * DAC select: 000 = DAC A (channel 0 / crystal), 001 = DAC B (laser)
 */
static void dac_set(TecChannel_t ch, uint16_t code)
{
    uint8_t tx[3] = {
        (uint8_t)(0x18u | (uint8_t)ch), /* 0x18=write+update DAC A, 0x19=DAC B */
        (uint8_t)(code >> 8),
        (uint8_t)(code & 0xFFu)
    };
    HAL_GPIO_WritePin(BSP_DAC_CS_PORT, BSP_DAC_CS_PIN, GPIO_PIN_RESET);
    /* TODO: DAC8562S SPI — uncomment when SPI is wired:
     *   HAL_SPI_Transmit(BSP_SPI_BUS, tx, 3, 10);
     */
    (void)tx;
    HAL_GPIO_WritePin(BSP_DAC_CS_PORT, BSP_DAC_CS_PIN, GPIO_PIN_SET);
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

    /* TODO: DRV8873S SPI — configure IC1_CTRL register:
     *   - PWM input mode (EN/IN1/IN2 control)
     *   - Clear any latched faults (CLR_FLT bit)
     * TODO: DRV8873S SPI — configure IC2_CTRL register:
     *   - Set OCP deglitch time
     *   - Set VM undervoltage lockout threshold
     */
    (void)drv_write_reg; /* suppress unused-function warning until TODOs filled */

    /* Set DAC to 0 output initially */
    dac_set(ch, 0);
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

    /* Map [0.0, 1.0] → [0, 65535] DAC code */
    uint16_t dac_code = (uint16_t)(value * 65535.0f);
    dac_set(ch, dac_code);
}

void BSP_TEC_Disable(TecChannel_t ch)
{
    /* Coast the H-bridge first, then pull nSLEEP low */
    HAL_GPIO_WritePin(s_drv[ch].in1_port, s_drv[ch].in1_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(s_drv[ch].in2_port, s_drv[ch].in2_pin, GPIO_PIN_RESET);
    dac_set(ch, 0);
    HAL_GPIO_WritePin(s_drv[ch].nsleep_port, s_drv[ch].nsleep_pin, GPIO_PIN_RESET);
}
