#include "cli.h"
#include "bsp_config.h"
#include "tec_control.h"
#include "laser_control.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define CLI_BUF_LEN  80

static char     s_line_buf[CLI_BUF_LEN];
static uint16_t s_line_pos;

/* ── Output helpers ──────────────────────────────────────────────────────── */

static void cli_send(const char *str)
{
    HAL_UART_Transmit(BSP_CLI_UART, (const uint8_t *)str,
                      (uint16_t)strlen(str), 500);
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
                 "  set laser.kp           <val>\r\n"
                 "  set laser.ki           <val>\r\n"
                 "  set laser.kd           <val>\r\n"
                 "  set laser.setpoint     <val>  (C)\r\n"
                 "  set laser.current      <val>  (A)\r\n"
                 "  set laser.maxcurrent   <val>  (A)\r\n"
                 "  set laser.threshold    <val>  (A)\r\n"
                 "  set laser.power        <val>  (0-100 %)\r\n"
                 "  laser on\r\n"
                 "  laser off\r\n"
                 "  status\r\n"
                 "  help\r\n");
        return;
    }

    /* status */
    if (strcmp(line, "status") == 0) {
        float temp, output;

        TEC_Control_GetState(TEC_CRYSTAL, &temp, &output);
        cli_sendf("Crystal TEC: T=%7.4f C  SP=%7.4f  out=%+6.4f"
                  "  Kp=%.4f  Ki=%.6f  Kd=%.4f\r\n",
                  (double)temp,
                  (double)g_crystal_pid.setpoint,
                  (double)output,
                  (double)g_crystal_pid.Kp,
                  (double)g_crystal_pid.Ki,
                  (double)g_crystal_pid.Kd);

        TEC_Control_GetState(TEC_LASER, &temp, &output);
        cli_sendf("Laser  TEC: T=%7.4f C  SP=%7.4f  out=%+6.4f"
                  "  Kp=%.4f  Ki=%.6f  Kd=%.4f\r\n",
                  (double)temp,
                  (double)g_laser_pid.setpoint,
                  (double)output,
                  (double)g_laser_pid.Kp,
                  (double)g_laser_pid.Ki,
                  (double)g_laser_pid.Kd);

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

    /* set <param> <value> */
    char param[32], value_str[16];
    if (sscanf(line, "set %31s %15s", param, value_str) == 2) {
        float val = strtof(value_str, NULL);

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
    cli_send("\r\n=== Laserfabriken v1 ===\r\n");
    cli_send("Type 'help' for commands\r\n> ");
}

void CLI_Process(void)
{
    uint8_t byte;

    /* Drain all available bytes without blocking the main loop */
    while (HAL_UART_Receive(BSP_CLI_UART, &byte, 1, 0) == HAL_OK) {
        if (byte == '\r' || byte == '\n') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                cli_send("\r\n");
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
            HAL_UART_Transmit(BSP_CLI_UART, &byte, 1, 10); /* echo */
        }
    }
}
