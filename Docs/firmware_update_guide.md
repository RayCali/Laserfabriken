# Firmware Update Guide

This guide explains how to update the firmware on your Laserfabriken device from Windows, using only free/open-source tools (`dfu-util`, Zadig/WinUSB) — **no ST proprietary software (e.g. STM32CubeProgrammer) required**, and none of it needs installing beyond the one-time driver setup below. You only need to do the driver setup in Step 1 **once, ever** — after that, updating firmware is a single command (or drag-and-drop) each time.

> **Note for whoever finishes this doc:** Step 2a below (putting the board into DFU/firmware-update mode) is marked TODO — the physical procedure for the production G431 UGN board (button? jumper? something else?) isn't documented anywhere in this repo yet. Fill it in before sending this to an actual customer. See the note in that section.

## What you need

- **`firmware-updater.exe`** and **`dfu-util.exe`** — you should have received these together, in the same folder. `firmware-updater.exe` won't work without `dfu-util.exe` sitting right next to it.
- **The new firmware file** — a `.bin` file, e.g. `laserfabriken_v2.bin`, provided by Laserfabriken.
- **A USB cable** to connect your device to your computer.
- **Zadig** — a free, open-source tool, only needed for the one-time setup in Step 1. Download it from: https://github.com/pbatard/libwdi/releases (look for `zadig-2.6.exe`, or whatever the latest version is).

## Step 1 — One-time driver setup

You only need to do this once, ever, on a given computer. If you've already done this before (on this same computer), skip to Step 2.

1. **Put your device into firmware-update mode.**
   > ⚠️ **TODO — needs hardware confirmation before this guide goes to a customer.** Per the UGN schematic (`M3_STM32G431Rx` sheet), the STM32's `BOOT0` pin is pulled low by default via a 10kΩ resistor (R19) — normal boot. There's a 2-pin header, **J5**, that connects `BOOT0` to 3.3V when bridged, which forces the chip into DFU mode on the next power-on/reset. **However**, J5 appears marked DNP (Do Not Populate) in the schematic — meaning it may not exist as a physical, usable connector on real assembled boards. **Before writing real instructions here, confirm on an actual board whether J5 is populated.** If it isn't, there is currently no customer-accessible way to enter DFU mode at all, and that needs a hardware decision (populate J5, add a button, etc.) before this guide — or the firmware-update feature itself — can ship. If J5 turns out to be populated: the instruction will be "bridge the two pins of J5 with a jumper (or press and hold if it's actually a button) before plugging in the USB cable, then remove the jumper after the update completes."
2. Connect the device to your computer with the USB cable.
3. Download Zadig from the link above and run it. Windows will ask for permission to make changes to your computer (a UAC prompt) — click **Yes**.
4. In Zadig's menu bar, click **Options → List All Devices** and make sure it's checked. This makes sure the device shows up even though Windows doesn't recognize it yet.
5. In the dropdown near the top, find and select your Laserfabriken device. It will likely be listed by a generic name (e.g. "STM32 BOOTLOADER") rather than the product name — if you're not sure which entry it is, check that its ID (shown below the dropdown) starts with `USB\VID_0483&PID_DF11`.
6. In the box on the right (next to the green arrow), select **WinUSB**.
7. Click **Install Driver** (or **Replace Driver**). This can take a minute. When it finishes, you'll see a confirmation message.
8. You're done with setup. You can close Zadig.

## Step 2 — Flashing new firmware

Do this every time you have a new firmware file to install.

1. **Put your device into firmware-update mode** (same step as Step 1.1 above) and connect it via USB.
2. Flash the new firmware — either of these two ways works:
   - **Drag-and-drop**: drag the `.bin` file onto `firmware-updater.exe`'s icon and drop it. A black command-window will pop up and show progress.
   - **Command line**: open a Command Prompt in the folder containing `firmware-updater.exe`, and run:
     ```
     firmware-updater.exe laserfabriken_v2.bin
     ```
     (replace `laserfabriken_v2.bin` with the actual name of your firmware file)
3. Wait for the message **"Done! The device is restarting with the new firmware."** Then press Enter to close the window.

That's it — your device is now running the new firmware.

## Troubleshooting

**"Flash command failed" / it looks like it can't find the device:**
- Make sure the device is actually in firmware-update mode (Step 2.1 / Step 1.1) — if it booted normally instead, the computer won't be able to talk to it this way.
- Make sure you completed the one-time Zadig setup (Step 1) on this computer already. If you're not sure, just run through Step 1 again — it's safe to repeat.
- Try a different USB cable or port — some cables are power-only and don't carry data.

**Zadig doesn't show your device, even with "List All Devices" checked:**
- Double check the device is in firmware-update mode and the USB cable is connected before opening Zadig. If it wasn't, close Zadig, fix that, then reopen Zadig.

**Still stuck:**
- Contact Laserfabriken support with a description of what step you're on and any error message you see.

## For developers / internal testing only

`firmware-updater.exe` also supports `--target=stlink`, for flashing over an ST-Link debug probe instead of the production DFU path — not relevant for customers, only for testing this tool during development.

