# Firmware Updater — Handoff to Windows

**Status:** Code written and reviewed on paper against the `libwdi.h` API. **Never compiled. Never run. Never tested against real hardware.** This was written in a Linux sandbox with no Windows machine and no STM32 hardware available — everything below needs to happen on a real Windows box with the actual board.

If you're a Claude Code instance picking this up: read this whole file before touching anything. It tells you exactly what's solid, what's guessed, and what to verify first.

---

## 1. What this is and why it exists

Laserfabriken's software spec (`Docs/mjukvaruspecifikation.md`, §3.6) requires firmware updates to work **without installing a proprietary driver from the MCU manufacturer (ST)**. The STM32G431 has a built-in ROM DFU bootloader reachable over USB, but on Windows that requires a WinUSB driver binding before any tool can talk to it — normally done by hand with **Zadig**.

Making the customer run Zadig manually (pick the right device from a dropdown, select WinUSB, click Install) works but is not a great end-customer experience. This tool automates that exact step programmatically using **libwdi** — the open-source (LGPL v3) library Zadig itself is built on — so the customer just runs one `.exe` instead.

Full background/discussion of the alternatives considered (just document Zadig manually, just use STM32CubeProgrammer + accept ST's driver, build this tool) is in the chat history that produced this — ask the user if you need that context restated. Short version: this tool is the "nice" option, not the only viable one. If it turns out to be a lot of pain to get working, falling back to "document the manual Zadig steps for v1" is a completely legitimate alternative — say so if you hit a wall.

## 2. What exists right now

- `main.c` — the actual tool. Flow:
  1. `wdi_create_list()` — enumerate USB devices
  2. Walk the linked list looking for VID=`0x0483`, PID=`0xDF11` (ST's fixed identifier for "STM32 ROM DFU bootloader", same across all STM32 parts)
  3. `wdi_prepare_driver()` + `wdi_install_driver()` — bind WinUSB to that device (this is the automated Zadig-equivalent)
  4. `system()` call out to a bundled `dfu-util.exe` to actually flash the `.bin` file passed on the command line

That's it — no other files exist yet (no build scripts, no vendored libwdi, no bundled dfu-util.exe).

## 3. What was verified vs. guessed

**Verified** (fetched directly from the real `libwdi.h` on GitHub, not from memory):
- `struct wdi_device_info` field names/order
- `struct wdi_options_create_list`, `wdi_options_prepare_driver`, `wdi_options_install_driver` field names
- `enum wdi_driver_type` (confirmed `WDI_WINUSB` is a real value)
- Function signatures for `wdi_create_list`, `wdi_destroy_list`, `wdi_prepare_driver`, `wdi_install_driver`, `wdi_strerror`
- Error code convention (`WDI_SUCCESS = 0`, negative on error)

**NOT verified — first things to check when you have a real environment:**
- Whether zero-initializing `wdi_options_prepare_driver` (leaving `disable_cat`, `disable_signing`, `use_wcid_driver`, `external_inf` all at 0/FALSE) actually produces an installable driver package on a modern Windows 10/11 machine with default security settings, or whether unsigned driver installation gets blocked and `disable_signing`/some cert-handling step is actually required. This is the single biggest risk area — look at `examples/wdi-simple.c` in the libwdi repo for how the reference tool handles this.
- Whether `pending_install_timeout = 0` in `wdi_options_install_driver` means "use a sane library default" or "time out immediately" — check the header comments/wiki, adjust if needed.
- The exact `dfu-util` command-line flags for this specific board/bootloader (`-a 0 -s 0x08000000:leave`). This is the standard pattern for STM32 internal-flash DFU, but hasn't been run against this board. Compare against how `Docs/mjukvaruspecifikation.md` §3.6 or any existing dfu-util usage notes describe it, and just try it.
- Whether the STM32G431's DFU bootloader needs `:mass-erase` or `:force` variants on the address spec, or whether plain `:leave` is sufficient.

## 4. What you need to actually build and test this

1. **Get libwdi**: clone https://github.com/pbatard/libwdi — either build it from source (needs its own toolchain setup, see their README) or grab a prebuilt release from the GitHub releases page. You need `libwdi.h` and a linkable `.lib`/`.dll` for whatever compiler you use.
2. **Pick a compiler**: MSVC (Visual Studio) is what libwdi's own examples target most directly. MinGW is also supported per the repo. Either is fine — just be consistent with whichever `.lib` format you get.
3. **Get dfu-util.exe**: https://dfu-util.sourceforge.net/releases/ — grab the Windows binary, drop it next to the built `.exe` (or bundle it in the final installer).
4. **Build `main.c`** against libwdi. Expect a `Requires -DWDI_STATIC` or similar depending on whether you link static or dynamic — check libwdi's own build docs.
5. **Get a real STM32G431 firmware `.bin`**: build the actual Laserfabriken firmware from this repo (see the top-level project — it's a normal STM32CubeIDE/CMake project) to get a real `.bin` to test flashing with, or use any throwaway STM32 DFU-capable board first just to prove the driver-install half works before risking the real hardware.
6. **Put the board in DFU mode** (BOOT0 held high at reset/power-up — check the schematic/`gpio.c` for exact boot pin behavior on this board) and run the tool.

## 5. Testing checklist

- [ ] Tool builds without errors against real `libwdi.h`
- [ ] Tool finds the STM32 DFU device (VID:PID match) when plugged in and in DFU mode
- [ ] Tool finds it correctly even if some other driver is already bound (test on a machine that's never run Zadig for this device, and one that has)
- [ ] WinUSB installation succeeds without a signing/certificate error blocking it
- [ ] UAC prompt appears and, once accepted, install actually completes (check Device Manager afterward — device should show under "Universal Serial Bus devices" as WinUSB-bound, not "Unknown device")
- [ ] `dfu-util.exe` successfully talks to the now-WinUSB-bound device
- [ ] Actual flash succeeds and the board boots the new firmware afterward
- [ ] Re-running the tool on a machine where the driver is *already* installed doesn't error out or behave badly (idempotency)
- [ ] Test on a clean Windows 10 and Windows 11 VM if possible — driver signing enforcement differs slightly between versions

## 6. If this turns out to be more trouble than it's worth

Don't burn excessive time fighting driver-signing edge cases. If it's not working smoothly after a reasonable attempt, tell the user directly and point back at the two documented fallback options:
1. Document the manual Zadig process for end users (see chat history / `Docs/ugn_pinout_and_bringup_status.md` context — ask the user if that doc needs a pointer added here)
2. Just ship ST's official STM32CubeProgrammer + its bundled driver and drop the "no proprietary driver" constraint — ask the user whether that constraint is actually a hard requirement from Laserfabriken or just a stated preference, since that changes the cost/benefit here a lot.

## 7. Reference links

- libwdi: https://github.com/pbatard/libwdi
- libwdi usage wiki: https://github.com/pbatard/libwdi/wiki/Usage
- Reference example this code is modeled on: https://github.com/pbatard/libwdi/blob/master/examples/wdi-simple.c
- dfu-util: https://dfu-util.sourceforge.net/
- STM32 DFU VID:PID and general Zadig/dfu-util Windows pain points: https://www.hanselman.com/blog/how-to-fix-dfuutil-stm-winusb-zadig-bootloaders-and-other-firmware-flashing-issues-on-windows
