# Firmware Updater — Handoff to Windows

**Status:** Code written and reviewed on paper against the `libwdi.h` API. **Never compiled. Never run. Never tested against real hardware.** This was written in a Linux sandbox with no Windows machine available — everything below needs to happen on a real Windows box.

If you're a Claude Code instance picking this up: read this whole file before touching anything. It tells you exactly what's solid, what's guessed, and what to verify first.

**Important — there are two targets, and you can only test one of them right now:**

The tool supports `--target=dfu` and `--target=stlink`. They share the exact same libwdi driver-binding logic and differ only in VID:PID and the external flashing tool. This split exists because **the user has a Nucleo F401RE board available now, but not yet the real G431 UGN board.**

- `--target=stlink` → talks to the Nucleo's onboard ST-Link/V2-1 debug probe (VID:PID `0483:374B`), flashes via `st-flash.exe`. **Testable today** against the Nucleo.
- `--target=dfu` → talks to the STM32's own native USB DFU bootloader (VID:PID `0483:DF11`), flashes via `dfu-util.exe`. **This is the actual production path for end customers** — the G431 UGN board has no ST-Link chip on it, so DFU is the only way it will ever be flashed in the field. **Cannot be tested until the G431 board exists.**

Do not treat a successful `stlink` test as proof the tool works end-to-end. It only proves the libwdi/WinUSB driver-binding automation works in general. The `dfu` path talks to different bootloader firmware (ST's ROM DFU, not ST-Link's protocol) and has its own untested command syntax (see §3). **Both paths need to be exercised separately before this is considered done.**

Why a Nucleo can't be used to test the `dfu` path directly: Nucleo boards have exactly one USB connector, and it's wired to the onboard ST-Link chip, not to the target MCU's own USB pins (PA11/PA12 on the F401). So a Nucleo will never enumerate as `0483:DF11` over its normal USB cable — there's no way to reach the F401's native DFU bootloader without extra wiring most Nucleo boards don't expose by default. This is a hardware topology fact, not a tool bug.

---

## 1. What this is and why it exists

Laserfabriken's software spec (`Docs/mjukvaruspecifikation.md`, §3.6) requires firmware updates to work **without installing a proprietary driver from the MCU manufacturer (ST)**. The STM32G431 has a built-in ROM DFU bootloader reachable over USB, but on Windows that requires a WinUSB driver binding before any tool can talk to it — normally done by hand with **Zadig**.

Making the customer run Zadig manually (pick the right device from a dropdown, select WinUSB, click Install) works but is not a great end-customer experience. This tool automates that exact step programmatically using **libwdi** — the open-source (LGPL v3) library Zadig itself is built on — so the customer just runs one `.exe` instead.

Full background/discussion of the alternatives considered (just document Zadig manually, just use STM32CubeProgrammer + accept ST's driver, build this tool) is in the chat history that produced this — ask the user if you need that context restated. Short version: this tool is the "nice" option, not the only viable one. If it turns out to be a lot of pain to get working, falling back to "document the manual Zadig steps for v1" is a completely legitimate alternative — say so if you hit a wall.

## 2. What exists right now

- `main.c` — the actual tool. Usage: `main.exe [--target=dfu|stlink] <firmware.bin>` (defaults to `dfu` if the flag is omitted). Flow:
  1. Parse `--target=`, look up VID:PID + flash command for that target (`TARGET_CONFIGS[]` near the top of the file)
  2. `wdi_create_list()` — enumerate USB devices
  3. Walk the linked list looking for a VID:PID match for the selected target
  4. `wdi_prepare_driver()` + `wdi_install_driver()` — bind WinUSB to that device (this is the automated Zadig-equivalent; identical code path for both targets)
  5. `system()` call out to the target's bundled flashing tool (`dfu-util.exe` or `st-flash.exe`) to actually flash the `.bin` file passed on the command line

That's it — no other files exist yet (no build scripts, no vendored libwdi, no bundled dfu-util.exe or st-flash.exe).

## 3. What was verified vs. guessed

**Verified** (fetched directly from the real `libwdi.h` on GitHub, not from memory):
- `struct wdi_device_info` field names/order
- `struct wdi_options_create_list`, `wdi_options_prepare_driver`, `wdi_options_install_driver` field names
- `enum wdi_driver_type` (confirmed `WDI_WINUSB` is a real value)
- Function signatures for `wdi_create_list`, `wdi_destroy_list`, `wdi_prepare_driver`, `wdi_install_driver`, `wdi_strerror`
- Error code convention (`WDI_SUCCESS = 0`, negative on error)

**NOT verified — first things to check when you have a real environment:**
- Whether zero-initializing `wdi_options_prepare_driver` (leaving `disable_cat`, `disable_signing`, `use_wcid_driver`, `external_inf` all at 0/FALSE) actually produces an installable driver package on a modern Windows 10/11 machine with default security settings, or whether unsigned driver installation gets blocked and `disable_signing`/some cert-handling step is actually required. This is the single biggest risk area — look at `examples/wdi-simple.c` in the libwdi repo for how the reference tool handles this. This applies identically to both targets since it's shared code — verify it once, against whichever target you can test first (`stlink`).
- Whether `pending_install_timeout = 0` in `wdi_options_install_driver` means "use a sane library default" or "time out immediately" — check the header comments/wiki, adjust if needed.
- The ST-Link/V2-1 PID (`0x374B`) — this came from a web search, not from libwdi's own source or a hardware capture. Cross-check it against Device Manager on the actual Nucleo (look at the USB device's hardware ID string, e.g. `USB\VID_0483&PID_374B`) before trusting it. Older standalone ST-Link/V2 units reportedly use `0x3748` instead — if `374B` doesn't match, try that.
- The exact `dfu-util` command-line flags for the G431's bootloader (`-a 0 -s 0x08000000:leave`). This is the standard pattern for STM32 internal-flash DFU, but hasn't been run against this board — and can't be, until the G431 exists. Compare against how `Docs/mjukvaruspecifikation.md` §3.6 or any existing dfu-util usage notes describe it, and just try it once the board is available.
- Whether the STM32G431's DFU bootloader needs `:mass-erase` or `:force` variants on the address spec, or whether plain `:leave` is sufficient.
- The exact `st-flash` command-line syntax (`st-flash write <file> 0x08000000`) — this is the general documented form from the stlink-org README, but hasn't been run. `st-flash`'s CLI has had minor syntax changes across releases; check the version you actually download.

