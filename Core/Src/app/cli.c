#include "cli.h"
#include "bsp_config.h"
#include "tec_control.h"
#include "laser_control.h"
#include "params.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#define CLI_BUF_LEN  80

static char     s_line_buf[CLI_BUF_LEN];
static uint16_t s_line_pos;

static char     s_disp_buf[CLI_BUF_LEN];
static uint16_t s_disp_pos;

/* Diagnostic only: counts every byte that arrives on the display UART
 * (valid or garbage) and remembers the last one, so 'status' can answer
 * "is anything physically arriving at all?" independent of whether it
 * parses as a command -- splits a wiring/connector fault from a firmware
 * parsing fault instead of guessing. */
static uint32_t s_disp_rx_count;
static uint8_t  s_disp_rx_last;

/* ── Interrupt-driven display UART RX ────────────────────────────────────
 * USART1's RX buffer is only 1 byte deep (FIFO disabled, usart.c). Polling
 * it from CLI_Process() (HAL_UART_Receive, Timeout=0) silently drops any
 * byte that arrives while the main loop is briefly elsewhere -- confirmed
 * on the bench: multi-line bursts from the display consistently lost
 * bytes right at the seam between lines, matching gaps caused by
 * TEC_Control_Tick()'s SPI transactions. HAL_UART_RxCpltCallback() grabs
 * each byte into this ring buffer the instant it arrives via interrupt,
 * independent of what the main loop is doing, which removes that window
 * structurally instead of just narrowing it. Requires USART1's NVIC
 * interrupt enabled in CubeMX (Pinout & Configuration -> USART1 -> NVIC
 * Settings -> USART1 global interrupt). */
#define DISP_RX_RING_SIZE 64
static volatile uint8_t  s_disp_rx_ring[DISP_RX_RING_SIZE];
static volatile uint16_t s_disp_rx_head;
static volatile uint16_t s_disp_rx_tail;
static uint8_t            s_disp_rx_isr_byte;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        uint16_t next = (uint16_t)((s_disp_rx_head + 1u) % DISP_RX_RING_SIZE);
        if (next != s_disp_rx_tail) {
            s_disp_rx_ring[s_disp_rx_head] = s_disp_rx_isr_byte;
            s_disp_rx_head = next;
        } /* ring full (CLI_Process() not keeping up at all) -- drop rather
             than overwrite unread data */
        HAL_UART_Receive_IT(&huart1, &s_disp_rx_isr_byte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        __HAL_UART_CLEAR_NEFLAG(&huart1);
        __HAL_UART_CLEAR_FEFLAG(&huart1);
        __HAL_UART_CLEAR_PEFLAG(&huart1);
        /* HAL aborts the pending receive on error -- re-arm it or RX stops
         * permanently, same failure mode the old polling code hit. */
        HAL_UART_Receive_IT(&huart1, &s_disp_rx_isr_byte, 1);
    }
}

static bool disp_rx_pop(uint8_t *out)
{
    if (s_disp_rx_head == s_disp_rx_tail)
        return false;
    *out = s_disp_rx_ring[s_disp_rx_tail];
    s_disp_rx_tail = (uint16_t)((s_disp_rx_tail + 1u) % DISP_RX_RING_SIZE);
    return true;
}

/* ── Output helpers ──────────────────────────────────────────────────────── */

static void cli_send(const char *str)
{
    uint16_t len = (uint16_t)strlen(str);
    /* CDC_Transmit_FS drops the data and returns USBD_BUSY instead of
     * queuing/blocking if the previous USB packet is still in flight.
     * status prints several lines back-to-back, so without this retry
     * later lines get silently dropped or interleaved with the still-
     * transmitting buffer, corrupting the output.
     * Bounded by a timeout (not a bare spin) because CLI_Process() runs
     * in the same main-loop iteration as TEC_Control_Tick() — an
     * unbounded wait here (e.g. USB unplugged mid-transmit) would stall
     * the PG-fault/laser-guard safety checks too. */
    uint32_t start = HAL_GetTick();
    while (BSP_CLI_Send((void *)str, len) == USBD_BUSY) {
        if ((HAL_GetTick() - start) > 20u)
            break;
    }
}

