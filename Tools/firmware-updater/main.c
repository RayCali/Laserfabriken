/*
 * Laserfabriken Firmware Updater
 *
 * Windows-only tool that flashes STM32 firmware over USB. Per
 * Docs/mjukvaruspecifikation.md §3.6, the WinUSB driver binding is a
 * one-time manual Zadig step done by the customer at first setup -- this
 * tool does not automate that step. See FIRMWARE_UPDATER.md for
 * the one-time setup instructions and why an earlier libwdi-based
 * auto-install approach was dropped (Windows' default driver-signature
 * policy rejects the unsigned driver package that approach produced).
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
 *           testing on a Nucleo before the G431 board exists.
 *           Flashed via bundled st-flash.exe (github.com/stlink-org/stlink).
 *
 * Both dfu-util and st-flash report their own "device not found" errors
 * if the WinUSB driver hasn't been bound yet -- see the one-time setup
 * note printed on failure below.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

typedef struct {
    const char *arg_name;  /* value passed after --target= */
    const char *label;     /* human-readable name for console messages */
} target_config_t;

static const target_config_t TARGET_CONFIGS[] = {
    { "dfu",    "STM32 DFU bootloader (production board, G431 UGN)" },
    { "stlink", "ST-Link/V2-1 (Nucleo dev board)" },
};
#define NUM_TARGETS (sizeof(TARGET_CONFIGS) / sizeof(TARGET_CONFIGS[0]))

#define FLASH_BASE_ADDR "0x08000000"  /* STM32 internal flash base — standard across the series */

static void wait_for_enter(void)
{
    printf("\nPress Enter to close...\n");
    (void)getchar();
}

/*
 * Directory this .exe was launched from. Bundled tools (dfu-util.exe,
 * st-flash.exe) live next to it, but system() shelling out to a bare
 * "dfu-util.exe" is not reliable -- confirmed on real hardware that
 * cmd.exe's bare-command search does not always check the current
 * directory (depends on how the caller launched the current directory
 * is set, e.g. a desktop shortcut with a different "Start in" folder).
 * Always build an explicit path instead.
 */
static int get_exe_dir(char *buf, size_t size)
{
    char *last_sep;

    if (GetModuleFileNameA(NULL, buf, (DWORD)size) == 0) {
        return -1;
    }
    last_sep = strrchr(buf, '\\');
    if (last_sep == NULL) {
        return -1;
    }
    *last_sep = '\0';
    return 0;
}

/*
 * Shell out to the appropriate bundled flashing tool for this target.
 * Returns 0 on success.
 */
static int flash_firmware(const target_config_t *target, const char *bin_path)
{
    char exe_dir[512];
    char cmd[1024];
    char cmd_wrapped[1030];
    int  ret;

    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) {
        printf("Could not determine this program's own directory.\n");
        return -1;
    }

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
                 "\"%s\\dfu-util.exe\" -a 0 -s %s:leave -D \"%s\"",
                 exe_dir, FLASH_BASE_ADDR, bin_path);
    } else if (strcmp(target->arg_name, "stlink") == 0) {
        /* st-flash write <file> <address> — from github.com/stlink-org/stlink.
         * TODO verify exact syntax against whatever release version is
         * bundled; st-flash's CLI has changed slightly across versions. */
        snprintf(cmd, sizeof(cmd),
                 "\"%s\\st-flash.exe\" write \"%s\" %s",
                 exe_dir, bin_path, FLASH_BASE_ADDR);
    } else {
        printf("Unknown target: %s\n", target->arg_name);
        return -1;
    }

    printf("Flashing firmware: %s\n", bin_path);
    printf("(running: %s)\n", cmd);

    /* cmd.exe mis-parses a command line that starts with a quoted path
     * unless the ENTIRE line is also wrapped in an outer pair of quotes
     * (confirmed on real hardware: without this, cmd.exe truncated the
     * quoted exe path at its first space and reported it as an unknown
     * command). This is a documented cmd.exe /c quirk, not specific to
     * this tool. */
    snprintf(cmd_wrapped, sizeof(cmd_wrapped), "\"%s\"", cmd);

    ret = system(cmd_wrapped);
    if (ret != 0) {
        printf("Flash command failed (return code %d).\n"
               "If this looks like a 'device not found' error, the WinUSB\n"
               "driver may not be bound yet -- run the one-time Zadig setup\n"
               "described in FIRMWARE_UPDATER.md first.\n", ret);
        return ret;
    }

    printf("Firmware flashed.\n");
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
        printf("Usage: %s [--target=dfu|stlink] <firmware.bin>\n\n", argv[0]);
        printf("  --target=dfu     STM32 DFU bootloader (production board, G431 UGN) [default]\n");
        printf("  --target=stlink  ST-Link/V2-1 (Nucleo dev board, for testing)\n");
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
        printf("Unknown target '%s'. Use 'dfu' or 'stlink'.\n", target_name);
        wait_for_enter();
        return 1;
    }

    printf("Target: %s\n\n", target->label);

    if (flash_firmware(target, bin_path) != 0) {
        printf("\nFlashing failed.\n");
        wait_for_enter();
        return 1;
    }

    printf("\nDone! The device is restarting with the new firmware.\n");
    wait_for_enter();
    return 0;
}