## 4. What you need to actually build and test this

1. **Get libwdi**: clone https://github.com/pbatard/libwdi — either build it from source (needs its own toolchain setup, see their README) or grab a prebuilt release from the GitHub releases page. You need `libwdi.h` and a linkable `.lib`/`.dll` for whatever compiler you use.
2. **Pick a compiler**: MSVC (Visual Studio) is what libwdi's own examples target most directly. MinGW is also supported per the repo. Either is fine — just be consistent with whichever `.lib` format you get.
3. **Get the flashing tools**:
   - `dfu-util.exe`: https://dfu-util.sourceforge.net/releases/
   - `st-flash.exe`: https://github.com/stlink-org/stlink/releases (part of the stlink-org/stlink toolset, BSD-3 licensed)
   Drop both next to the built `.exe` (or bundle them in the final installer).
4. **Build `main.c`** against libwdi. Expect a `Requires -DWDI_STATIC` or similar depending on whether you link static or dynamic — check libwdi's own build docs.
5. **Start with `--target=stlink` against the Nucleo F401RE** (the user has this board now): this proves out the shared libwdi/WinUSB driver-binding logic without needing the G431 board at all. Use any throwaway firmware `.bin` for the F401 — the point is verifying the driver-install + flash mechanics, not the firmware content.
6. **Later, once the G431 UGN board exists, test `--target=dfu` separately**: build the actual Laserfabriken G431 firmware from this repo to get a real `.bin`, put the board in DFU mode (BOOT0 held high at reset/power-up — check the schematic/`gpio.c` for exact boot pin behavior on this board), and run the tool with `--target=dfu`. **Do not assume success on `stlink` means this will also work** — different VID:PID, different bootloader protocol, different flashing tool, all unverified independently (see §3).

## 5. Testing checklist

**Phase 1 — `--target=stlink` against the Nucleo (testable now):**
- [ ] Tool builds without errors against real `libwdi.h`
- [ ] Tool finds the ST-Link device (VID:PID `0483:374B`) when the Nucleo is plugged in
- [ ] Tool finds it correctly even if some other driver is already bound (test on a machine that's never run Zadig for this device, and one that has)
- [ ] WinUSB installation succeeds without a signing/certificate error blocking it
- [ ] UAC prompt appears and, once accepted, install actually completes (check Device Manager afterward — device should show under "Universal Serial Bus devices" as WinUSB-bound, not "Unknown device" and not still under "ST-Link" drivers)
- [ ] `st-flash.exe` successfully talks to the now-WinUSB-bound device
- [ ] Actual flash succeeds and the Nucleo boots the new firmware afterward
- [ ] Re-running the tool on a machine where the driver is *already* installed doesn't error out or behave badly (idempotency)
- [ ] Test on a clean Windows 10 and Windows 11 VM if possible — driver signing enforcement differs slightly between versions

**Phase 2 — `--target=dfu` against the real G431 UGN board (blocked until that board exists):**
- [ ] Tool finds the STM32 DFU device (VID:PID `0483:DF11`) when plugged in and in DFU mode
- [ ] `dfu-util.exe` successfully talks to the WinUSB-bound device
- [ ] Actual flash succeeds and the G431 board boots the new firmware afterward
- [ ] Confirm this is genuinely the production customer flow described in `Docs/mjukvaruspecifikation.md` §3.6 — no ST-Link chip involved, no other driver installed beforehand

Do not consider this tool "done" until both phases pass. Phase 1 alone only proves the automation mechanism works in principle.

## 6. If this turns out to be more trouble than it's worth

Don't burn excessive time fighting driver-signing edge cases. If it's not working smoothly after a reasonable attempt, tell the user directly and point back at the two documented fallback options:
1. Document the manual Zadig process for end users (see chat history / `Docs/ugn_pinout_and_bringup_status.md` context — ask the user if that doc needs a pointer added here)
2. Just ship ST's official STM32CubeProgrammer + its bundled driver and drop the "no proprietary driver" constraint — ask the user whether that constraint is actually a hard requirement from Laserfabriken or just a stated preference, since that changes the cost/benefit here a lot.

## 7. Reference links

- libwdi: https://github.com/pbatard/libwdi
- libwdi usage wiki: https://github.com/pbatard/libwdi/wiki/Usage
- Reference example this code is modeled on: https://github.com/pbatard/libwdi/blob/master/examples/wdi-simple.c
- dfu-util: https://dfu-util.sourceforge.net/
- stlink-org/stlink (st-flash, BSD-3): https://github.com/stlink-org/stlink
- STM32 DFU VID:PID and general Zadig/dfu-util Windows pain points: https://www.hanselman.com/blog/how-to-fix-dfuutil-stm-winusb-zadig-bootloaders-and-other-firmware-flashing-issues-on-windows
