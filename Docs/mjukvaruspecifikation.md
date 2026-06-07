# Mjukvaruspecifikation — Laserfabriken v1

**Dokument:** SW-SPEC-001  
**Version:** 1.3  
**Datum:** 2026-05-31  
**Författare:** Board Architect Design AB  
**Status:** Under granskning — öppna beslutspunkter återstår (se avsnitt 7)

---

## 1. Syfte och scope

Detta dokument utgör den definitiva mjukvaruspecifikationen för Laserfabriken v1. Specifikationen definierar vad systemet ska göra, vilka gränssnitt som ska stödjas och vilka prestandakrav som gäller. Allt som inte explicit nämns i detta dokument är **utanför scope** och kräver ett separat tilläggsavtal för att implementeras.

Hårdvaruval och komponentspecifikationer dokumenteras i ett separat hårdvarudokument.

---

## 2. Systembeskrivning

Laserfabriken är ett instrument för precisionsstyrning av temperatur och laserdiodström i ett laserfabrikationssystem. Systemet består av följande enheter:

| Enhet | Roll |
|---|---|
| Huvudkort (STM32) | Reglering, mätning, kommunikation |
| Display (ESP32-S3) | Användargränssnitt |
| ESP32-mottagare *(alt. A)* | Tar emot trådlös signal från display, vidarebefordrar via UART till STM32 |

ESP32-mottagaren är endast aktuell om trådlöst protokoll väljs (se avsnitt 7.2). Vid val av UART kommunicerar displayen direkt med STM32 utan extra komponent.

---

## 3. Mjukvarukrav

### 3.1 PID-temperaturreglering

Systemet ska implementera individuell PID-reglering för kristall-TEC och laser-TEC.

**Krav:**

- Två oberoende PID-instanser körs parallellt (kristall och laser)
- Temperaturstabilitet vid steady state: < 5 mK
- PID-parametrar (Kp, Ki, Kd) och börvärde ska vara inställbara via USB-terminal
- PID-parametrar sparas i icke-flyktigt minne (metod fastställs i avsnitt 7.1)
- Parametrar ska återställas vid uppstart
- PID-integral använder dubbel precision (64-bit) för att undvika ackumuleringsdrift
- D-termen filtreras med EMA low-pass-filter (konfigurerbar alfa) för att undertrycka mätbrus
- Anti-windup via back-calculation (integral-clamping)
- TEC-utgång representeras som normaliserat värde [−1,0 ; +1,0]
- TEC-regleringen är alltid aktiv oberoende av laserdiodstillstånd

**Standardvärden vid uppstart (om icke-flyktigt minne ej initierat):**

| Parameter | Kristall | Laser TEC |
|---|---|---|
| Kp | 0,5 | 0,5 |
| Ki | 0,02 | 0,02 |
| Kd | 0,1 | 0,1 |
| Börvärde | 25,0 °C | 25,0 °C |

#### 3.1.1 Kristallvåglängdsstyrning

Eftersom kristallens uteffekt beror på våglängd snarare än temperatur ska systemet tillåta inmatning av önskad våglängd direkt. Omvandling till temperaturbörvärde sker internt.

**Modell:** linjär korrelation λ = k · T + m → T = (λ − m) / k

**Krav:**

- Börvärde ska kunna ställas in som våglängd i nm via CLI och display
- Omvandlingskoefficienter k (nm/°C, min 4 decimaler) och m (nm, min 2 decimaler) ska vara konfigurerbara via CLI
- Standardvärden härledda från karakteriseringsdata:

| Parameter | Standardvärde |
|---|---|
| k | 0,1929 nm/°C |
| m | 1548,60 nm |

### 3.2 Laserdiodstyrning

**Krav:**

- Laserdiodström styrs som ett oberoende delsystem, separerat från TEC-regleringen
- Styrning sker via effektnivå 0–100 % med följande mappning:
  - 0 % → 0 A (laser av)
  - 1–100 % → linjär interpolation mellan tröskelström och maxström
- Tröskelström: laser ger ingen mätbar uteffekt under detta värde (standard 0,1 A, konfigurerbar)
- Om ström sätts direkt under tröskelström men över 0 A sätts strömmen till 0 A
- Maxström: 2,0 A (mjukvarugräns, konfigurerbar)
- Lasern är **alltid av vid uppstart** — kräver explicit aktivering
- Om ny maxgräns understiger aktuell ström reduceras strömmen omedelbart
- Effektnivå och aktivt tillstånd (ON/OFF) ska visas på display i realtid
- Kalibrering av DAC-kod → faktisk ström måste utföras mot hårdvaran vid idrifttagning

