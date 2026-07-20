# Firmware Updater — Handoff

**Status (2026-07-15): Phase 1 PASSED on real hardware.** `firmware-updater.exe --target=stlink Laserfabriken_v1.bin` successfully flashed real firmware to a real Nucleo F401RE — `st-flash` reported `Flash written and verified! jolly good!`. This followed a same-day architecture change (see §1) away from an earlier automated-driver-install design that turned out to be solving a problem the software spec didn't actually ask for.

If you're a Claude Code instance picking this up: read this whole file before touching anything.

**`dist/` vs `build/`**: `dist/firmware-updater.exe` + `dist/dfu-util.exe` are git-tracked, prebuilt binaries — someone who just wants to *use* this tool pulls the repo and runs those directly, no compiler needed. `build/` (gitignored) is the throwaway output of `.\build.ps1`, for actual development. If you change `main.c`, rebuild and update `dist/` with `.\build.ps1 -Publish`, then `git add`/commit `dist/` — otherwise `dist/` silently goes stale relative to `main.c`.

---

## 1. What this is (current design, 2026-07-15)

Per `Docs/mjukvaruspecifikation.md` §3.6, the actual spec requirement is:

> På Windows används WinUSB — en standard Microsoft-drivrutin (**installeras en gång via Zadig**, öppen källkod). Flashning sker med `dfu-util`.

("WinUSB is used — installed **once** via Zadig. Flashing is done with `dfu-util`.") A one-time manual Zadig step is the spec'd design, not a fallback.

`main.c` is accordingly a thin wrapper: it takes `--target=dfu|stlink` and a `.bin` path, and shells out to the matching bundled flashing tool (`dfu-util.exe` or `st-flash.exe`). It assumes the WinUSB driver binding already exists (done once, by the customer, via Zadig, at first setup — not automated by this tool). No USB library dependency, no libwdi, no driver-install code.

```
main.exe [--target=dfu|stlink] <firmware.bin>
  --target=dfu     STM32 DFU bootloader (production board, G431 UGN) [default]
  --target=stlink  ST-Link/V2-1 (Nucleo dev board, for testing)
```

