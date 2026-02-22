#!/usr/bin/env python3
"""
Helper for zig build: outputs preset config as one define per line.
Usage: python3 zig_config.py <config_file> <preset_name>

Output format (one per line):
  DEFINE:ENABLE_FMRADIO
  DEFINE:ENABLE_VOX
  DEFINE_VAL:EDITION_STRING="Fusion"
  DEFINE_VAL:ALERT_TOT=10
  DEFINE:PY32F071x8
  DEFINE:USE_FULL_LL_DRIVER
  SOURCE:../src/apps/fm/fm.c
  SOURCE:../src/apps/fm/fm_ui.c
  VERSION:1.6.6
  NAME:deltafw
  AUTHORS:qshosfw
  GIT_HASH:<hash>
  BUILD_DATE:<date>
"""
import sys
import subprocess
from datetime import datetime

try:
    import tomli
except ImportError:
    try:
        import tomllib as tomli
    except ImportError:
        print("ERROR: Need tomli or tomllib", file=sys.stderr)
        sys.exit(1)


# Map of option name -> (define_name, [source_files])
# Only options that add source files need entries here
OPTION_SOURCES = {
    "AIRCOPY": ("ENABLE_AIRCOPY", ["src/apps/aircopy/aircopy.c", "src/apps/aircopy/aircopy_ui.c"]),
    "UART": ("ENABLE_UART", ["src/drivers/bsp/uart.c", "src/features/uart/uart.c"]),
    "USB": ("ENABLE_USB", [
        "src/drivers/bsp/vcp.c",
        "src/usb/usbd_cdc_if.c",
        "src/middlewares/CherryUSB/core/usbd_core.c",
        "src/middlewares/CherryUSB/port/usb_dc_py32.c",
        "src/middlewares/CherryUSB/class/cdc/usbd_cdc.c",
    ]),
    "SERIAL_SCREENCAST": ("ENABLE_SERIAL_SCREENCAST", ["src/features/screencast/screencast.c"]),
    "BK1080": ("ENABLE_BK1080", ["src/drivers/bsp/bk1080.c"]),
    "FMRADIO": ("ENABLE_FMRADIO", ["src/apps/fm/fm.c", "src/apps/fm/fm_ui.c"]),
    "VOICE": ("ENABLE_VOICE", ["src/drivers/bsp/voice.c"]),
    "PWRON_PASSWORD": ("ENABLE_PWRON_PASSWORD", ["src/ui/lock.c"]),
    "FLASHLIGHT": ("ENABLE_FLASHLIGHT", ["src/features/flashlight/flashlight.c"]),
    "CW_KEYER": ("ENABLE_CW_KEYER", ["src/features/cw/cw.c"]),
    "AM_FIX": ("ENABLE_AM_FIX", ["src/features/am_fix/am_fix.c"]),
    "IDENTIFIER": ("ENABLE_IDENTIFIER", ["src/helper/identifier.c"]),
    "CRYPTO": ("ENABLE_CRYPTO", ["src/helper/crypto.c"]),
    "APP_BREAKOUT_GAME": ("ENABLE_APP_BREAKOUT_GAME", ["src/features/breakout/breakout.c"]),
    "REGA": ("ENABLE_REGA", ["src/features/rega/rega.c"]),
    "LIVESEEK": ("ENABLE_LIVESEEK", ["src/apps/liveseek/liveseek.c"]),
}