**Säkerhetskrav:**

- Lasern får inte aktiveras automatiskt vid omstart
- Strömmen ska aldrig överstiga hårdvarumax definierad i konfigurationsfilen
- Vid idrifttagning: börja med maxström 0,1 A och öka stegvis med verifiering mot multimeter

#### 3.2.1 Temperaturskydd för laser-TEC

Systemet ska skydda laserdioden mot övertemperatur och temperaturdrift via två oberoende mekanismer.

**Absolut maxtemperatur:**
- Om laser-TEC-temperaturen överstiger det konfigurerade gränsvärdet stängs laserdioden av omedelbart
- Standard: 60,0 °C

**Avvikelseskydd (deviation guard):**
- Skyddet aktiveras i två faser för att tolerera normal PID-överskjutning vid uppstart:
  1. **Settling-fas:** timern ackumulerar tid då temperaturen ligger inom ±max\_avvikelse från börvärdet; om temperaturen lämnar fönstret nollställs timern
  2. **Bevakningsfas:** när timern nått inställd tid är skyddet aktivt; om temperaturen därefter avviker mer än max\_avvikelse stängs laserdioden av
- Skyddet återställs till settling-fas vid `laser on` eller ändring av laser-TEC-börvärde

**Standardvärden:**

| Parameter | Standardvärde |
|---|---|
| Max avvikelse | 5,0 °C |
| Absolut maxtemperatur | 60,0 °C |
| Settling-tid | 30,0 s |

**Notis:** CLI-terminalen skriver ut ett varningsmeddelande omedelbart när laserdioden stängs av av temperaturskyddet.

### 3.3 USB-terminal (CLI)

Systemet ska tillhandahålla ett kommandoradsgränssnitt via USB/UART vid 115200 baud.

**Kommandon:**

| Kommando | Beskrivning |
|---|---|
| `set crystal.kp <val>` | Ställ Kp för kristall-PID |
| `set crystal.ki <val>` | Ställ Ki för kristall-PID |
| `set crystal.kd <val>` | Ställ Kd för kristall-PID |
| `set crystal.setpoint <val>` | Ställ börvärde kristall (°C), återställer integral |
| `set crystal.wavelength <val>` | Ställ börvärde via våglängd (nm), omvandlar till °C via k/m |
| `set crystal.k <val>` | Ställ våglängdsmodellens lutning k (nm/°C) |
| `set crystal.m <val>` | Ställ våglängdsmodellens offset m (nm) |
| `set laser.kp <val>` | Ställ Kp för laser-TEC-PID |
| `set laser.ki <val>` | Ställ Ki för laser-TEC-PID |
| `set laser.kd <val>` | Ställ Kd för laser-TEC-PID |
| `set laser.setpoint <val>` | Ställ börvärde laser TEC (°C), återställer integral och temperaturskydd |
| `set laser.current <val>` | Ställ laserdiodström direkt (A) |
| `set laser.maxcurrent <val>` | Ställ mjukvarugräns för laserdiodström (A) |
| `set laser.threshold <val>` | Ställ tröskelström under vilken laser ger ingen uteffekt (A) |
| `set laser.power <val>` | Ställ effektnivå 0–100 % (mappar linjärt till tröskelström–maxström) |
| `set laser.max_temp_deviation <val>` | Ställ max tillåten temperaturavvikelse efter settling (°C) |
| `set laser.absolute_max_temp <val>` | Ställ absolut maxtemperatur för laser-TEC (°C) |
| `set laser.temp_timer <val>` | Ställ settling-tid innan temperaturskyddet aktiveras (s) |
| `laser on` | Aktivera laserdiod, återställer temperaturskydd |
| `laser off` | Avaktivera laserdiod |
| `status` | Visa temperatur, börvärde, PID-utgång, laserstatus och temperaturskyddsstatus |
| `help` | Lista tillgängliga kommandon |

**Krav på CLI:**
- Icke-blockerande (stör inte PID-regleringen)
- Lokal echo av inmatade tecken
- Stöd för backspace/DEL
- Svar skickas alltid som bekräftelse eller felmeddelande

