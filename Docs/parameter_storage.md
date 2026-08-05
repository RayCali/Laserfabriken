# Parametersparning — Flash-implementation & slitage

**Dokument:** PARAM-STORE-001
**Datum:** 2026-08-05
**Författare:** Board Architect Design AB

---

## 1. Bakgrund

§7.1 i `mjukvaruspecifikation.md` flaggade valet mellan Backup SRAM (obegränsade skrivningar, men kräver RTC-batteri på kretskortet) och intern Flash (inget batteri krävs, men begränsad skriv-/raderingslivslängd) som ett beslut som måste fattas innan hårdvarudesignen låses. Rekommendationen där var Backup SRAM.

Den faktiska implementationen (`Core/Src/app/params.c`) använder **intern Flash**, inte Backup SRAM — sannolikt eftersom inget RTC-batteri finns monterat på UGN-kortet. Det här dokumentet beskriver hur den implementationen faktiskt fungerar, vad den sparar, och vad det innebär för Flash-minnets livslängd i praktiken.

---

## 2. Vilka parametrar sparas

`Params_Save()` skriver **exakt samma kompletta fältlista** varje gång den körs, oavsett vad som triggade sparningen:

| Fält | Beskrivning |
|---|---|
| `crystal_kp`, `crystal_ki`, `crystal_kd` | Kristall-TEC:ns PID-parametrar |
| `crystal_setpoint` | Kristall-TEC:ns temperatur-börvärde |
| `crystal_wl_k`, `crystal_wl_m` | Våglängd↔temperatur-modellens lutning/offset |
| `crystal_wavelength_nm` | Beräknad våglängd (härledd ur ovanstående tre, sparas som bekvämlighetskopia) |
| `laser_kp`, `laser_ki`, `laser_kd` | Laser-TEC:ns PID-parametrar |
| `laser_setpoint` | Laser-TEC:ns temperatur-börvärde |
| `laser_max_current_A` | Mjukvarubegränsning för laserström |
| `laser_threshold_A` | Laserns tröskelström |
| `laser_power_pct` | Laserns effektnivå (0–100%) |
| `temp_max_dev_C`, `temp_abs_max_C`, `temp_timer_s` | Lasertemperaturvaktens inställningar |

**Sparas ALDRIG, oavsett hur sparning triggas:** laser på/av-status, 5V-utgångens på/av-status. Dessa finns inte som fält i `FlashParams_t` överhuvudtaget — det är inte en fråga om att de "inte hunnit sparas", de skrivs aldrig. Vid varje omstart återgår båda till sina hårdkodade startvärden i `gpio.c`/`Laser_Control_Init()` (5V = PÅ, laser = AV), oavsett vad de senast var inställda till.

---

## 3. När sparning triggas

Två vägar in till samma `Params_Save()`-funktion:

**A. Debounce-baserad autosparning** (`Params_Process()`, körs varje huvudloop-varv):
- `Params_MarkDirty()` anropas idag bara från två ställen: `set crystal.wavelength` och `set laser.power`. Ingen annan CLI-kommando markerar tillståndet som "dirty".
- Varje anrop till `Params_MarkDirty()` sätter en tidsstämpel. Om ett nytt anrop kommer innan 20 sekunder har gått, nollställs tidsstämpeln — nedräkningen börjar om.
- Först när 20 sekunder har passerat **utan** ytterligare ändring av våglängd eller effekt sker en sparning — en gång, inte upprepat.

**B. Explicit sparning** (CLI-kommandot `save`):
- Anropar `Params_Save()` direkt, oavsett "dirty"-flaggan eller timer.
- Skriver exakt samma fullständiga fältlista som autosparningen (se tabellen ovan) — det finns ingen skillnad i **vad** som sparas mellan de två vägarna, bara **när** det sker.

Att ändra t.ex. `crystal.kp` (en PID-parameter) triggar alltså ingen egen autosparning — men värdet skrivs ändå med nästa gång en sparning väl sker (utlöst av en våglängds- eller effektändring, eller av `save`), eftersom hela fältlistan alltid skrivs tillsammans.

---

## 4. Flash-slitage och wear-leveling

### 4.1 Den underliggande begränsningen

STM32G4:s interna Flash tål ~10 000 raderingscykler per sida (2 kB) innan den börjar bli opålitlig (databladsvärde, se även §7.1 i mjukvaruspecifikationen). Det är specifikt **raderingar**, inte skrivningar i vardaglig mening, som sliter: att skriva till redan raderad (blank) Flash kräver ingen radering, men att skriva över befintlig data med nytt innehåll kräver en full sidradering först.

### 4.2 Hur implementationen undviker att slita ut sidan snabbt

`params.c` behandlar en enda Flash-sida (`PARAMS_PAGE = 63`, bas-adress `PARAMS_BASE = 0x0801F800`) som en roterande logg med **25 platser** (`PARAMS_MAX = 25`):

- Varje `Params_Save()` skriver till nästa **lediga** plats i loggen, taggad med ett stigande sekvensnummer — ingen radering behövs, eftersom den platsen redan är blank.
- `Params_Load()` vid uppstart skannar samtliga 25 platser och väljer den med högst sekvensnummer (senaste giltiga posten, verifierad med CRC32).
- Först när alla 25 platser är fyllda raderas hela sidan och loggen börjar om från plats 0.

Effekten: sidans budget på ~10 000 raderingar begränsar inte antalet sparningar till 10 000 — den begränsar antalet **sidraderingar**, och varje radering täcker nu 25 sparningar.

```
Effektiv livslängd ≈ 10 000 raderingar × 25 sparningar/radering
                    = 250 000 sparningar totalt
```

### 4.3 Vad det betyder i praktiken

Kombinerat med 20-sekunders-debouncen (som förhindrar att sparningar triggas snabbare än var 20:e sekund, även vid kontinuerlig vridning på ratten):

```
Värsta tänkbara fall: en sparning var 20:e sekund, non-stop
  = 3 sparningar/minut × 60 × 24 = 4 320 sparningar/dygn
  250 000 / 4 320 ≈ 58 dygn kontinuerlig, ihållande användning

Realistisk användning (enstaka justeringar, inte non-stop):
  betydligt längre — sannolikt år till decennier
```

---

## 5. Om laser på/av eller 5V-status ska bli beständiga

Om det i framtiden blir önskvärt att laserns på/av-läge eller 5V-utgångens läge ska överleva en omstart (istället för att alltid återgå till hårdkodade standardvärden), krävs:

1. Nya fält i `FlashParams_t` (`Core/Inc/app/params.h`) för respektive tillstånd.
2. `Params_Save()`/`Params_Load()` uppdateras att skriva/läsa de nya fälten.
3. `Params_MarkDirty()` anropas från `laser on`/`laser off`- och `5v on`/`5v off`-hanteringen i `cli.c`, annars sparas ändringen aldrig ens vid nästa debounce-tillfälle.

Inte implementerat i nuläget — flaggat här som en möjlig framtida ändring, inte en bugg.
