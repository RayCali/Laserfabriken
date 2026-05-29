#include "bsp_temp.h"
#include "bsp_config.h"
#include "stm32f4xx_hal.h"
#include <math.h>
#include <stdint.h>

/*
 * ADS1220 SPI interface — Mode 1 (CPOL=0, CPHA=1).
 * All other peripherals (DRV8873S, DAC8562S) use Mode 0.
 * spi_set_phase() switches the STM32 SPI clock phase around each ADS1220
 * transaction and restores Mode 0 afterward.
 *
 * G431CB migration option: put ADS1220 on a second SPI bus (hspi2 in
 * bsp_config.h) to eliminate the mode-switching entirely.
 */

/* ── SPI mode switching ───────────────────────────────────────────────────── */

static void spi_set_phase(uint32_t phase)
{
    /* HAL_SPI_Init only writes CR1/CR2, so this is fast. */
    BSP_SPI_BUS->Init.CLKPhase = phase;
    HAL_SPI_Init(BSP_SPI_BUS);
}

/* ── ADS1220 command byte definitions (datasheet Table 16) ──────────────── */

#define ADS1220_CMD_RESET   0x06u
#define ADS1220_CMD_START   0x08u
#define ADS1220_CMD_RDATA   0x10u
/* WREG: 0100 rrnn — rr=start register (0–3), nn=number of regs - 1 */
#define ADS1220_CMD_WREG(reg, n)  ((uint8_t)(0x40u | ((reg) << 2) | ((n) - 1u)))

/* Register 0 — input MUX and gain */
#define ADS1220_MUX_AIN0_AIN1  0x00u  /* Crystal NTC: AIN0 differential with AIN1 */
#define ADS1220_MUX_AIN2_AIN3  0x80u  /* Laser NTC:   AIN2 differential with AIN3 */
/* For single-ended: AIN0/AVSS = 0x60, AIN2/AVSS = 0xA0 — update when schematic confirmed */
#define ADS1220_GAIN_1         0x00u
#define ADS1220_PGA_BYPASS     0x01u  /* bypass PGA for low source impedance */

/* Register 1 — data rate and operating mode */
/* Bits [7:5] = DR, [4:3] = mode=normal(00), [2] = CM=continuous(1), [1:0] = TS/BCS */
#define ADS1220_REG1_CONTINUOUS 0x04u

/* Register 2 — voltage reference and filter */
#define ADS1220_VREF_INTERNAL   0x00u
#define ADS1220_FILTER_NONE     0x00u

/* ── Chip-select helpers ─────────────────────────────────────────────────── */

static void ads_cs_low(void)
{
    HAL_GPIO_WritePin(BSP_ADC_CS_PORT, BSP_ADC_CS_PIN, GPIO_PIN_RESET);
}

static void ads_cs_high(void)
{
    HAL_GPIO_WritePin(BSP_ADC_CS_PORT, BSP_ADC_CS_PIN, GPIO_PIN_SET);
}

/* ── Low-level SPI helpers (Mode 1, CS already asserted by caller) ────────── */

static void ads_write(uint8_t *buf, uint16_t len)
{
    /* TODO: ADS1220 SPI — replace stub with:
     *   HAL_SPI_Transmit(BSP_SPI_BUS, buf, len, 10);
     */
    (void)buf;
    (void)len;
}

static void ads_read(uint8_t *buf, uint16_t len)
{
    /* TODO: ADS1220 SPI — replace stub with:
     *   HAL_SPI_Receive(BSP_SPI_BUS, buf, len, 10);
     */
    (void)buf;
    (void)len;
    for (uint16_t i = 0; i < len; i++) buf[i] = 0;
}

/* ── ADS1220 configuration ───────────────────────────────────────────────── */

static void ads_send_cmd(uint8_t cmd)
{
    ads_cs_low();
    ads_write(&cmd, 1);
    ads_cs_high();
}

static void ads_write_regs(uint8_t start_reg, uint8_t *data, uint8_t n)
{
    uint8_t cmd = ADS1220_CMD_WREG(start_reg, n);
    ads_cs_low();
    ads_write(&cmd, 1);
    ads_write(data, n);
    ads_cs_high();
}

/* ── Raw ADC read ────────────────────────────────────────────────────────── */