### 3.4 Displaygränssnitt

Displayen kommunicerar med STM32 via det protokoll som fastställs i avsnitt 7.2. Nedanstående funktionskrav gäller oavsett transportval.

**Vad displayen visar:**

| Element | Beskrivning |
|---|---|
| Lasereffekt | Aktuell effektnivå, 0–100 %, analogmätare |
| Kristall våglängdsbörvärde | Våglängdsbörvärde i nm |
| Laser ON/OFF | Knapp som visar och styr laserns aktivt tillstånd |

**Vad användaren kan styra via display:**

| Styrning | Metod |
|---|---|
| Lasereffekt | Roterande knapp (steg 1 %) |
| Kristall våglängdsbörvärde | Roterande knapp (steg 1 nm / 0,1 nm / 0,01 nm — väljs genom att trycka på mätaren) |
| Val av aktiv parameter | Touch på respektive mätare |
| Laser ON/OFF | Touch på knapp |

**Datastruktur (display → STM32):**

```
struct_message {
    uint8_t power_pct;             // Lasereffekt (0–100 %)
    float   crystal_wavelength_nm; // Kristall våglängdsbörvärde (nm) → omvandlas till °C via k/m
    bool    laser_on;              // Laserns tillstånd
}
```

Meddelandet skickas vid varje användarinteraktion och omvandlas till motsvarande CLI-kommandon på STM32-sidan. Effektnivån översätts till `set laser.power <val>`, våglängden till `set crystal.wavelength <val>`.

**Skärmsläckning:**

- Om lasern är aktiv (ON) och ingen användarinteraktion sker på 5 sekunder släcks bakgrundsbelysningen
- Touch på skärmen återaktiverar bakgrundsbelysningen utan att ändra något värde
- Skärmsläckning är inte aktiv när lasern är av

### 3.5 5V-utgångsstyrning

- STM32 ska kunna aktivera och avaktivera 5V-utgången via GPIO
- Styrning ska vara möjlig via USB-terminal och display
- Tillstånd: aktivt vid uppstart

### 3.6 Firmware-uppdatering

Systemet ska stödja firmware-uppdatering utan proprietär drivrutin från MCU-tillverkaren.

**STM32 — USB DFU med WinUSB:**
- STM32 aktiverar inbyggd ROM-bootloader (DFU-läge) via boot-mode-pin
- På Windows används WinUSB — en standard Microsoft-drivrutin (installeras en gång via Zadig, öppen källkod)
- Flashning sker med `dfu-util` (öppen källkod, ingen proprietär mjukvara krävs)

**ESP32-display — flashning via STM32:**
- Om UART väljs (se avsnitt 7.2) ska STM32 kunna agera som transparent brygga för ESP32-firmware-uppdatering
- STM32 kontrollerar ESP32:s EN- och GPIO0-pinnar för att aktivera ESP32:s inbyggda ROM-bootloader
- PC kommunicerar med ESP32 via STM32:s USB-port utan att ESP32 behöver vara direkt åtkomlig
- Flashningsverktyg: `esptool` (öppen källkod)

---

## 4. Parametersparning

Se avsnitt 7.1 för val av lagringsmetod. Oavsett val gäller nedanstående krav.

**Parametrar som sparas:**
- Kp, Ki, Kd och börvärde för kristall-PID
- Våglängdsmodellens koefficienter k och m för kristall
- Kp, Ki, Kd och börvärde för laser-TEC-PID
- Laserdiod maxström och tröskelström
- Temperaturskyddets parametrar (max avvikelse, absolut max, settling-tid)

---

## 5. Mjukvaruarkitektur

```
┌─────────────────────────────────────────────┐
│  Applikationslager                          │
│  tec_control.c  laser_control.c  cli.c      │
├─────────────────────────────────────────────┤
│  BSP-lager (Board Support Package)          │
│  bsp_temp.c  bsp_tec.c  bsp_laser.c        │
├─────────────────────────────────────────────┤
│  HAL (SPI, GPIO, DAC, UART)                 │
└─────────────────────────────────────────────┘
```

Portabiliteten är koncentrerad till en enda konfigurationsfil (`bsp_config.h`).

---

## 6. Prestandakrav

