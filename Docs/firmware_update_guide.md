# Firmware Update Guide

This guide explains how to update the firmware on your Laserfabriken device from Windows. You only need to do the driver setup in Step 1 **once, ever** — after that, updating firmware is a single command (or drag-and-drop) each time.

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

`firmware-updater.exe` also supports `--target=stlink`, for flashing over an ST-Link debug probe (e.g. a Nucleo development board) instead of the production DFU path — not relevant for customers, only for testing this tool during development. See `Tools/firmware-updater/FIRMWARE_UPDATER.md` for details.
