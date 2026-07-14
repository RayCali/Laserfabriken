# UGN Hardware Bring-Up Status — Pinout Rework & Datasheet Verification

**Document:** UGN-STATUS-001
**Date:** 2026-07-05
**Author:** Board Architect Design AB

---

## 1. Context

The Laserfabriken UGN board schematic went through a pinout revision (SPI moved from SPI1 to SPI3, several signals reassigned to different ports/pins). This document tracks what was fixed in firmware to match the new schematic, what bugs were found and corrected against the ADS1220 / TPS563252 datasheets, and — most importantly — what still needs to be verified against **real hardware** before any of it can be considered "locked in."

Nothing in this document has been tested on physical hardware yet. Everything below is either (a) fixed and verified against a datasheet/schematic on paper, or (b) flagged as needing a bench measurement.

---

## 2. Fixed — pinout rework (matches current schematic)

Rewritten to match the new SPI3-based pinout: `main.h`, `gpio.c`, `spi.c`/`spi.h`, `usart.c`, `bsp_config.h`, `main.c`, comments in `bsp_tec.c`.

| Signal | Old pin | New pin | Notes |
|---|---|---|---|
| ADC_CS | PA2 | PA4 | ADS1220 chip select |
| DAC_CS | PA3 | PA3 | unchanged — reserved for DAC8562S (spudkortet) |
| Crystal TEC DAC | PA4 (DAC1_CH1) | PA5 (DAC1_CH2) | internal STM32 DAC |
| TEC_PG | PB2 | PA2 | TPS563252 power-good, open-drain, internal pull-up |
| TEC_EN | PB1 | PC5 | TPS563252 enable |
| POLARITY | PC15 | PC6 | H-bridge direction select |
| M6_EN | PB10 | PB0 | 5V rail enable |
| SPI bus | SPI1 (PB3/PA6/PA7) | SPI3 (PC10/PC11/PC12), AF6 | ADS1220 + DAC8562S (spudkortet) |
| DISPLAY_TX/RX | PB6/PB7 (USART1) | PA9/PA10 (USART1) | display UART |
| DISPLAY_DO1/2/3 | PB11/PB12/PB13 | PB13/PB14/PB15 | unused GPIO to display connector |

The `.ioc` file was synced to match these manual C edits (`PinState`, `GPIO_PuPd`, `functionlistsort` entries) so a CubeMX "Generate Code" run should reproduce the same output rather than clobber it.

---

## 3. Fixed — ADS1220 register bugs (verified against datasheet SBAS501D)

Found while cross-checking `bsp_temp.c` against the ADS1220 datasheet register tables (Table 8-10, Table 8-13):