### A. Just running the tool (no code changes)

`dist/firmware-updater.exe` and `dist/dfu-util.exe` are git-tracked, pre-built binaries — pull the repo and use them directly, no compiler needed. You still need the one-time Zadig setup (Step 1 above) against whatever device you're flashing.

### B. Changing `main.c` and rebuilding

1. Install MSYS2 (https://www.msys2.org/, default path `C:\msys64` — `build.ps1` hardcodes this).
2. Open **"MSYS2 MINGW64"** from the Start menu (not the plain "MSYS2" shortcut — different environment) and run:
   ```
   pacman -S mingw-w64-x86_64-gcc
   ```
3. Build, from a normal PowerShell (not the MSYS2 shell) in this directory:
   ```powershell
   .\build.ps1
   ```
   Compiles `main.c` and downloads `dfu-util.exe` into `.\build\` (gitignored — your scratch area, not what ships).
4. Test without hardware — any `.bin` path works, even a fake/empty one:
   ```powershell
   .\build\firmware-updater.exe --target=stlink C:\path\to\some.bin
   ```
   With nothing plugged in, it should still run cleanly and report a "device not found"-style error from `dfu-util`/`st-flash`.
5. When ready to ship the change, publish to `dist\` (which *is* git-tracked) and commit:
   ```powershell
   .\build.ps1 -Publish
   git add Tools/firmware-updater/dist
   git commit
   ```
   Don't skip this — `dist\` is what a non-developer actually pulls and runs (option A above). Forgetting to publish means the change never ships, even though it built fine locally.

### C. Testing against real hardware

Both `dfu-util` and `st-flash` talk to their device through `libusb`, which needs Windows' WinUSB driver bound first — the same one-time Zadig step as Step 1 above, but done **per USB device**. `--target=dfu` and `--target=stlink` are two different devices (different VID:PID) — doing Zadig for one does not cover the other.

**`--target=stlink` — confirmed working on real hardware, including the actual G431 UGN card:**

A standalone external ST-Link probe connects to the UGN board's own SWD pads and presents itself to Windows with the standard ST-Link VID:PID. The UGN schematic (`laserfabriken-ugn.kicad_sch`, sheet 1) shows a dedicated 4-pad programming header, **`ProgPAD` (P1)**, broken out directly from the STM32's SWD pins: `SWDIO`, `SWDCLK`, `3V3`, `GND`. That's what the probe connects to on a real card — no DFU, no USB bootloader mode, no BOOT0/J5 involved at all for this path.

1. Set up `st-flash.exe` itself, separately from Zadig — it needs `libstlink.dll` and `libusb-1.0.dll` sitting next to it, plus its chip database installed at a hardcoded path. Full step-by-step: `Tools/firmware-updater/FIRMWARE_UPDATER.md` §5. One-time, per dev machine — not something that ships to customers (see below for why it isn't in git).
2. Connect the external ST-Link probe's SWDIO/SWDCLK/3V3/GND leads to the UGN card's `ProgPAD` header.
3. Run Zadig against the probe's ST-Link interface specifically — `USB\VID_0483&PID_374B&MI_00`. **Not** the mass-storage or VCP entries under the same VID:PID (`MI_01`/`MI_02`) — wrong interface, won't work.
4. Flash:
   ```powershell
   .\build\firmware-updater.exe --target=stlink C:\path\to\some.bin
   ```

This is also what STM32CubeProgrammer's GUI does under the hood when connected via ST-Link/SWD — see the main `README.md`'s build/flash section.

**`--target=dfu` (USB, no probe needed):** the customer-facing path — same device and steps as Step 1 above (`VID_0483&PID_DF11`). Separate from the ST-Link/SWD path above; a customer has no ST-Link probe and isn't expected to open the case, so DFU-over-USB remains the plan for them regardless of what's used for development. The BOOT0/J5 DFU-mode-entry procedure (Step 1, "Put your device into firmware-update mode") is still unconfirmed against a real card — SWD flashing during development doesn't exercise or resolve that question, since it doesn't go through BOOT0 at all.

### Verifying this yourself instead of trusting this doc

This doc (and `FIRMWARE_UPDATER.md`) can go stale relative to the actual repo — check directly rather than assuming either is still accurate:

```bash
# What's actually committed to git under Tools/firmware-updater/
git ls-files Tools/firmware-updater/

# What's explicitly excluded, and why (read the comment above the entries)
cat .gitignore
```

As of this writing: `dist/firmware-updater.exe` and `dist/dfu-util.exe` are tracked — that's option A above, no setup needed after cloning. `dist/st-flash.exe`, `libstlink.dll`, and `libusb-1.0.dll` are deliberately **not** tracked — a fresh clone will not have them, and testing `--target=stlink` needs them set up locally from scratch, following §5 in `FIRMWARE_UPDATER.md`.

See `Tools/firmware-updater/FIRMWARE_UPDATER.md` for the full history and reasoning behind all of this (§8 is the original, more detailed version of this section).
