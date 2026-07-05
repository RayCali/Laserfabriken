#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

#include "usart.h"
#include "spi.h"
#include "dac.h"
#include "main.h"
#include "usbd_cdc_if.h"

/*
 * bsp_config.h — the ONLY file that changes when switching between boards.
 *
 * Current board: Laserfabriken UGN (single TEC channel, no laser diode)
 * Next board:    Spudkortet (adds laser diode via DAC8562S + second DRV8873S channel)
 *
 * Migration checklist for Spudkortet:
 *  1. Re-generate .ioc: add DRV1/DRV2 pins, DAC8562S CS, ADC_DRDY
 *  2. Restore BSP_DRV* macros below
 *  3. Restore BSP_LASER_CS_PORT/PIN
 *  4. Restore BSP_ADC_DRDY_PIN for EXTI-based DRDY in bsp_temp.c
 */

/* ── CLI terminal — USB CDC ───────────────────────────────────────────────────
 * CLI over USB CDC (PA11/PA12). UART4 (PC10/PC11) is for display.
 */
#define BSP_CLI_Send(buf, len)   CDC_Transmit_FS((uint8_t*)(buf), (uint16_t)(len))
#define BSP_CLI_Receive(pbyte)   CDC_CLI_Receive(pbyte)

/* ── Display UART ────────────────────────────────────────────────────────────
 * USART1 (PA9/PA10) — bidirectional bridge to ESP32 display.
 */
#define BSP_DISPLAY_Send(buf, len)   HAL_UART_Transmit(&huart1, (uint8_t*)(buf), (uint16_t)(len), 100)
#define BSP_DISPLAY_Receive(pbyte)   HAL_UART_Receive(&huart1, (pbyte), 1, 0)

/* ── Shared SPI bus ──────────────────────────────────────────────────────────
 * SPI3: PC10 (SCK), PC11 (MISO), PC12 (MOSI) — Mode 1 (CPOL=0, CPHA=1).
 * ADS1220 CS: PA4.  DAC8562S CS: PA3 (reserved for Spudkortet).
 */
#define BSP_SPI_BUS         (&hspi3)
#define BSP_ADC_CS_PORT     ADC_CS_GPIO_Port
#define BSP_ADC_CS_PIN      ADC_CS_Pin
#define BSP_LASER_CS_PORT   DAC_CS_GPIO_Port   /* reserved — DAC8562S on Spudkortet */
#define BSP_LASER_CS_PIN    DAC_CS_Pin

/* ── Internal DAC for TEC amplitude ─────────────────────────────────────────
 * DAC1_OUT2 (PA5) = Crystal/oven TEC amplitude
 * Laser TEC (Spudkortet): PA4 freed up on spud by moving ADC_CS → DAC1_OUT1
 */
#define BSP_DAC                 (&hdac1)
#define BSP_DAC_CH_CRYSTAL      DAC_CHANNEL_2
#define BSP_DAC_CH_LASER        DAC_CHANNEL_2  /* placeholder — spudkortet uses DAC1_CH1 (PA4) */

/* ── TEC enable and polarity (UGN — TPS563252 programmable buck) ─────────────
 * TEC_EN (PC5): enable the TEC buck converter.
 * POLARITY (PC6): H = heat, L = cool (verify empirically on hardware).
 * TEC_PG (PA2): power-good input from buck (open-drain, pulled up in gpio.c).
 */
#define BSP_TEC_EN_PORT         TEC_EN_GPIO_Port
#define BSP_TEC_EN_PIN          TEC_EN_Pin
#define BSP_TEC_POLARITY_PORT   POLARITY_GPIO_Port
#define BSP_TEC_POLARITY_PIN    POLARITY_Pin
#define BSP_TEC_PG_PORT         TEC_PG_GPIO_Port
#define BSP_TEC_PG_PIN          TEC_PG_Pin

/* ── NTC thermistor: Steinhart-Hart coefficients ─────────────────────────────
 * Replace with values from your NTC datasheet or calibration.
 * Example below is for a generic 100 kΩ NTC at 25°C (B=3950 K).
 */
#define BSP_NTC_SH_A    8.757e-4f
#define BSP_NTC_SH_B    2.535e-4f
#define BSP_NTC_SH_C    1.838e-7f

/* ── NTC voltage divider ─────────────────────────────────────────────────────
 * ADS1220 external reference = 2.9 V (REFP0 connected to V2.9 rail).
 * R_series = resistor in series with NTC.
 */
#define BSP_NTC_VREF_V       2.9f
#define BSP_NTC_RSERIES_OHM  10000.0f

/* ── Laser diode hardware current limit ──────────────────────────────────────
 * Used by bsp_laser.c (Spudkortet). Not active on UGN.
 */
#define BSP_LASER_MAX_CURRENT_A  2.0f

/* ── ADS1220 data rate ───────────────────────────────────────────────────────
 * 0x00 → 20 SPS (50 ms per conversion).
 * Main loop ticks every 100 ms — data is always ready, no DRDY needed.
 */
#define BSP_ADS1220_DR_20SPS   0x00u
#define BSP_ADS1220_DR_90SPS   0x40u
#define BSP_ADS1220_DATA_RATE  BSP_ADS1220_DR_20SPS

#endif /* BSP_CONFIG_H */