# Options that only add a define (no extra sources)
DEFINE_ONLY_OPTIONS = {
    "ALARM": "ENABLE_ALARM",
    "TX1750": "ENABLE_TX1750",
    "VOX": "ENABLE_VOX",
    "NOAA": "ENABLE_NOAA",
    "NARROWER_BW_FILTER": "ENABLE_NARROWER_BW_FILTER",
    "WIDE_RX": "ENABLE_WIDE_RX",
    "TX_NON_FM": "ENABLE_TX_NON_FM",
    "SQUELCH_MORE_SENSITIVE": "ENABLE_SQUELCH_MORE_SENSITIVE",
    "CTCSS_TAIL_PHASE_SHIFT": "ENABLE_CTCSS_TAIL_PHASE_SHIFT",
    "REDUCE_LOW_MID_TX_POWER": "ENABLE_REDUCE_LOW_MID_TX_POWER",
    "BYP_RAW_DEMODULATORS": "ENABLE_BYP_RAW_DEMODULATORS",
    "RESCUE_OPERATIONS": "ENABLE_RESCUE_OPERATIONS",
    "SCAN_RANGES": "ENABLE_SCAN_RANGES",
    "BIG_FREQ": "ENABLE_BIG_FREQ",
    "SMALL_BOLD": "ENABLE_SMALL_BOLD",
    "INVERTED_LCD_MODE": "ENABLE_INVERTED_LCD_MODE",
    "LCD_CONTRAST_OPTION": "ENABLE_LCD_CONTRAST_OPTION",
    "RSSI_BAR": "ENABLE_RSSI_BAR",
    "MIC_BAR": "ENABLE_MIC_BAR",
    "SHOW_CHARGE_LEVEL": "ENABLE_SHOW_CHARGE_LEVEL",
    "USBC_CHARGING_INDICATOR": "ENABLE_USBC_CHARGING_INDICATOR",
    "REVERSE_BAT_SYMBOL": "ENABLE_REVERSE_BAT_SYMBOL",
    "BOOT_BEEPS": "ENABLE_BOOT_BEEPS",
    "BOOT_RESUME_STATE": "ENABLE_BOOT_RESUME_STATE",
    "DEEP_SLEEP_MODE": "ENABLE_DEEP_SLEEP_MODE",
    "RX_TX_TIMER_DISPLAY": "ENABLE_RX_TX_TIMER_DISPLAY",
    "NO_CODE_SCAN_TIMEOUT": "ENABLE_NO_CODE_SCAN_TIMEOUT",
    "BLMIN_TMP_OFF": "ENABLE_BLMIN_TMP_OFF",
    "KEEP_MEM_NAME": "ENABLE_KEEP_MEM_NAME",
    "COPY_CHAN_TO_VFO": "ENABLE_COPY_CHAN_TO_VFO",
    "CUSTOM_MENU_LAYOUT": "ENABLE_CUSTOM_MENU_LAYOUT",
    "DTMF_CALLING": "ENABLE_DTMF_CALLING",
    "PMR446_FREQUENCY_BAND": "ENABLE_PMR446_FREQUENCY_BAND",
    "GMRS_FRS_MURS_BANDS": "ENABLE_GMRS_FRS_MURS_BANDS",
    "FREQUENCY_LOCK_REGION_CA": "ENABLE_FREQUENCY_LOCK_REGION_CA",
    "SYSTEM_INFO_MENU": "ENABLE_SYSTEM_INFO_MENU",
    "F_CAL_MENU": "ENABLE_F_CAL_MENU",
    "RESET_CHANNEL_FUNCTION": "ENABLE_RESET_CHANNEL_FUNCTION",
    "EEPROM_HEXDUMP": "ENABLE_EEPROM_HEXDUMP",
    "FIRMWARE_DEBUG_LOGGING": "ENABLE_FIRMWARE_DEBUG_LOGGING",
    "AM_FIX_SHOW_DATA": "ENABLE_AM_FIX_SHOW_DATA",
    "BATTERY_CHARGING": "ENABLE_BATTERY_CHARGING",
    "STORAGE_ENCRYPTION": "ENABLE_STORAGE_ENCRYPTION",
    "PASSCODE": "ENABLE_PASSCODE",
    "TRNG_SENSORS": "ENABLE_TRNG_SENSORS",
    "AGC_SHOW_DATA": "ENABLE_AGC_SHOW_DATA",
    "UART_RW_BK_REGS": "ENABLE_UART_RW_BK_REGS",
    "SWD": "ENABLE_SWD",
    "FASTER_CHANNEL_SCAN": "ENABLE_FASTER_CHANNEL_SCAN",
    "SCRAMBLER": "ENABLE_SCRAMBLER",
    "ON_DEVICE_PROGRAMMING": "ENABLE_ON_DEVICE_PROGRAMMING",
    "SCAN_LIST_EDITING": "ENABLE_SCAN_LIST_EDITING",
    "TX_OFFSET": "ENABLE_TX_OFFSET",
    "EXTRA_ROGER": "ENABLE_EXTRA_ROGER",
    "CUSTOM_ROGER": "ENABLE_CUSTOM_ROGER",
    "UART_CMD_RSSI": "ENABLE_UART_CMD_RSSI",
    "UART_CMD_BATT": "ENABLE_UART_CMD_BATT",
    "UART_CMD_ID": "ENABLE_UART_CMD_ID",
    "EXTRA_UART_CMD": "ENABLE_EXTRA_UART_CMD",
}