**Two targets**, same reason as before: the user has a Nucleo F401RE available now but not yet the real G431 UGN board.
- `--target=stlink` → ST-Link/V2-1 debug probe (VID:PID `0483:374B`), flashed via `st-flash.exe`. **Tested and passing today.**
- `--target=dfu` → STM32's own ROM DFU bootloader (VID:PID `0483:DF11`), flashed via `dfu-util.exe`. **The actual production path** — the G431 UGN board has no ST-Link chip, so this is the only way it's ever flashed in the field. **Still cannot be tested until the G431 board exists** (a Nucleo's USB port is wired to its onboard ST-Link, not the target MCU's own USB pins — there's no way to reach `0483:DF11` on a Nucleo without extra wiring).

A successful `stlink` test does **not** prove the `dfu` path works — different VID:PID, different bootloader protocol, different flashing tool, different (unverified) `dfu-util` command syntax. Both need independent verification.

## 2. Why this isn't the libwdi/auto-driver-install tool from earlier today

An earlier version of this tool used **libwdi** (the library Zadig itself is built on) to automate the WinUSB binding step programmatically, so the customer wouldn't need to run Zadig manually at all. That turned into a large detour and was abandoned. Keeping the findings here in case anyone revisits it:

- Building libwdi from source needs a real toolchain (WDK redistributable + libusb0/libusbK + MSYS2 autotools) — doable, but heavy for what should've been a simple tool. See git history for the `build.ps1` version that did this, if you want to resurrect it.
- **It hit a real, hard wall**: on a stock Windows 10 Home install with no custom Group Policy, `wdi_install_driver()` failed with `System policy has been modified to reject unsigned drivers`. `wdi_prepare_driver()` logged `No .cat file generated (missing elevated privileges)` because it ran before elevation — libwdi's own reference tools (`zadig.exe`, via `common_controls_and_elevation.manifest`) request `requireAdministrator` for the *whole process* for exactly this reason, which `main.c` didn't do. Fixing this properly means either embedding a `requireAdministrator` manifest **and** generating/embedding/trusting a real signing certificate (`wdi_install_trusted_certificate()`), or accepting it may still fail on some customer machines regardless.
- Also found a real matching bug in that version: `wdi_create_list()` returns one entry per USB *interface*, not per device. Composite devices (the Nucleo's ST-Link/V2-1 exposes 3: debug, mass-storage, VCP, all sharing VID:PID `0483:374B`) need matching on interface number (`d->mi == 0`) too, or you can bind WinUSB to the wrong interface (confirmed on hardware: it grabbed "ST-Link mass storage" instead of "ST-Link Debug"). The reference `wdi-simple.c` matches on `vid + pid + mi + is_composite` together, never on VID:PID alone.
- **The actual test that mattered**: does the *plain, manual Zadig tool* work on this machine at all, independent of our automation attempt? Yes — confirmed on hardware. The user ran Zadig manually (`Options → List All Devices`, selected the `ST-Link Debug (Interface 0)` device, installed WinUSB), and it worked cleanly, no signing error. So the spec's one-time-Zadig plan is solid; only the "automate it away" enhancement was the problem.

Conclusion: the automation wasn't worth the risk/complexity it added, and wasn't something the spec asked for. Dropped it, went back to the spec's actual design.

## 3. Two real bugs found and fixed in the current (flash-only) `main.c`

1. **`system()` + a quoted executable path**: `main.c` resolves its own directory (`GetModuleFileNameA`) and builds an explicit path to the bundled tool (e.g. `"C:\...\st-flash.exe" write ...`) rather than relying on a bare `st-flash.exe` name — confirmed on hardware that a bare name is **not** reliably found via `system()`/`cmd /c`, even from the tool's own directory. But a command line that *starts* with a quoted path and isn't *entirely* wrapped in an outer quote pair gets mis-parsed by `cmd.exe` (confirmed: `"C:\Users\Ray Cali\...\st-flash.exe" write "file.bin" 0x08000000` failed with `'C:\Users\Ray' is not recognized...`, truncating at the first space). Fix: wrap the whole command line in one more pair of quotes before calling `system()`. This is a documented `cmd.exe` quirk, not specific to this tool — search "cmd.exe quoted path first argument" if you hit it elsewhere.
2. (Not a code bug, but a packaging gap — see §5.)

## 4. What was verified vs. still guessed

**Verified on real hardware (2026-07-15):**
- `--target=stlink` end to end: device found, driver already bound (via one-time Zadig), `st-flash.exe` located and invoked correctly, flash + verify succeeded
- ST-Link/V2-1 PID is `0x374B` on this specific Nucleo (confirmed via Device Manager) — the `0x3748` alternative mentioned in older notes did not apply here
- `st-flash write <file> 0x08000000` is the correct invocation for v1.8.0

**Still not verified — first things to check when the G431 board exists:**
- The exact `dfu-util` command-line flags (`-a 0 -s 0x08000000:leave`) — standard pattern for STM32 internal-flash DFU, but never run against real hardware
- Whether the STM32G431's DFU bootloader needs `:mass-erase` or `:force` variants, or plain `:leave` is sufficient
- Whether the DFU path also needs the interface/composite-matching care noted in §2 — the ROM DFU bootloader is usually single-interface, but don't assume

## 5. Packaging gaps — not yet solved, need solving before shipping to a customer

- **`dfu-util.exe` is now bundled, and `build.ps1` fetches it automatically (2026-07-15)** — using **`dfu-util-static.exe`** from https://dfu-util.sourceforge.net/releases/ (`dfu-util-0.9-win64.zip`), renamed to `dfu-util.exe` in `build/`. Deliberately picked the *static* build over the regular `dfu-util.exe` in that zip: confirmed on hardware that the regular one needs `libusb-1.0.dll` next to it (fails with `STATUS_DLL_NOT_FOUND` otherwise, same class of problem as `st-flash`'s dependency in the point below), while `dfu-util-static.exe` has **zero** runtime dependencies — nothing else to bundle, nothing to forget. Confirmed working end-to-end through `firmware-updater.exe --target=dfu`: correctly found, correctly invoked, correctly reports `No DFU capable USB device available` (expected — no G431 board exists yet to test against for real). Note: it also warned `Invalid DFU suffix signature` on our test `.bin` — a DFU suffix is an optional footer (CRC + vendor/product ID) some `.bin` files have; harmless for now but the warning says it becomes mandatory in a future dfu-util release, worth revisiting once real G431 firmware `.bin` files are being produced.

  **This closes the "how does this work on another laptop" gap for the customer-facing path**: `build/` is entirely gitignored (nothing built is committed), but `build.ps1` now downloads `dfu-util.exe` itself if it's missing, alongside compiling `main.c`. So on any machine: `git pull` + `.\build.ps1` reproduces the complete working toolset (`firmware-updater.exe` + `dfu-util.exe`) with zero manual steps — verified by wiping `dfu-util.exe` and re-running the script (2026-07-15). Only requirement is MSYS2 installed (for `gcc`) and internet access for the one-time download. `st-flash.exe`/`--target=stlink` remains a manual dev-only setup (below) — deliberately not scripted, since it's not part of what ships to customers.
- **`st-flash.exe` needs `libstlink.dll` and `libusb-1.0.dll` next to it** (from the stlink-org release zip and MSYS2's `mingw-w64-x86_64-libusb` package respectively — `st-flash.exe --version` fails with a missing-DLL error otherwise).
- **`st-flash.exe`'s chip database is hardcoded to `C:\Program Files (x86)\stlink\config\chips`**, not looked up relative to the exe or overridable via a CLI flag or env var. Without it, flashing fails with `unknown chip id!` / `Failed to parse flash type`, even though the release zip ships the right chip file (`F401xD_xE.chip` for this board). Confirmed on hardware (2026-07-15) that `--flash=<size>` does **not** work around this — `st-flash` still needs the chip-specific "flash type" (sector layout / write-loader variant) from that config, not just the size.

  **This is dev-tooling-only, not a customer-facing problem**: `--target=stlink`/`st-flash` is only ever used for testing this tool against a Nucleo during development (see `Docs/firmware_update_guide.md`'s "For developers" section) — the real customer path (`--target=dfu`/`dfu-util`) doesn't touch `st-flash` at all and may not have this issue (untested — check once `dfu-util` is bundled). So this doesn't need an installer or a patched `st-flash` build; it needs a one-time setup step on whatever machine you're using to test the `stlink` path, same category as the one-time Zadig step:

  1. Download the stlink-org Windows release matching what `build.ps1`-adjacent testing used: https://github.com/stlink-org/stlink/releases (`stlink-<version>-win32.zip`)
  2. Copy its `Program Files (x86)\stlink\` folder (from inside the zip) to the real `C:\Program Files (x86)\stlink\` — needs admin. From an elevated PowerShell:
     ```powershell
     Copy-Item -Path "<extracted-zip>\Program Files (x86)\stlink" -Destination "C:\Program Files (x86)\" -Recurse -Force
     ```
  3. Copy `st-flash.exe` + `libstlink.dll` (from the zip's `bin\` folder) next to `firmware-updater.exe` for testing.
  4. Copy `libusb-1.0.dll` next to it too. If it's not already at `C:\msys64\mingw64\bin\libusb-1.0.dll`, install it first:
     ```
     pacman -S mingw-w64-x86_64-libusb
     ```

  **Where these three files actually come from — they're not all from the same place, worth knowing before bundling anything for real:**
  - `st-flash.exe` and `libstlink.dll` are a matched pair from the same source: the stlink-org release zip (`bin\` folder inside it). `st-flash.exe` won't run without `libstlink.dll` sitting next to it — Windows loads it at startup, it's not baked into the `.exe`.
  - `libusb-1.0.dll` is unrelated and did **not** come in that zip at all. `st-flash.exe` depends on it too (fails immediately with `libusb-1.0.dll: cannot open shared object file` without it), but it has to be sourced separately — here, from the MSYS2 toolchain already on this machine (`mingw-w64-x86_64-libusb` package).
  - **Implication for real packaging**: "download the stlink release and you're done" is not true — there's a hidden third-party dependency to also track down. `dfu-util.exe` (the actual customer-facing tool, not yet bundled at all) may have a similar hidden `.dll` dependency of its own — check for this when you get to bundling it, don't assume the download is self-contained just because `st-flash` wasn't.

  Do this once per dev machine you test `--target=stlink` on; not needed at all for anything customer-facing.
- **No installer/packaging step exists yet** for the actual customer-facing `--target=dfu` path — right now this is just a loose `.exe` plus loose tool binaries in a gitignored `build/` folder. Needs an actual distribution plan (single zip? installer? auto-download `dfu-util.exe` on first run?).
- **The one-time Zadig setup is now documented for customers**: `Docs/firmware_update_guide.md`. It has one open gap — putting the production G431 UGN board into DFU mode. Found the circuit in the actual UGN schematic (`laserfabriken-ugn-A0.pdf`, sheet `M3_STM32G431Rx`, 2026-07-15): `BOOT0` is pulled low by default via R19 (10kΩ to GND — normal boot), with a 2-pin header **J5** that connects `BOOT0` to 3.3V when bridged, forcing DFU mode on the next reset/power-on. **Unconfirmed**: J5 appears marked DNP (Do Not Populate) in the schematic, which would mean it doesn't physically exist on assembled boards — if so, there is currently **no customer-accessible way to enter DFU mode at all**, which would block the whole firmware-update feature, not just this guide. Needs confirmation against a real assembled board (or whoever owns the KiCad project) before either the guide or Phase 2 testing can proceed on this point.

## 6. Testing checklist

**Phase 1 — `--target=stlink` against the Nucleo:**
- [x] Tool builds without errors (2026-07-15)
- [x] Tool correctly reports a clean error when the device has no driver bound yet (2026-07-15, pre-Zadig)
- [x] One-time Zadig setup works on a stock Windows 10 Home machine, no signing error (2026-07-15)
- [x] Tool finds the ST-Link device (VID:PID `0483:374B`) once WinUSB is bound (2026-07-15)
- [x] `st-flash.exe` successfully talks to the device and flash + verify succeeds (2026-07-15)
- [ ] Confirm the Nucleo actually boots/runs the newly-flashed firmware (flash+verify passed; didn't separately confirm runtime behavior post-flash)
- [ ] Re-running the tool when the driver is *already* installed doesn't error out or behave badly (idempotency) — untested
- [ ] Test on a clean Windows 11 machine too — only tested on Windows 10 Home so far

**Phase 2 — `--target=dfu` against the real G431 UGN board (blocked until that board exists):**
- [ ] Tool finds the STM32 DFU device (VID:PID `0483:DF11`) when plugged in and in DFU mode
- [ ] One-time Zadig setup works for the DFU interface specifically (untested — only the ST-Link interface has been tried)
- [ ] `dfu-util.exe` successfully talks to the WinUSB-bound device
- [ ] Actual flash succeeds and the G431 board boots the new firmware afterward

Do not consider this "done" until Phase 2 passes and §5's packaging gaps are closed.

## 7. Reference links

- Zadig releases (for the one-time customer setup step): https://github.com/pbatard/libwdi/releases
- dfu-util: https://dfu-util.sourceforge.net/
- stlink-org/stlink (st-flash, BSD-3): https://github.com/stlink-org/stlink
- STM32 DFU VID:PID and general Zadig/dfu-util Windows pain points: https://www.hanselman.com/blog/how-to-fix-dfuutil-stm-winusb-zadig-bootloaders-and-other-firmware-flashing-issues-on-windows

## 8. New developer setup

Three separate things, in order — you may only need the first one:

### A. Just running the tool (flash a `.bin`), no code changes

Use `dist\firmware-updater.exe` + `dist\dfu-util.exe` directly (already git-tracked, already built) — see `Docs/firmware_update_guide.md`. No compiler, no MSYS2, nothing to install here. You still need the one-time Zadig step from **C** below against whatever device you're flashing.

### B. Changing `main.c` and rebuilding

This gets you a compiled `.exe`. It does **not** by itself let you test against real hardware — for that, also do **C**.

1. **Install MSYS2** — https://www.msys2.org/, default install path (`C:\msys64`, `build.ps1` hardcodes this). This is a real, from-scratch install; Windows doesn't ship it.
2. **Install the mingw64 GCC package.** Open "MSYS2 MINGW64" from the Start menu (not the plain "MSYS2" shortcut — that's a different environment) and run:
   ```
   pacman -S mingw-w64-x86_64-gcc
   ```
3. **Build**: from a normal PowerShell (not the MSYS2 shell) in this directory:
   ```powershell
   .\build.ps1
   ```
   This compiles `main.c` and downloads `dfu-util.exe` into `.\build\` (gitignored — this is your scratch area, not what ships).
4. **Test without hardware**: run it directly —
   ```powershell
   .\build\firmware-updater.exe
   ```
   Even with nothing plugged in, it should run cleanly and report a "device not found" style error from `dfu-util`/`st-flash` (see §4/§6 for what "working correctly" looks like at this stage). To test against a *real* device, see **C**.
5. **When you're done and want to actually ship the change**, rebuild and copy into `dist\` (which *is* git-tracked) in one step:
   ```powershell
   .\build.ps1 -Publish
   ```
   Then stage and commit it:
   ```powershell
   git add Tools/firmware-updater/dist
   git commit
   ```
   **Don't skip this step** — `dist\` is what a non-dev pulls and runs (see **A**); if you forget to publish, your change never actually ships even though it's sitting correctly in `build\` on your machine.

### C. Testing against a real device (either target)

**This is where Zadig comes in, and it applies to both targets, not just `stlink`.** Both `dfu-util` and `st-flash` talk to the device through `libusb`, which needs Windows' generic **WinUSB** driver bound to that specific device first — that one-time binding is exactly what Zadig does (see §2 for the full story on why it's a manual Zadig step and not automated).

Key thing to understand: this is a **per-device** requirement, not a per-target-type one. `--target=dfu` and `--target=stlink` talk to two entirely different USB devices (different VID:PID), so each needs its *own* one-time Zadig run — doing it for one doesn't help the other, and you only need to do it for whichever one you're actually about to test.

**If testing `--target=dfu`** (the real production path) — needs the G431 UGN board, which doesn't exist yet (§1). Blocked for now; when it does exist, run Zadig against the DFU device (`VID_0483&PID_DF11`) — same steps as `Docs/firmware_update_guide.md` walks a customer through, since it's the same device.

**If testing `--target=stlink`** (Nucleo, available now):
1. First, set up `st-flash.exe` itself (separate from Zadig) — follow §5's `st-flash.exe` bullet, a one-time per-dev-machine copy of its chip database.
2. Then run Zadig against the Nucleo's ST-Link interface — note this is `0483:374B`, and specifically **interface 0**, not the whole composite device (see §2 for why that distinction matters — matching on VID:PID alone once caused this tool to bind the wrong interface). Steps, as actually done and confirmed working on 2026-07-15:
   1. Download Zadig: https://github.com/pbatard/libwdi/releases (`zadig-2.9.exe` or newer)
   2. Plug in the Nucleo via its USB port (the one wired to the onboard ST-Link, i.e. the normal/only USB port on most Nucleo boards)
   3. Run Zadig — accept the UAC prompt
   4. **Options → List All Devices** (must be checked, or the target won't show up)
   5. In the device dropdown, find **ST-Link Debug (Interface 0)** — confirm via its hardware ID shown below the dropdown, which should read `USB\VID_0483&PID_374B&MI_00`. Don't pick the mass-storage or VCP entries under the same VID:PID (`MI_01`/`MI_02`) — wrong interface, won't work.
   6. Select **WinUSB** in the driver box, click **Install Driver**
   7. Confirm in Device Manager afterward: the "ST-Link Debug" entry should show Status `OK`, not `Error`

Either way, it's a one-time step per dev machine per device — you won't need to repeat it once it's done.

### If you're touching the driver-signing / auto-install problem again

Don't, unless something has fundamentally changed (e.g. the spec itself changes, or someone has a real code-signing certificate to embed). Read §2 first — it's a full writeup of why that path was tried and abandoned today, including the exact error and the reference-implementation detail (`requireAdministrator` manifest) that would be needed to even attempt it properly.
