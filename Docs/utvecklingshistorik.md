# Utvecklingshistorik — Laserfabriken

**Dokument:** DEV-HIST-001  
**Version:** 1.0  
**Datum:** 2026-06-04  
**Författare:** Board Architect Design AB

---

## Varför två versioner?

Projektet har två kodversioner som speglar två olika hårdvaruplattformar:

| Version | Hårdvara | Syfte |
|---------|----------|-------|
| v1 | STM32 Nucleo F401RE | Snabb prototyp- och utvecklingsplattform |
| v2 | STM32G431RBTx (LQFP64) | Målhårdvara för slutprodukt |

Anledningen till att börja på Nucleo F401RE var att den finns omedelbart tillgänglig med inbyggd ST-Link och USB-till-UART-brygga, vilket gör det möjligt att skriva och testa firmware innan slutkortet är designat. Mjukvaruarkitekturen designades från start med ett BSP-lager (Board Support Package) för att göra migrationen till G431 så enkel som möjligt — i teorin ska bara `bsp_config.h` behöva ändras vid processorbytet.

---

## Laserfabriken v1 — STM32 Nucleo F401RE

### Syfte
Utvecklings- och verifieringsplattform. All applikationslogik implementerades och testades här innan migrationen till G431.

### Vad som implementerades

**PID-temperaturreglering**
- Två oberoende PID-instanser: kristall-TEC och laser-TEC
- Dubbel precision (64-bit) på integraltermen för att undvika ackumuleringsdrift
- EMA low-pass filter på D-termen mot mätbrus
- Anti-windup via back-calculation (integral-clamping)
- TEC alltid aktiv oberoende av laserns tillstånd

**Kristallvåglängdsstyrning**
- Börvärde ställs i nm istället för °C
- Linjär modell: λ = k·T + m → T = (λ − m) / k
- Standardvärden: k = 0,1929 nm/°C, m = 1548,6 nm

**Laserdiodstyrning**
- Effektstyrning 0–100 % med linjär mappning mot tröskelström och maxström
- Tröskelström (standard 0,1 A) — strömmen nollställs automatiskt under detta värde
- Maxström mjukvarubegränsad (standard 2,0 A)
- Lasern alltid av vid uppstart

**Laser-TEC temperaturskydd**
- Absolut maxtemperatur (standard 60 °C) — laser stängs av omedelbart vid överskridning
- Avvikelseskydd (deviation guard) med settling-fas
  - Settling-timer (standard 30 s): ackumulerar tid inom ±max_avvikelse från börvärde
  - Timer nollställs om temperaturen lämnar fönstret under settling
  - När settled: laser stängs av om avvikelse överstiger gränsen
- Skyddet återställs vid `laser on` eller ändring av laser-TEC-börvärde

**USB-terminal (CLI)**
- Kommunikation via USART2 (PA2/PA3) → ST-Link virtuell COM-port
- Icke-blockerande polling i main loop
- Alla PID-parametrar, börvärden, laserström och temperaturskyddsparametrar konfigurerbara
- Simuleringskommando: `set sim.laser_temp` för testning utan hårdvara
- Omedelbar utskrift vid temperaturskyddsutlösning

**BSP-skelett**
- `bsp_temp.c`: ADS1220 stub (returnerar 0,0 °C, Steinhart-Hart implementerad)
- `bsp_tec.c`: DRV8873S stub (GPIO-riktning komplett, intern DAC TODO)
- `bsp_laser.c`: DAC8562S stub (CS-GPIO komplett, SPI TODO)

### CLI-kommunikation
```
USART2 (PA2/PA3) → ST-Link inbyggd USB-UART-brygga → PC
```

---

## Laserfabriken v2 — STM32G431RBTx

### Syfte
Migration till målprocessorn. Samma applikationslogik som v1, uppdaterat BSP-lager och ny pinlayout optimerad för kabeldragning på slutkortet.

### Hårdvaruplattform under migration
Panelkort med STM32G431RBTx (LQFP64) används som testplattform tills det egna Laserfabriken-kortet är färdigdesignat.

### Vad som ändrades

**Processor och package**
- STM32F401RE → STM32G431RBTx
- LQFP48 → LQFP64 (fler GPIO, PC0–PC12 tillgängliga)
- Systemklocka: upp till 170 MHz (HSI + PLL)

**Pinlayout — optimerad för kabeldragning**

| Signal | v1 (F401RE) | v2 (G431RB) | Anledning till ändring |
|--------|-------------|-------------|------------------------|
| SPI1_SCK | PA5 | PB3 | Frigör PA5 till DAC1_OUT2 |
| ADC_CS | PB6 | PA2 | Samlas med SPI-klustret |
| DAC_CS | PC7 | PA3 | Samlas med SPI-klustret |
| DRV1_CS | PB5 | PC5 | Intill DRV2_CS (PC4) |
| DRV1_nFAULT | PB3 | PA0 | PB3 frigjort till SPI SCK |
| DAC1_OUT1 | — | PA4 | Ny — kristall TEC-amplitud |
| DAC1_OUT2 | — | PA5 | Ny — laser TEC-amplitud |
| CLI | USART2 PA2/PA3 | USB CDC PA11/PA12 | Ingen ST-Link på slutkort |
| Display | USART1 PA9/PA10 | UART4 PC10/PC11 | Anpassning till G431 |

**USB CDC för CLI**
- Eftersom G431-kortet saknar inbyggd ST-Link används USB CDC direkt från processorn (PA11/PA12)
- Ringbuffer (256 byte) i `usbd_cdc_if.c` för icke-blockerande mottagning
- `BSP_CLI_Send` / `BSP_CLI_Receive` makron i `bsp_config.h` abstraherar CLI-transporten
- `cli.c` är identisk med v1 förutom att UART-anrop ersatts med BSP-makron

**Intern DAC aktiverad**
- DAC1_OUT1 (PA4): kristall TEC-amplitud till DRV8873S
- DAC1_OUT2 (PA5): laser TEC-amplitud till DRV8873S
- Kräver att SPI1_SCK flyttas från PA5 till PB3

**Applikationslagret oförändrat**
- `pid.c`, `tec_control.c`, `laser_control.c`, `cli.c` — identiska med v1
- Allt som ändrades: `bsp_config.h` (handles, makron), `main.c` (init-anrop), HAL-include (f4 → g4)

---

## Arkitekturprincip

```
┌─────────────────────────────────┐
│  cli.c  tec_control.c           │  App-lager: vet varför, inte hur
│  laser_control.c  pid.c         │
├─────────────────────────────────┤
│  bsp_tec.c  bsp_laser.c         │  BSP-lager: vet hur, inte varför
│  bsp_temp.c                     │
├─────────────────────────────────┤
│  STM32 HAL (SPI, GPIO, DAC...)  │
└─────────────────────────────────┘
```

Portabiliteten är koncentrerad till `bsp_config.h`. Vid byte från F401RE till G431RB ändrades enbart denna fil samt HAL-inkludet i BSP-filerna.

---

## Öppna beslutspunkter (från mjukvaruspecifikation v1.3)

| Punkt | Status |
|-------|--------|
| 7.1 Parametersparning (Backup SRAM eller Flash) | Ej beslutat |
| 7.2 Displaykommunikation (ESP-NOW eller UART) | UART valt — UART4 konfigurerad i v2 |
