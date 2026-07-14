/*
 * Laserfabriken Firmware Updater
 *
 * Windows-only tool that automates the STM32 USB DFU firmware update flow
 * without requiring the customer to run Zadig manually, and without
 * installing any proprietary driver from the MCU manufacturer (ST) —
 * only the generic, Microsoft-provided WinUSB driver is installed, via
 * the open-source libwdi library.
 *
 * Flow:
 *   1. Enumerate USB devices, find the STM32 DFU bootloader by VID:PID
 *      (0483:DF11 — fixed ST-assigned identifier for the built-in ROM
 *      DFU bootloader, same across all STM32 parts).
 *   2. Prepare + install a WinUSB driver binding for that device via
 *      libwdi (this is the automated equivalent of the manual Zadig
 *      "select device, select WinUSB, click Install" steps).
 *   3. Shell out to dfu-util.exe (bundled alongside this executable) to
 *      perform the actual firmware flash.
 *
 * STATUS: written and reviewed against the libwdi.h API on paper (see
 * FIRMWARE_UPDATER_HANDOFF.md for exactly what was and wasn't verified).
 * NOT YET BUILT OR RUN ON WINDOWS. NOT YET TESTED AGAINST REAL HARDWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "libwdi.h"

#define STM32_DFU_VID   0x0483
#define STM32_DFU_PID   0xDF11
#define INF_NAME        "laserfabriken_stm32_dfu.inf"
#define DRIVER_DIR      "usb_driver"

/* STM32G431RB internal flash base address (standard across the STM32 series) */
#define FLASH_BASE_ADDR "0x08000000"

static void wait_for_enter(void)
{
    printf("\nTryck Enter for att stanga...\n");
    (void)getchar();
}

/*
 * Find the STM32 DFU device in the enumerated list, install a WinUSB
 * binding for it via libwdi, and clean up.
 *
 * Returns 0 on success, non-zero on failure.
 */
static int install_winusb_driver(void)
{
    struct wdi_device_info *list = NULL;
    struct wdi_device_info *dev  = NULL;
    struct wdi_options_create_list    list_opts = {0};
    struct wdi_options_prepare_driver prep_opts = {0};
    struct wdi_options_install_driver inst_opts = {0};
    int r;

    list_opts.list_all         = TRUE;  /* include devices that already have some driver bound */
    list_opts.list_hubs        = FALSE;
    list_opts.trim_whitespaces = TRUE;

    r = wdi_create_list(&list, &list_opts);
    if (r != WDI_SUCCESS) {
        printf("Kunde inte lista USB-enheter: %s\n", wdi_strerror(r));
        return r;
    }

    for (struct wdi_device_info *d = list; d != NULL; d = d->next) {
        if (d->vid == STM32_DFU_VID && d->pid == STM32_DFU_PID) {
            dev = d;
            break;
        }
    }

    if (dev == NULL) {
        printf("Hittade ingen STM32 i DFU-lage (VID=0x%04X PID=0x%04X).\n"
               "Kontrollera att kortet ar anslutet via USB och i DFU-lage\n"
               "(BOOT0 hoog vid reset/strompaslag).\n",
               STM32_DFU_VID, STM32_DFU_PID);
        wdi_destroy_list(list);
        return -1;
    }

    printf("Hittade STM32 DFU-enhet: %s\n", dev->desc ? dev->desc : "(inget namn rapporterat)");

    prep_opts.driver_type  = WDI_WINUSB;
    prep_opts.vendor_name  = "Laserfabriken";
    /* Leave disable_cat / disable_signing / use_wcid_driver / external_inf at
     * their zero-initialised defaults — TODO verify these are sane defaults
     * for an unsigned/self-signed driver package on a clean Windows install.
     * See handoff doc §"Known risk areas". */

    r = wdi_prepare_driver(dev, DRIVER_DIR, INF_NAME, &prep_opts);
    if (r != WDI_SUCCESS) {
        printf("Kunde inte forbereda drivrutinspaket: %s\n", wdi_strerror(r));
        wdi_destroy_list(list);
        return r;
    }

    printf("Installerar WinUSB-drivrutin (ett UAC-fonster kan visas — svara Ja)...\n");

    /* inst_opts.hWnd left NULL — console app, no parent window.
     * pending_install_timeout left 0 — TODO verify 0 means "use library
     * default" rather than "don't wait at all"; see handoff doc. */
    r = wdi_install_driver(dev, DRIVER_DIR, INF_NAME, &inst_opts);
    if (r != WDI_SUCCESS) {
        printf("Drivrutinsinstallation misslyckades: %s\n", wdi_strerror(r));
        wdi_destroy_list(list);
        return r;
    }

    printf("WinUSB-drivrutin installerad.\n");
    wdi_destroy_list(list);
    return 0;
}

/*
 * Shell out to the bundled dfu-util.exe to perform the actual flash.
 * Returns 0 on success.
 */
static int flash_firmware(const char *bin_path)
{
    char cmd[1024];
    int  ret;

    /* -a 0            : DFU alt-setting 0 (internal flash on STM32)
     * -s ADDR:leave   : start address + "leave" tells the bootloader to
     *                   jump to the newly flashed application afterwards
     * -D <file>        : download (write) this file to the device
     *
     * TODO verify on real hardware: alt-setting index, whether ":leave"
     * behaves as expected on this specific board/bootloader config, and
     * whether the STM32G431's DFU bootloader needs :force or :mass-erase
     * variants — see handoff doc. */
    snprintf(cmd, sizeof(cmd),
             "dfu-util.exe -a 0 -s %s:leave -D \"%s\"",
             FLASH_BASE_ADDR, bin_path);

    printf("Flashar firmware: %s\n", bin_path);
    printf("(kor: %s)\n", cmd);

    ret = system(cmd);
    if (ret != 0) {
        printf("dfu-util.exe misslyckades (returkod %d).\n", ret);
        return ret;
    }

    printf("Firmware flashad.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    printf("=== Laserfabriken Firmware Updater ===\n\n");

    if (argc < 2) {
        printf("Anvandning: %s <firmware.bin>\n", argv[0]);
        wait_for_enter();
        return 1;
    }

    if (install_winusb_driver() != 0) {
        printf("\nDrivrutinssteget misslyckades. Avbryter.\n");
        wait_for_enter();
        return 1;
    }

    if (flash_firmware(argv[1]) != 0) {
        printf("\nFlashning misslyckades.\n");
        wait_for_enter();
        return 1;
    }

    printf("\nKlart! Enheten startar om med den nya firmwaren.\n");
    wait_for_enter();
    return 0;
}
