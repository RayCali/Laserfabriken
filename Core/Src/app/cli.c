#include "cli.h"
#include "bsp_config.h"
#include "tec_control.h"
#include "laser_control.h"
#include "params.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define CLI_BUF_LEN  80

static char     s_line_buf[CLI_BUF_LEN];
static uint16_t s_line_pos;

static char     s_disp_buf[CLI_BUF_LEN];
static uint16_t s_disp_pos;

/* ── Output helpers ──────────────────────────────────────────────────────── */

static void cli_send(const char *str)
{
    BSP_CLI_Send(str, strlen(str));
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
        cli_sendf("Crystal TEC: T=%7.4f C  SP=%7.4f C  WL=%7.2f nm  k=%.4f nm/C  m=%.2f nm  out=%+6.4f"
                  "  Kp=%.4f  Ki=%.6f  Kd=%.4f\r\n",
                  (double)temp,
                  (double)g_crystal_pid.setpoint,
                  (double)wl,
                  (double)g_crystal_wl_k,
                  (double)g_crystal_wl_m,
                  (double)output,
                  (double)g_crystal_pid.Kp,
                  (double)g_crystal_pid.Ki,
                  (double)g_crystal_pid.Kd);
        if (g_tec_pg_fault)
            cli_send("  TEC buck: FAULT — power-good low, output disabled\r\n");

        TEC_Control_GetState(TEC_LASER, &temp, &output);
        cli_sendf("Laser  TEC: T=%7.4f C  SP=%7.4f  out=%+6.4f"
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

    cli_send("ERR: unknown command (type 'help')\r\n");
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void CLI_Init(void)
{
    cli_send("\r\n=== Laserfabriken v2 ===\r\n");
    cli_send("Type 'help' for commands\r\n> ");
}

void CLI_Process(void)
{
    uint8_t byte;

    /* Print TEC power-good fault notification as soon as it is set by the control loop */
    if (g_tec_pg_fault_pending) {
        g_tec_pg_fault_pending = 0;
        cli_send("\r\n!! TEC DISABLED — buck power-good fault (PG low) !!\r\n> ");
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

    /* Drain display UART — process received commands without forwarding back */
    while (BSP_DISPLAY_Receive(&byte) == 0) {
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
}