static void cli_sendf(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cli_send(buf);
}

static void disp_forward(const char *line)
{
    BSP_DISPLAY_Send(line, strlen(line));
    const char crlf[] = "\r\n";
    BSP_DISPLAY_Send(crlf, 2);
}

/* ── Command processing ──────────────────────────────────────────────────── */

static void process_line(char *line)
{
    /* Strip trailing whitespace */
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
        line[--len] = '\0';

    if (len == 0) return;

    /* help */
    if (strcmp(line, "help") == 0) {
        cli_send("Commands:\r\n"
                 "  set crystal.kp         <val>\r\n"
                 "  set crystal.ki         <val>\r\n"
                 "  set crystal.kd         <val>\r\n"
                 "  set crystal.setpoint   <val>  (C)\r\n"
                 "  set crystal.wavelength <val>  (nm) — sets crystal setpoint via T=(λ-m)/k\r\n"
                 "  set crystal.k          <val>  (nm/C, slope)\r\n"
                 "  set crystal.m          <val>  (nm, offset)\r\n"
                 "  set laser.kp           <val>\r\n"
                 "  set laser.ki           <val>\r\n"
                 "  set laser.kd           <val>\r\n"
                 "  set laser.setpoint     <val>  (C)  — resets temp guard\r\n"
                 "  set laser.current      <val>  (A)\r\n"
                 "  set laser.maxcurrent   <val>  (A)\r\n"
                 "  set laser.threshold    <val>  (A)\r\n"
                 "  set laser.power        <val>  (0-100 %)\r\n"
                 "  set laser.max_temp_deviation <val>  (C)\r\n"
                 "  set laser.absolute_max_temp  <val>  (C)\r\n"
                 "  set laser.temp_timer         <val>  (s)\r\n"
                 "  set sim.laser_temp   <val>  (C) — inject fake laser temp\r\n"
                 "  sim off              — disable sim overrides\r\n"
                 "  laser on\r\n"
                 "  laser off\r\n"
                 "  5v on\r\n"
                 "  5v off\r\n"
                 "  crystal.tec on\r\n"
                 "  crystal.tec off\r\n"
                 "  dac test on           — pause crystal TEC PID, force raw DAC=4095 (safe)\r\n"
                 "  dac set   <code>      — 0-4095, raw DAC write (requires 'dac test on')\r\n"
                 "  polarity heat         — POLARITY pin HIGH (requires 'dac test on')\r\n"
                 "  polarity cool         — POLARITY pin LOW  (requires 'dac test on')\r\n"
                 "  dac test off          — force raw DAC=4095, resume PID control\r\n"
                 "  status\r\n"
                 "  save\r\n"
                 "  help\r\n");
        return;
    }

    /* status */
    if (strcmp(line, "status") == 0) {
        float temp, output;

        TEC_Control_GetState(TEC_CRYSTAL, &temp, &output);
        float wl = g_crystal_wl_k * g_crystal_pid.setpoint + g_crystal_wl_m;
        cli_sendf("Crystal TEC: T=%7.4f C  SP=%7.4f C  WL=%7.2f nm  k=%.4f nm/C ",
                  (double)temp,
                  (double)g_crystal_pid.setpoint,
                  (double)wl,
                  (double)g_crystal_wl_k);

        cli_sendf("m=%.2f nm  out=%.4f  Kp=%.4f  Ki=%.6f  Kd=%.4f\r\n",
                  (double)g_crystal_wl_m,
                  (double)output,
                  (double)g_crystal_pid.Kp,
                  (double)g_crystal_pid.Ki,
                  (double)g_crystal_pid.Kd);

        if (g_tec_pg_fault)
            cli_send("  TEC buck: FAULT — power-good low, output disabled\r\n");

        TEC_Control_GetState(TEC_LASER, &temp, &output);
        cli_sendf("Laser  TEC: T=%7.4f C  SP=%7.4f  out=%.4f"
                  "  Kp=%.4f  Ki=%.6f  Kd=%.4f\r\n",
                  (double)temp,
                  (double)g_laser_pid.setpoint,
                  (double)output,
                  (double)g_laser_pid.Kp,
                  (double)g_laser_pid.Ki,
                  (double)g_laser_pid.Kd);
        if (g_laser_temp_trip_reason == LASER_TEMP_TRIP_ABS_MAX)
            cli_send("  Temp guard: TRIPPED — absolute max exceeded\r\n");
        else if (g_laser_temp_trip_reason == LASER_TEMP_TRIP_DEV)
            cli_send("  Temp guard: TRIPPED — deviation exceeded after settling\r\n");
        else if (g_laser_temp_guard_armed)
            cli_sendf("  Temp guard: ARMED  (max_dev=%.2f C  abs_max=%.2f C)\r\n",
                      (double)g_laser_temp_max_dev_C, (double)g_laser_temp_abs_max_C);
        else
            cli_sendf("  Temp guard: settling %.1f/%.1f s  (max_dev=%.2f C  abs_max=%.2f C)\r\n",
                      (double)g_laser_temp_settle_elapsed_s,
                      (double)g_laser_temp_timer_s,
                      (double)g_laser_temp_max_dev_C,
                      (double)g_laser_temp_abs_max_C);

        const LaserState_t *ls = Laser_Control_GetState();
        cli_sendf("Laser diode: Pwr=%3u%%  I=%.4f A  Ith=%.4f A  Imax=%.4f A  %s\r\n",
                  (unsigned)ls->power_pct,
                  (double)ls->current_A,
                  (double)ls->threshold_A,
                  (double)ls->max_current_A,
                  ls->enabled ? "ON" : "OFF");

        // cli_sendf("5V output: %s\r\n",
        //     HAL_GPIO_ReadPin(M6_EN_GPIO_Port, M6_EN_Pin) == GPIO_PIN_SET ? "ON" : "OFF");
        cli_sendf("TEC output: %s\r\n",
                  HAL_GPIO_ReadPin(BSP_TEC_EN_PORT, BSP_TEC_EN_PIN) == GPIO_PIN_SET ? "ON" : "OFF");

        cli_sendf("Display UART: %lu bytes received, last=0x%02X\r\n",
                  (unsigned long)s_disp_rx_count, s_disp_rx_last);
        return;
    }

    /* laser on / laser off */
    if (strcmp(line, "laser on") == 0) {
        Laser_Control_Enable();
        cli_send("OK laser enabled\r\n");
        return;
    }
    if (strcmp(line, "laser off") == 0) {
        Laser_Control_Disable();
        cli_send("OK laser disabled\r\n");
        return;
    }

    /* crystal.tec on / crystal.tec off */
    if (strcmp(line, "crystal.tec on") == 0) {
        g_crystal_pid.enabled = 1;
        cli_send("OK crystal TEC enabled\r\n");
        return;
    }
    if (strcmp(line, "crystal.tec off") == 0) {
        
        g_crystal_pid.enabled = 0;
        cli_send("OK crystal TEC disabled\r\n");
        return;
    }
    if (strcmp(line, "laser off") == 0) {
        Laser_Control_Disable();
        cli_send("OK laser disabled\r\n");
        return;
    }

    /* 5v on / 5v off */
    if (strcmp(line, "5v on") == 0) {
        HAL_GPIO_WritePin(M6_EN_GPIO_Port, M6_EN_Pin, GPIO_PIN_SET);
        cli_send("OK 5V output enabled\r\n");
        return;
    }
    if (strcmp(line, "5v off") == 0) {
        HAL_GPIO_WritePin(M6_EN_GPIO_Port, M6_EN_Pin, GPIO_PIN_RESET);
        cli_send("OK 5V output disabled\r\n");
        return;
    }

    /* dac test on / dac test off / dac set <code> / polarity heat / polarity cool */
    if (strcmp(line, "dac test on") == 0) {
        g_tec_manual_test = 1;
        BSP_TEC_SetRawDAC(4095u);
        BSP_TEC_SetRawPolarity(false);
        BSP_TEC_Enable(TEC_CRYSTAL, 1);
        cli_send("OK manual DAC test mode ON — crystal TEC PID paused, raw DAC forced to 4095"
                 " (safe/inert), polarity forced to cool. Use 'dac set <code>' to sweep DOWN"
                 " from there, checking FB and PG after every step. Do not jump toward 0."
                 " Use 'polarity heat'/'polarity cool' to change direction.\r\n");
        return;
    }
    if (strcmp(line, "dac test off") == 0) {
        Laser_Control_Disable(); // TODO: Disable TEC 
        BSP_TEC_Enable(TEC_CRYSTAL, 0);
        BSP_TEC_SetRawDAC(4095u);
        BSP_TEC_SetRawPolarity(false);
        g_tec_manual_test = 0;
        cli_send("OK raw DAC forced to 4095, polarity forced to cool, manual test mode OFF"
                 " — PID control resumed\r\n");
        return;
    }
    if (strcmp(line, "polarity heat") == 0) {
        if (!g_tec_manual_test) {
            cli_send("ERR: run 'dac test on' first\r\n");
            return;
        }
        BSP_TEC_SetRawPolarity(true);
        cli_send("OK polarity = heat (POLARITY pin HIGH)\r\n");
        return;
    }
    if (strcmp(line, "polarity cool") == 0) {
        if (!g_tec_manual_test) {
            cli_send("ERR: run 'dac test on' first\r\n");
            return;
        }
        BSP_TEC_SetRawPolarity(false);
        cli_send("OK polarity = cool (POLARITY pin LOW)\r\n");
        return;
    }
    {
        unsigned dac_code;
        if (sscanf(line, "dac set %u", &dac_code) == 1) {
            if (!g_tec_manual_test) {
                cli_send("ERR: run 'dac test on' first\r\n");
                return;
            }
            if (dac_code > 4095u) {
                cli_send("ERR: code must be 0-4095\r\n");
                return;
            }
            BSP_TEC_SetRawDAC((uint16_t)dac_code);
            cli_sendf("OK raw DAC = %u  -- measure FB now.  PG=%s\r\n",
                      dac_code, BSP_TEC_PowerGood() ? "GOOD" : "LOW/FAULT");
            return;
        }
    }

    /* sim off */
    if (strcmp(line, "sim off") == 0) {
        g_sim_laser_temp_enable = 0;
        cli_send("OK sim disabled\r\n");
        return;
    }

    /* save */
    if (strcmp(line, "save") == 0) {
        int r = Params_Save();
        cli_send(r == 0 ? "OK saved\r\n" : "ERR: flash write failed\r\n");
        return;
    }

    /* set <param> <value> */
    char param[32], value_str[16];
    if (sscanf(line, "set %31s %15s", param, value_str) == 2) {
        float val = strtof(value_str, NULL);

        /* Crystal wavelength model (checked before TEC PID to avoid param_name collision) */
        if (strcmp(param, "crystal.wavelength") == 0) {
            TEC_Crystal_SetWavelength(val);
            cli_sendf("OK crystal.wavelength = %.2f nm  -> SP=%.4f C\r\n",
                      (double)val, (double)g_crystal_pid.setpoint);
            Params_MarkDirty();
            return;
        }
        if (strcmp(param, "crystal.k") == 0) {
            float wl = g_crystal_wl_k * g_crystal_pid.setpoint + g_crystal_wl_m;
            g_crystal_wl_k = val;
            TEC_Crystal_SetWavelength(wl);
            cli_sendf("OK crystal.k = %.4f nm/C  -> SP=%.4f C\r\n",
                      (double)val, (double)g_crystal_pid.setpoint);
            return;
        }
        if (strcmp(param, "crystal.m") == 0) {
            float wl = g_crystal_wl_k * g_crystal_pid.setpoint + g_crystal_wl_m;
            g_crystal_wl_m = val;
            TEC_Crystal_SetWavelength(wl);
            cli_sendf("OK crystal.m = %.2f nm  -> SP=%.4f C\r\n",
                      (double)val, (double)g_crystal_pid.setpoint);
            return;
        }

        /* Laser diode current control (checked before TEC PID to avoid ambiguity) */
        if (strcmp(param, "laser.current") == 0) {
            Laser_Control_SetCurrent(val);
            const LaserState_t *ls = Laser_Control_GetState();
            if (val > 0.0f && ls->current_A == 0.0f)
                cli_sendf("WARN: %.4f A is below threshold (%.4f A) — current set to 0\r\n",
                          (double)val, (double)ls->threshold_A);
            else
                cli_sendf("OK laser.current = %.4f A\r\n", (double)ls->current_A);
            return;
        }
        if (strcmp(param, "laser.maxcurrent") == 0) {
            Laser_Control_SetMaxCurrent(val);
            cli_sendf("OK laser.maxcurrent = %.4f A\r\n", (double)val);
            return;
        }
        if (strcmp(param, "laser.threshold") == 0) {
            float prev_current = g_laser_state.current_A;
            Laser_Control_SetThreshold(val);
            const LaserState_t *ls = Laser_Control_GetState();
            if (prev_current > 0.0f && ls->current_A == 0.0f)
                cli_sendf("OK laser.threshold = %.4f A  (current was below threshold — set to 0)\r\n",
                          (double)val);
            else
                cli_sendf("OK laser.threshold = %.4f A  (power = %u%%)\r\n",
                          (double)val, (unsigned)ls->power_pct);
            return;
        }
        if (strcmp(param, "laser.power") == 0) {
            uint8_t pct = (val < 0.0f) ? 0u : (val > 100.0f) ? 100u : (uint8_t)val;
            Laser_Control_SetPower(pct);
            cli_sendf("OK laser.power = %u%%  -> I=%.4f A\r\n",
                      (unsigned)pct, (double)Laser_Control_GetState()->current_A);
            Params_MarkDirty();
            return;
        }
        if (strcmp(param, "laser.max_temp_deviation") == 0) {
            g_laser_temp_max_dev_C = val;
            cli_sendf("OK laser.max_temp_deviation = %.2f C\r\n", (double)val);
            return;
        }
        if (strcmp(param, "laser.absolute_max_temp") == 0) {
            g_laser_temp_abs_max_C = val;
            cli_sendf("OK laser.absolute_max_temp = %.2f C\r\n", (double)val);
            return;
        }
        if (strcmp(param, "laser.temp_timer") == 0) {
            g_laser_temp_timer_s = val;
            cli_sendf("OK laser.temp_timer = %.1f s\r\n", (double)val);
            return;
        }
        if (strcmp(param, "sim.laser_temp") == 0) {
            g_sim_laser_temp_C      = val;
            g_sim_laser_temp_enable = 1;
            cli_sendf("OK sim.laser_temp = %.4f C  (sim active)\r\n", (double)val);
            return;
        }
        /* TEC PID params */
        PID_t *pid = NULL;
        const char *param_name = NULL;

        if (strncmp(param, "crystal.", 8) == 0) {
            pid        = &g_crystal_pid;
            param_name = param + 8;
        } else if (strncmp(param, "laser.", 6) == 0) {
            pid        = &g_laser_pid;
            param_name = param + 6;
        } else {
            cli_send("ERR: unknown channel (use 'crystal' or 'laser')\r\n");
            return;
        }

        if (strcmp(param_name, "kp") == 0) {
            pid->Kp = val;
            cli_sendf("OK %s = %.4f\r\n", param, (double)val);
        } else if (strcmp(param_name, "ki") == 0) {
            pid->Ki = val;
            cli_sendf("OK %s = %.6f\r\n", param, (double)val);
        } else if (strcmp(param_name, "kd") == 0) {
            pid->Kd = val;
            cli_sendf("OK %s = %.4f\r\n", param, (double)val);
        } else if (strcmp(param_name, "setpoint") == 0) {
            pid->setpoint = val;
            PID_Reset(pid); /* reset integral when setpoint changes */
            if (pid == &g_laser_pid)
                TEC_LaserTempGuard_Reset();
            cli_sendf("OK %s = %.4f C (integral reset)\r\n", param, (double)val);
        } else {
            cli_send("ERR: unknown parameter\r\n");
        }
        return;
    }

    cli_sendf("ERR: unknown command: '%s' (type 'help')\r\n", line);
}

