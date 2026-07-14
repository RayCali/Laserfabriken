/*
 * Laserfabriken Firmware Updater
 *
 * Windows-only tool that automates the STM32 USB firmware update flow
 * without requiring the customer to run Zadig manually, and without
 * installing any proprietary driver from the MCU manufacturer (ST) —
 * only the generic, Microsoft-provided WinUSB driver is installed, via
 * the open-source libwdi library.
 *
 * Supports two targets, selected with --target=dfu or --target=stlink:
 *
 *   dfu     STM32's own built-in ROM DFU bootloader (VID:PID 0483:DF11).
 *           This is the production path: the G431 UGN board has no
 *           ST-Link chip on it, so this is how customers update it —
 *           the chip's own native USB peripheral talks DFU directly.
 *           Flashed via bundled dfu-util.exe.
 *
 *   stlink  ST-Link/V2-1 debug probe (VID:PID 0483:374B) — the chip
 *           built into Nucleo dev boards. NOT usable on the production
 *           G431 UGN board (it has no ST-Link chip), but useful for
 *           testing this tool's driver-binding logic right now on a
 *           Nucleo, before the G431 board exists.
 *           Flashed via bundled st-flash.exe (github.com/stlink-org/stlink).
 *
 * Flow (identical for both targets, only VID:PID + flash command differ):
 *   1. Enumerate USB devices, find the target by VID:PID.
 *   2. Prepare + install a WinUSB driver binding for that device via
 *      libwdi (automated equivalent of the manual Zadig steps).
 *   3. Shell out to the appropriate bundled flashing tool.
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

/* --- Per-target configuration ---------------------------------------- */

typedef struct {
    const char    *arg_name;  /* value passed after --target= */
    const char    *label;     /* human-readable name for console messages */
    unsigned short vid;
    unsigned short pid;
} target_config_t;

static const target_config_t TARGET_CONFIGS[] = {
    { "dfu",    "STM32 DFU-bootloader (produktionskort, G431 UGN)", 0x0483, 0xDF11 },
    { "stlink", "ST-Link/V2-1 (Nucleo-utvecklingskort)",            0x0483, 0x374B },
};
#define NUM_TARGETS (sizeof(TARGET_CONFIGS) / sizeof(TARGET_CONFIGS[0]))

#define FLASH_BASE_ADDR "0x08000000"  /* STM32 internal flash base — standard across the series */
#define INF_NAME        "laserfabriken_stm32.inf"
#define DRIVER_DIR      "usb_driver"

static void wait_for_enter(void)
{
    printf("\nTryck Enter for att stanga...\n");
    (void)getchar();
}

/*
 * Find the target device in the enumerated list, install a WinUSB
 * binding for it via libwdi, and clean up.
 *
 * Returns 0 on success, non-zero on failure.
 */
static int install_winusb_driver(const target_config_t *target)
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
        if (d->vid == target->vid && d->pid == target->pid) {
            dev = d;
            break;
        }
    }

    if (dev == NULL) {
        printf("Hittade ingen enhet av typen '%s' (VID=0x%04X PID=0x%04X).\n",
               target->label, target->vid, target->pid);
        if (strcmp(target->arg_name, "dfu") == 0) {
            printf("Kontrollera att kortet ar anslutet via USB och i DFU-lage\n"
                   "(BOOT0 hoog vid reset/strompaslag).\n");
        } else {
            printf("Kontrollera att Nucleo-kortet ar anslutet via ST-Link-USB-porten.\n");
        }
        wdi_destroy_list(list);
        return -1;
    }

    printf("Hittade enhet: %s\n", dev->desc ? dev->desc : "(inget namn rapporterat)");

    prep_opts.driver_type  = WDI_WINUSB;
    prep_opts.vendor_name  = "Laserfabriken";
    /* Leave disable_cat / disable_signing / use_wcid_driver / external_inf at
     * their zero-initialised defaults — TODO verify these are sane defaults
     * for an unsigned/self-signed driver package on a clean Windows install.
     * See handoff doc "Known risk areas". */

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
 * Shell out to the appropriate bundled flashing tool for this target.
 * Returns 0 on success.
 */
static int flash_firmware(const target_config_t *target, const char *bin_path)
{
    char cmd[1024];
    int  ret;

    if (strcmp(target->arg_name, "dfu") == 0) {
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
    } else if (strcmp(target->arg_name, "stlink") == 0) {
        /* st-flash write <file> <address> — from github.com/stlink-org/stlink.
         * TODO verify exact syntax against whatever release version is
         * bundled; st-flash's CLI has changed slightly across versions. */
        snprintf(cmd, sizeof(cmd),
                 "st-flash.exe write \"%s\" %s",
                 bin_path, FLASH_BASE_ADDR);
    } else {
        printf("Okant mal: %s\n", target->arg_name);
        return -1;
    }

    printf("Flashar firmware: %s\n", bin_path);
    printf("(kor: %s)\n", cmd);

    ret = system(cmd);
    if (ret != 0) {
        printf("Flashningskommandot misslyckades (returkod %d).\n", ret);
        return ret;
    }

    printf("Firmware flashad.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    const char *target_name = "dfu"; /* default: production path */
    const char *bin_path    = NULL;
    const target_config_t *target = NULL;
    size_t i;
    int a;

    printf("=== Laserfabriken Firmware Updater ===\n\n");

    for (a = 1; a < argc; a++) {
        if (strncmp(argv[a], "--target=", 9) == 0) {
            target_name = argv[a] + 9;
        } else {
            bin_path = argv[a];
        }
    }

    if (bin_path == NULL) {
        printf("Anvandning: %s [--target=dfu|stlink] <firmware.bin>\n\n", argv[0]);
        printf("  --target=dfu     STM32 DFU-bootloader (produktionskort, G431 UGN) [default]\n");
        printf("  --target=stlink  ST-Link/V2-1 (Nucleo-utvecklingskort, for testning)\n");
        wait_for_enter();
        return 1;
    }

    for (i = 0; i < NUM_TARGETS; i++) {
        if (strcmp(TARGET_CONFIGS[i].arg_name, target_name) == 0) {
            target = &TARGET_CONFIGS[i];
            break;
        }
    }
    if (target == NULL) {
        printf("Okant mal '%s'. Anvand 'dfu' eller 'stlink'.\n", target_name);
        wait_for_enter();
        return 1;
    }

    printf("Mal: %s\n\n", target->label);

    if (install_winusb_driver(target) != 0) {
        printf("\nDrivrutinssteget misslyckades. Avbryter.\n");
        wait_for_enter();
        return 1;
    }

    if (flash_firmware(target, bin_path) != 0) {
        printf("\nFlashning misslyckades.\n");
        wait_for_enter();
        return 1;
    }

    printf("\nKlart! Enheten startar om med den nya firmwaren.\n");
    wait_for_enter();
    return 0;
}