| Constant | Was | Now | Bug |
|---|---|---|---|
| `ADS1220_MUX_AIN0_AVSS` | `0x60` (MUX=0110b) | `0x80` (MUX=1000b) | Measured AIN1−AIN0 (a floating differential pair — AIN1 isn't wired) instead of AIN0 vs AVSS single-ended |
| `ADS1220_VREF_EXTERNAL_REFP0` | `0x08` | `0x40` | VREF[1:0] is bits 7:6, not 4:3. Old value accidentally set the PSW bit (bit 3) instead, leaving the ADC on its **internal 2.048V reference** while `raw_to_resistance()` assumed the external 2.9V rail — every temperature reading would have been wrong |
| `BSP_ADS1220_DR_90SPS` | `0x20` (=45 SPS) | `0x40` (=90 SPS) | Mislabeled constant, currently unused (`BSP_ADS1220_DATA_RATE` uses the 20SPS constant, which was already correct) |

Also enabled **simultaneous 50Hz+60Hz notch rejection** (`ADS1220_FILTER_50_60HZ = 0x10`, was `0x00` = no rejection) since this product ships to markets with both mains frequencies — costs nothing at 20 SPS.

**Verified correct, no change needed:** SPI mode 1 (CPOL=0/CPHA=1), command byte encodings (RESET/START/RDATA/WREG), RESET recovery delay, continuous-conversion timing margin vs. the 100ms main loop tick.

---

## 4. Fixed — software spec compliance

- **M6_EN (5V rail) default state**: was LOW at startup, spec §3.5 requires "active at startup" — changed to `GPIO_PIN_SET` in `gpio.c`.
- **`5v on` / `5v off` CLI commands** added (`cli.c`), toggling M6_EN. Works over both USB-CLI and display UART since both feed the same `process_line()`.
- **`BSP_TEC_PowerGood()`** added (`bsp_tec.c`/`.h`) — reads the TEC_PG pin, and is now wired into `TEC_Control_Tick()`: if PG reads low, the crystal TEC is disabled (`BSP_TEC_Disable`) and held off every tick until PG reads good again. A one-shot fault notification prints to the CLI (`!! TEC DISABLED — buck power-good fault (PG low) !!`), and `status` shows a `TEC buck: FAULT` line while the fault is active.

---

## 5. Intentionally left alone (not bugs)

- `bsp_laser.c`, `laser_control.c`, all `laser.*` CLI commands, `DAC_CS`/`DAC1_CH1` (PA4) macros — kept dormant for the future **spudkortet** board, which adds a laser diode. On UGN hardware these are no-ops (single TEC channel only, `TEC_LASER` calls return early in `bsp_tec.c`).

### 5.1 Required PCB change for spudkortet — ADC_CS must move off PA4

The STM32G431 only exposes **two** external GPIO pins for the internal DAC1: PA4 (DAC1_CH1) and PA5 (DAC1_CH2). On UGN, PA5 drives the crystal TEC amplitude and PA4 is used as `ADC_CS` (ADS1220 chip select) — there's no laser TEC hardware on UGN, so this is fine.

On **spudkortet**, the laser TEC needs its own DAC amplitude channel, and the only one available is PA4 (DAC1_CH1). This means:

- **ADC_CS must be moved to a different free GPIO** on the spudkortet schematic — it cannot stay on PA4
- Resulting pin plan for spudkortet: PA4 = DAC1_CH1 (laser TEC amplitude), PA5 = DAC1_CH2 (crystal TEC amplitude, unchanged), PA3 = DAC_CS (DAC8562S, laser diode current — unchanged), ADC_CS = *(new pin, TBD)*

This is a **hardware/schematic change**, not something fixable in firmware — flag it to whoever designs the spudkortet schematic before that board is ordered.

---

## 6. Open items — require real hardware to verify

These are the things that must be measured/tested before the corresponding firmware behavior can be trusted.

### 6.1 TEC DAC → VOUT scaling (safety-relevant)

`bsp_tec.c: internal_dac_set()` inverts the 12-bit DAC code (`4095 - code`) assuming the TPS563252 FB network's stated design points: VDAC=0V→VOUT=10.8V, VDAC=2.4V→VOUT=0V.

**Problem found:** with the confirmed FB resistors (R14=150k to VOUT, R15=3.3k to DACin, no third resistor to GND), the KCL math predicts:
- VOUT crosses zero at VDAC ≈ **0.61V**, not 2.4V
- VOUT would want to reach ≈**27.9V** at VDAC=0V, not 10.8V (the 10.8V figure is likely just the buck's VIN=12V duty-cycle ceiling, not something the resistor network produces)

**What to do on the bench:**
1. Set DAC code to 0 (max commanded drive) → measure actual VOUT
2. Set DAC code to 4095 (off) → measure actual VOUT
3. If possible, sweep a few intermediate codes to find the real VOUT=0 crossover point
4. Report values back so `internal_dac_set()` can be rescaled to use the full 12-bit range across only the *useful* VDAC sub-range (right now a portion of the low-drive command range likely does nothing, since VOUT is already clamped at 0 before VDAC reaches its "off" position)

### 6.2 POLARITY GPIO direction (heat vs. cool)

`bsp_tec.c` assumes `GPIO_PIN_SET` = heat, `GPIO_PIN_RESET` = cool — a placeholder decision, not derived from the schematic. The M4 H-bridge's exact FET switching logic plus which physical TEC lead is wired to connector pin A vs. B would need to be traced to determine this from the schematic alone, and that wasn't conclusive from the available drawing.

**What to do on the bench:** apply a known TEC, watch which face heats when POLARITY is driven high, adjust the sign convention in `BSP_TEC_SetOutput()` if wrong.


### 6.4 SPI timing margins

No explicit delays were added between `HAL_GPIO_WritePin` (CS assert) and the first `HAL_SPI_Transmit` call to satisfy ADS1220's `td(CSSC)` (50ns min). Relying on HAL call overhead being sufficient at a 4MHz SPI clock. Watch for corrupted/garbage ADC reads; add explicit delays if seen.

### 6.5 ~~`BSP_TEC_PowerGood()` — not yet integrated~~ (done)

Fixed: `TEC_Control_Tick()` now disables the crystal TEC and latches a fault flag (`g_tec_pg_fault`) whenever PG reads low, auto-clearing once PG reads good again. CLI prints a one-shot notification and `status` shows the fault while active. **Still needs a bench test**: force PG low (or simulate a buck fault) and confirm the TEC actually cuts out and recovers as expected.

### 6.6 M6_EN (5V rail) — actual load unconfirmed

Per spec §3.5 it's just "the 5V output." Control (`5v on`/`5v off`) is implemented, but what's actually connected to it, and what its default-on behavior implies at power-up, hasn't been confirmed against the real board.



---

## 7. PID tuning procedure

The PID parameters shipped in `tec_control.c` (`Kp=0.5, Ki=0.02, Kd=0.1`) are spec-mandated *default/reset* values (mjukvaruspecifikation §3.1), not tuned values — they exist so the system has a safe, defined starting point after a fresh flash, not because they're expected to hit the <5mK steady-state stability requirement out of the box. Real tuning has to happen against the actual crystal oven + TEC + buck thermal mass, which doesn't exist until the board is built.

**Relevant fixed implementation details** (don't need to be re-derived during tuning):
- Control loop runs every 100ms (`PID_PERIOD_S = 0.1f` in `tec_control.c`) — thermal systems like this are normally dominated by multi-second time constants, so 100ms is not the limiting factor
- Integral term uses double precision (avoids long-run accumulation drift)
- D-term is smoothed by an EMA low-pass filter (`D_ALPHA = 0.1`, compile-time constant, not CLI-exposed) to suppress measurement noise feeding into the derivative
- Anti-windup via back-calculation is already implemented — integral won't runaway during saturation, but still watch for slow recovery if gains are too aggressive
- Output is clamped to [−1.0, +1.0] before being handed to `BSP_TEC_SetOutput()`

### 7.1 Recommended method — Ziegler–Nichols (ultimate gain)

Chosen because it needs no system model, works well for slow first-order-ish thermal plants, and only requires the CLI commands already implemented.

**Step 1 — find the critical gain:**
```
set crystal.ki 0
set crystal.kd 0
set crystal.kp 0.1        (start low, increase gradually)
status                    (repeat, watch T vs SP over time)
```
Increase `crystal.kp` in small steps until the temperature settles into a **sustained, constant-amplitude** oscillation around the setpoint (not growing, not decaying). Record:
- `Kc` = the Kp value at which this happens
- `Pc` = the oscillation period, in seconds (time between two peaks)

**Step 2 — Ziegler–Nichols starting point:**
```
Kp = 0.6  × Kc
Ki = 1.2  × Kc / Pc
Kd = 0.075 × Kc × Pc
```
Set these via `set crystal.kp/ki/kd <val>` — this is a starting point, not the final answer.

**Step 3 — fine-tune:**

Watch three specific symptoms in `status` output, and adjust one gain at a time:

1. **Residual steady-state offset** — temperature settles near the setpoint but never quite reaches it (e.g. holds at 24.95°C when setpoint is 25.0°C). P alone can never fully cancel a constant disturbance (e.g. heat leaking to ambient) — that's the integral term's job, and it's too weak. → **nudge `Ki` up** slightly.

2. **Overshoot / ringing** — after a setpoint step (`set crystal.setpoint <val>`, which auto-resets the integral), the temperature shoots past the target before settling (overshoot), or bounces back and forth several times (ringing) instead of approaching smoothly. The loop is reacting too aggressively. → **reduce `Kp` (and/or `Kd`)**.

3. **Jittery/chattery output** — the `output` value in `status` flickers rapidly even while temperature sits still near setpoint. The D-term differentiates the temperature signal, which amplifies whatever measurement noise is present (even small ADC noise becomes large once differentiated) — feeding a noisy command to the TEC. → **reduce `Kd`** (the fixed `D_ALPHA` EMA filter only smooths so much).

**Step 4 — verify against spec:**
- Let the loop settle at a fixed setpoint and log `status` output (T column) for several minutes
- Confirm peak-to-peak (or stddev) steady-state variation is **< 5 mK** per spec §3.1
- Repeat independently for the laser TEC (`set laser.kp/ki/kd`) once spudkortet exists — on UGN hardware `TEC_LASER` is a no-op, so this can only be exercised via `set sim.laser_temp` in simulation for now

### 7.2 Safety notes specific to this tuning pass

- **Do not tune aggressively (large `Kp`) until §6.1 (DAC→VOUT scaling) is resolved.** Until the real VDAC→VOUT relationship is measured, a "moderate" commanded output could correspond to an unexpectedly large actual TEC voltage.
- Start tuning with small setpoint deviations from ambient to keep TEC current low while gains are still unknown-safe.
- Persist good values with `save` once satisfied — otherwise they're lost on next reset (parameters storage method is itself still an open decision, spec §7.1).

---

## 8. Summary

Everything in §2–§4 is fixed and checked against the datasheets/schematics available. Nothing in §6 can be considered final until measured on the real board — in particular §6.1 (DAC scaling) and §6.2 (polarity direction) are safety-relevant and must be verified before running the TEC at any significant drive level.