/* ── Display state sync ──────────────────────────────────────────────────── */

/* Push the "relevant" parameters (laser power, crystal wavelength, laser
 * on/off, 5V on/off) to the display whenever any of them actually change,
 * using the same command syntax the display sends us -- one parser on the
 * display side handles both directions identically.
 *
 * Diffed once per main loop iteration rather than pushed at each individual
 * 'set'/'laser on'/'5v on'/etc. call site: these can also change on their
 * own (e.g. the laser temp guard auto-disabling the laser on a trip -- see
 * g_laser_temp_trip_pending), and a diff check here catches that the same
 * way it catches an explicit CLI command, without needing a push call
 * threaded through every possible mutation site, present or future.
 *
 * Sentinel "impossible" initial values force a first sync on boot, once
 * Params_Load() has restored the real values -- see main.c USER CODE 2. */
static uint16_t s_sync_power_pct  = 0xFFFFu;
static float    s_sync_wavelength = -1.0f;
static uint8_t  s_sync_laser_on   = 0xFFu;
static uint8_t  s_sync_5v_on      = 0xFFu;
static uint8_t  s_sync_crystal_tec_on = 0xFFu;
static float    s_sync_crystal_temp = -1000.0f;
static float    s_sync_crystal_output = -1000.0f;

static void cli_sync_display(void)
{
    const LaserState_t *ls = Laser_Control_GetState();
    float wavelength = g_crystal_wl_k * g_crystal_pid.setpoint + g_crystal_wl_m;
    float temp, output;
    uint8_t v5_on = (HAL_GPIO_ReadPin(M6_EN_GPIO_Port, M6_EN_Pin) == GPIO_PIN_SET) ? 1u : 0u;
    char buf[40];

    if (ls->power_pct != s_sync_power_pct) {
        s_sync_power_pct = ls->power_pct;
        snprintf(buf, sizeof(buf), "set laser.power %u\r\n", (unsigned)ls->power_pct);
        BSP_DISPLAY_Send(buf, strlen(buf));
    }
    if (wavelength != s_sync_wavelength) {
        s_sync_wavelength = wavelength;
        snprintf(buf, sizeof(buf), "set crystal.wavelength %.2f\r\n", (double)wavelength);
        BSP_DISPLAY_Send(buf, strlen(buf));
    }
    if (ls->enabled != s_sync_laser_on) {
        s_sync_laser_on = ls->enabled;
        BSP_DISPLAY_Send(ls->enabled ? "laser on\r\n" : "laser off\r\n",
                          ls->enabled ? 10 : 11);
    }
    if (v5_on != s_sync_5v_on) {
        s_sync_5v_on = v5_on;
        BSP_DISPLAY_Send(v5_on ? "5v on\r\n" : "5v off\r\n",
                          v5_on ? 7 : 8);
    }

    uint8_t crystal_tec_on = (HAL_GPIO_ReadPin(BSP_TEC_EN_PORT, BSP_TEC_EN_PIN) == GPIO_PIN_SET) ? 1u : 0u;
    if (crystal_tec_on != s_sync_crystal_tec_on) {
        s_sync_crystal_tec_on = crystal_tec_on;
        BSP_DISPLAY_Send(crystal_tec_on ? "crystal.tec on\r\n" : "crystal.tec off\r\n",
                          crystal_tec_on ? 16 : 17);
    }

    TEC_Control_GetState(TEC_CRYSTAL, &temp, &output);
    if (temp != s_sync_crystal_temp) {
        s_sync_crystal_temp = temp;
        snprintf(buf, sizeof(buf), "crystal.temp %.4f\r\n", (double)temp);
        BSP_DISPLAY_Send(buf, strlen(buf));
    }

    if (output != s_sync_crystal_output) {
        s_sync_crystal_output = output;
        snprintf(buf, sizeof(buf), "crystal.output %.4f\r\n", (double)output);
        BSP_DISPLAY_Send(buf, strlen(buf));
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void CLI_Init(void)
{
    HAL_UART_Receive_IT(&huart1, &s_disp_rx_isr_byte, 1);
    cli_send("\r\n=== Laserfabriken ===\r\n");
    cli_send("Type 'help' for commands\r\n> ");
}

void CLI_Process(void)
{
    uint8_t byte;

    /* Print TEC power-good fault notification as soon as it is set by the control loop */
    if (g_tec_pg_fault_pending == 1) {
        g_tec_pg_fault_pending = 2; /* 2 = printed, waiting for clear */
        cli_send("\r\nTEC buck power-good fault (PG low) !!\r\n> ");
    } else if (g_tec_pg_fault_pending == 3) {
        g_tec_pg_fault_pending = 0;
        cli_send("\r\nTEC buck power-good OK\r\n> ");
    }


    /* Print trip notification as soon as it is set by the control loop */
    if (g_laser_temp_trip_pending) {
        g_laser_temp_trip_pending = 0;
        if (g_laser_temp_trip_reason == LASER_TEMP_TRIP_ABS_MAX)
            cli_send("\r\n!! LASER OFF — absolute max temperature exceeded !!\r\n> ");
        else
            cli_send("\r\n!! LASER OFF — temperature deviation exceeded after settling !!\r\n> ");
    }

    /* Drain all available bytes without blocking the main loop */
    while (BSP_CLI_Receive(&byte) == 0) {
        if (byte == '\r' || byte == '\n') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                cli_send("\r\n");
                disp_forward(s_line_buf);
                process_line(s_line_buf);
                s_line_pos = 0;
            }
            cli_send("> ");
        } else if (byte == 0x7F || byte == 0x08) {
            /* Backspace / DEL */
            if (s_line_pos > 0) {
                s_line_pos--;
                cli_send("\b \b");
            }
        } else if (byte >= 0x20 && s_line_pos < CLI_BUF_LEN - 1) {
            s_line_buf[s_line_pos++] = (char)byte;
            BSP_CLI_Send(&byte, 1); /* echo */
        }
    }

    /* Drain display UART — process received commands without forwarding back.
     * Bytes are captured by HAL_UART_RxCpltCallback() into a ring buffer the
     * instant they arrive (interrupt-driven, see top of file) -- this just
     * consumes what's already been captured, no direct UART access here. */
    while (disp_rx_pop(&byte)) {
        s_disp_rx_count++;
        s_disp_rx_last = byte;
        if (byte == '\r' || byte == '\n') {
            if (s_disp_pos > 0) {
                s_disp_buf[s_disp_pos] = '\0';
                cli_send("\r\n");
                process_line(s_disp_buf);
                s_disp_pos = 0;
                cli_send("> ");
            }
        } else if (byte >= 0x20 && s_disp_pos < CLI_BUF_LEN - 1) {
            s_disp_buf[s_disp_pos++] = (char)byte;
        }
    }

    cli_sync_display();
}