| Krav | Värde |
|---|---|
| Temperaturmätområde | −55 till +150 °C |
| Temperaturmätupplösning | 0,01 °C |
| Temperaturstabilitet (steady state) | < 5 mK |
| TEC-strömområde | −3,0 till +3,0 A |
| Laserdiodströmområde | 0,0 till 2,0 A |
| Laserströmupplösning | 50 µA |
| Laserströmstabilitet | < 50 µA @ 0–10 Hz, konstant omgivningstemperatur |
| CLI-svarstid | < 500 ms |
| Uppstartstid till aktiv reglering | < 2 s |

---

## 7. Öppna beslutspunkter

Nedanstående punkter är ej fastställda och måste beslutas innan status kan ändras till "Fastställd".

---

### 7.1 Parametersparning — Backup SRAM eller Flash

PID-parametrar och börvärden måste överleva omstart. Valet av lagringsmetod påverkar kretskortslayout (RTC-batteri) och mjukvaruimplementationen. Beslutet måste fattas innan hårdvarudesignen låses.

**Alternativ A: Backup SRAM (rekommenderas)**

| Egenskap | Värde |
|---|---|
| Kapacitet | 1 kB (RTC-domän) |
| Skrivningar | Obegränsade |
| Skrivtid | Omedelbar, blockerar inte CPU |
| Krav | RTC-batteri på kretskortet |

**Alternativ B: Intern Flash**

| Egenskap | Värde |
|---|---|
| Kapacitet | Delar av programmets Flash-minne |
| Skrivningar | Max ~10 000 (Flash-livslängd) |
| Skrivtid | Kräver sektorsradering, blockerar CPU kortvarigt |
| Krav | Inget extra batteri |

**Rekommendation:** Backup SRAM förutsatt att RTC-batteri finns på kretskortet. Flash är designat för programkod och lämpar sig dåligt för parametrar som ändras av användaren.

**Beslut krävs av:** Laserfabriken  
**Påverkar:** Avsnitt 3.1, 4

---

### 7.2 Displaykommunikation — Trådlöst eller UART

Displayen (ESP32-S3) behöver kommunicera med STM32-huvudkortet. Valet avgör om en extra ESP32-modul krävs på kretskortet, om EU:s radiodirektiv (RED) gäller för produkten, och hur firmware-uppdatering av displayen går till. Beslutet påverkar både kretskortslayout och certifieringsprocess.

**Alternativ A: Trådlöst (ESP-NOW)**

Kommunikationskedja:
```
Display (ESP32-S3) → ESP-NOW (2,4 GHz) → Separat ESP32 (mottagare) → UART → STM32
```

| Egenskap | Konsekvens |
|---|---|
| Kommunikation | Trådlöst mellan display och mottagare-ESP32 |
| Extra komponent | Separat ESP32-modul monterad på eller nära huvudkortet |
| Certifiering | EU Radio Equipment Directive (RED) krävs för kommersiell produkt |
| Kablar | Ingen kabel mellan display och huvudkort |
| ESP32-flashning | Kräver separat fysisk åtkomst till respektive ESP32 |

**Alternativ B: UART (kabelansluten)**

Kommunikationskedja:
```
Display (ESP32-S3) → UART (kabel) → STM32
```

| Egenskap | Konsekvens |
|---|---|
| Kommunikation | Kabelansluten direkt mellan display och STM32 |
| Extra komponent | Ingen extra ESP32 behövs |
| Certifiering | RED krävs ej — ingen radioutrustning |
| Kablar | Kabel mellan display och huvudkort |
| ESP32-flashning | Kan ske via STM32 som transparent brygga (se avsnitt 3.6) |

**Rekommendation:** UART om produkten ska säljas utan radiocertifiering. Trådlöst om kabelanslutning inte är möjlig och RED-certifiering accepteras.

**Beslut krävs av:** Laserfabriken  
**Påverkar:** Avsnitt 2, 3.4, 3.6

---

## 8. Revisionshistorik

| Version | Datum | Ändring |
|---|---|---|
| 1.0 | 2026-05-18 | Första utgåva |
| 1.1 | 2026-05-18 | Öppna beslutspunkter tillagda |
| 1.2 | 2026-05-18 | Hårdvarugränssnitt (avsnitt 3) borttaget, prestandakrav utökade |
| 1.3 | 2026-05-31 | Effektstyrning (0–100 %), tröskelström, kristallvåglängdsstyrning, laser-TEC-temperaturskydd, display uppdaterad till våglängd |