static int32_t ads_read_raw(void)
{
    uint8_t raw[3] = {0, 0, 0};
    uint8_t cmd    = ADS1220_CMD_RDATA;

    /* TODO: ADS1220 SPI — wait for DRDY low before reading:
     *   while (HAL_GPIO_ReadPin(ADC_DRDY_GPIO_Port, ADC_DRDY_Pin) == GPIO_PIN_SET);
     * DRDY goes low when a new conversion result is ready.
     */

    ads_cs_low();
    ads_write(&cmd, 1);
    ads_read(raw, 3);
    ads_cs_high();

    /* Reconstruct signed 24-bit value (MSB first), sign-extend to 32 bits */
    int32_t val = ((int32_t)raw[0] << 16) | ((int32_t)raw[1] << 8) | raw[2];
    if (val & 0x800000) val |= (int32_t)0xFF000000; /* sign extension */

    return val;
}

/* ── Signal chain: raw → resistance → °C ────────────────────────────────── */

static float raw_to_resistance(int32_t raw)
{
    /* ADS1220 full scale: ±(2^23 - 1) counts = ±VREF (gain=1, internal ref) */
    float voltage = BSP_NTC_VREF_V * ((float)raw / (float)0x7FFFFF);
    if (voltage <= 0.0f || voltage >= BSP_NTC_VREF_V) return 1e9f; /* guard */

    /* Voltage divider: V = Vref * R_ntc / (R_series + R_ntc)
     * => R_ntc = R_series * V / (Vref - V)                        */
    return BSP_NTC_RSERIES_OHM * voltage / (BSP_NTC_VREF_V - voltage);
}

static float resistance_to_celsius(float r_ohm)
{
    /* Steinhart-Hart equation: 1/T = A + B*ln(R) + C*(ln(R))^3 */
    float ln_r = logf(r_ohm);
    float inv_T = BSP_NTC_SH_A
                + BSP_NTC_SH_B * ln_r
                + BSP_NTC_SH_C * ln_r * ln_r * ln_r;
    return (1.0f / inv_T) - 273.15f;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void BSP_Temp_Init(void)
{
    spi_set_phase(SPI_PHASE_2EDGE); /* ADS1220 requires Mode 1 */

    /* Reset ADS1220 to power-on defaults */
    ads_send_cmd(ADS1220_CMD_RESET);
    /* TODO: ADS1220 SPI — add HAL_Delay(1) after reset (t_por = 0.6 ms typ.) */

    /* Write all 3 config registers in one WREG transaction:
     * Reg0: MUX=AIN0/AIN1 (crystal), Gain=1, PGA bypass
     * Reg1: DR=20 SPS, normal mode, continuous conversion
     * Reg2: Internal VREF (2.048 V)
     */
    uint8_t cfg[3] = {
        ADS1220_MUX_AIN0_AIN1 | ADS1220_GAIN_1 | ADS1220_PGA_BYPASS,
        BSP_ADS1220_DATA_RATE | ADS1220_REG1_CONTINUOUS,
        ADS1220_VREF_INTERNAL | ADS1220_FILTER_NONE
    };
    ads_write_regs(0, cfg, 3);

    /* Start continuous conversions */
    ads_send_cmd(ADS1220_CMD_START);

    spi_set_phase(SPI_PHASE_1EDGE); /* Restore Mode 0 for other peripherals */
}

float BSP_Temp_Read(TempChannel_t ch)
{
    spi_set_phase(SPI_PHASE_2EDGE); /* ADS1220 Mode 1 */

    /* TODO: ADS1220 SPI — multiplex channel by rewriting Reg0 MUX field:
     *   TEMP_CH_CRYSTAL: ADS1220_MUX_AIN0_AIN1
     *   TEMP_CH_LASER:   ADS1220_MUX_AIN2_AIN3
     * Then send START to re-trigger, wait for DRDY, then RDATA.
     * For now the channel argument is unused — both reads return 0.0f.
     */
    (void)ch;

    int32_t raw = ads_read_raw(); /* returns 0 until SPI is implemented */

    spi_set_phase(SPI_PHASE_1EDGE); /* Restore Mode 0 */

    if (raw == 0) return 0.0f; /* stub sentinel — SPI not yet wired */

    float r = raw_to_resistance(raw);
    return resistance_to_celsius(r);
}