def main():
    if len(sys.argv) < 3:
        print("Usage: zig_config.py <config.toml> <preset>", file=sys.stderr)
        sys.exit(1)

    config_file = sys.argv[1]
    preset_name = sys.argv[2]

    with open(config_file, "rb") as f:
        config = tomli.load(f)

    project = config.get("project", {})
    version = project.get("version", "0.0.0")
    name = project.get("name", "deltafw")
    authors = ", ".join(project.get("authors", ["qshosfw"]))

    print(f"VERSION:{version}")
    print(f"NAME:{name}")
    print(f"AUTHORS:{authors}")

    # Git hash
    try:
        git_hash = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        git_hash = "unknown"
    print(f"GIT_HASH:{git_hash}")

    # Build date
    print(f"BUILD_DATE:{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

    # Merge defaults + preset
    defaults = config.get("defaults", {})
    preset = config.get("presets", {}).get(preset_name)
    if not preset:
        print(f"ERROR: Preset '{preset_name}' not found", file=sys.stderr)
        sys.exit(1)

    opts = defaults.copy()
    opts.update(preset.get("options", {}))

    # Always-on defines
    print("DEFINE:PY32F071x8")
    print("DEFINE:USE_FULL_LL_DRIVER")

    # CUSTOM_FIRMWARE_MODS handling
    if opts.get("CUSTOM_FIRMWARE_MODS", False):
        print("DEFINE:ENABLE_CUSTOM_FIRMWARE_MODS")
        print("DEFINE_VAL:ALERT_TOT=10")

    # EDITION_STRING
    edition = opts.get("EDITION_STRING", "Custom")
    print(f'DEFINE_VAL:EDITION_STRING="{edition}"')

    # SQL_TONE
    print("DEFINE_VAL:SQL_TONE=550")

    # Process options with source files
    for opt_name, (define, srcs) in OPTION_SOURCES.items():
        if opts.get(opt_name, False):
            print(f"DEFINE:{define}")
            for s in srcs:
                print(f"SOURCE:{s}")

    # SPECTRUM special handling
    if opts.get("SPECTRUM", False):
        print("DEFINE:ENABLE_SPECTRUM")
        if opts.get("SPECTRUM_WATERFALL", False):
            print("DEFINE:ENABLE_SPECTRUM_WATERFALL")
            print("DEFINE:ENABLE_SPECTRUM_ADVANCED")
            print("SOURCE:src/apps/spectrum/spectrum_waterfall.c")
        else:
            print("SOURCE:src/apps/spectrum/spectrum.c")
        if opts.get("SPECTRUM_EXTRA_VALUES", False):
            print("DEFINE:ENABLE_SPECTRUM_EXTRA_VALUES")
        if opts.get("SPECTRUM_EXTENSIONS", False):
            print("DEFINE:ENABLE_SPECTRUM_EXTENSIONS")

    # CRC sources (needed if AIRCOPY or UART or USB)
    if opts.get("AIRCOPY", False) or opts.get("UART", False) or opts.get("USB", False):
        print("SOURCE:src/drivers/bsp/crc.c")
        print("SOURCE:src/drivers/bsp/eeprom_compat.c")

    # USB extra include paths
    if opts.get("USB", False):
        print("USB_INC:src/middlewares/CherryUSB/core")
        print("USB_INC:src/middlewares/CherryUSB/port")
        print("USB_INC:src/middlewares/CherryUSB/common")
        print("USB_INC:src/middlewares/CherryUSB/class/cdc")

    # Define-only options
    for opt_name, define in DEFINE_ONLY_OPTIONS.items():
        if opts.get(opt_name, False):
            print(f"DEFINE:{define}")


if __name__ == "__main__":
    main()
