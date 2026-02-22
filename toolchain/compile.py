#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import concurrent.futures
import hashlib

# deltafw — High-Performance Zig/Clang Build Driver
# Replaces 'zig build' to provide perfect live logs and multi-core speed.

BOLD, RESET = "\033[1m", "\033[0m"
RED, GREEN, YELLOW, BLUE, CYAN, GRAY = "\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[36m", "\033[90m"
BG_GREEN, BG_CYAN = "\033[42m", "\033[46m"

BADGE_BUILD = f"{BOLD}\033[37m{BG_CYAN} BLD  {RESET}"
BADGE_OK = f"{BOLD}\033[37m{BG_GREEN}  OK  {RESET}"

def draw_progress_bar(current, total, width=20):
    percent = (current / total) * 100
    filled = int(width * current / total)
    color = BLUE if percent < 50 else CYAN if percent < 90 else GREEN
    return f"{color}{'█' * filled}{'░' * (width - filled)}{RESET} {BOLD}{percent:>5.1f}%{RESET}"

CORE_SOURCES = [
    "src/core/main.c", "src/core/init.c", "src/core/misc.c", "src/core/scheduler.c",
    "src/core/version.c", "src/core/board.c", "src/features/audio/audio.c",
    "src/features/radio/radio.c", "src/features/radio/frequencies.c", "src/features/dcs/dcs.c",
    "src/apps/settings/settings.c", "src/apps/settings/settings_ui.c", "src/features/radio/functions.c",
    "src/ui/bitmaps.c", "src/ui/font.c", "src/features/action/action.c", "src/features/app/app.c",
    "src/apps/scanner/chFrScanner.c", "src/features/common/common.c", "src/features/dtmf/dtmf.c",
    "src/features/generic/generic.c", "src/features/main_screen/main_screen.c", "src/features/menu/menu.c",
    "src/apps/security/passcode.c", "src/features/storage/storage.c", "src/apps/scanner/scanner.c",
    "src/drivers/bsp/adc.c", "src/drivers/bsp/backlight.c", "src/drivers/bsp/bk4829.c",
    "src/drivers/bsp/py25q16.c", "src/drivers/bsp/gpio.c", "src/drivers/bsp/i2c.c",
    "src/drivers/bsp/keyboard.c", "src/drivers/bsp/st7565.c", "src/drivers/bsp/system.c",
    "src/drivers/bsp/systick.c", "src/apps/battery/battery.c", "src/apps/battery/battery_ui.c",
    "src/apps/launcher/launcher.c", "src/apps/memories/memories.c", "src/apps/sysinfo/sysinfo.c",
    "src/apps/boot/boot.c", "src/ui/helper.c", "src/ui/inputbox.c", "src/ui/main.c",
    "src/ui/bar.c", "src/ui/menu.c", "src/apps/scanner/scanner_ui.c", "src/ui/status.c",
    "src/ui/ui.c", "src/ui/hexdump.c", "src/ui/ag_graphics.c", "src/ui/ag_menu.c",
    "src/ui/textinput.c", "src/ui/freqinput.c", "src/apps/boot/welcome.c",
    "src/core/Src/main.c", "src/core/Src/py32f071_it.c", "src/core/Src/system_py32f071.c",
    "src/drivers/hal/Src/py32f071_ll_rcc.c", "src/drivers/hal/Src/py32f071_ll_pwr.c",
    "src/drivers/hal/Src/py32f071_ll_dma.c", "src/drivers/hal/Src/py32f071_ll_gpio.c",
    "src/drivers/hal/Src/py32f071_ll_utils.c", "src/drivers/hal/Src/py32f071_ll_adc.c",
    "src/drivers/hal/Src/py32f071_ll_comp.c", "src/drivers/hal/Src/py32f071_ll_crc.c",
    "src/drivers/hal/Src/py32f071_ll_dac.c", "src/drivers/hal/Src/py32f071_ll_exti.c",
    "src/drivers/hal/Src/py32f071_ll_i2c.c", "src/drivers/hal/Src/py32f071_ll_rtc.c",
    "src/drivers/hal/Src/py32f071_ll_spi.c", "src/drivers/hal/Src/py32f071_ll_tim.c",
    "src/drivers/hal/Src/py32f071_ll_lptim.c", "src/drivers/hal/Src/py32f071_ll_usart.c",
    "toolchain/libc_shim/libc_shim.c"
]

COMMON_FLAGS = [
    "-target", "thumb-freestanding-eabi", "-mcpu=cortex_m0plus",
    "-mthumb", "-Oz", "-flto", "-std=gnu2x",
    "-ffunction-sections", "-fdata-sections",
    "-fno-common", "-fno-builtin", "-fshort-enums",
    "-fno-exceptions", "-fno-threadsafe-statics", "-fomit-frame-pointer",
    "-mno-unaligned-access", "-fno-stack-protector",
    "-Wno-implicit-function-declaration", "-Wno-type-limits",
    "-Wno-builtin-requires-header", "-Wno-gnu-designator",
    "-isystem", "toolchain/libc_shim",
    "-I", "src", "-I", "src/core", "-I", "src/core/Inc",
    "-I", "src/drivers/hal/Inc", "-I", "src/drivers/cmsis/Include",
    "-I", "src/drivers/cmsis/Device/PY32F071/Include", "-I", "src/usb", "-I", "."
]

def compile_file(src, obj, flags):
    cmd = ["zig", "cc"] + flags + ["-c", src, "-o", obj]
    res = subprocess.run(cmd, capture_output=True, text=True)
    return res.returncode == 0, src, res.stderr

def main():
    preset = sys.argv[1] if len(sys.argv) > 1 else "Custom"
    os.makedirs(".zig-cache/objs", exist_ok=True)
    
    cfg_out = subprocess.check_output(["python3", "toolchain/zig_config.py", "config.toml", preset], text=True)
    defines, extra_sources = [], []
    v_maj, v_min, v_fix, v_commit, v_date = "1", "6", "6", "unknown", "unknown"
    
    for line in cfg_out.splitlines():
        if line.startswith("DEFINE:"): defines.append("-D" + line.split(":")[1])
        if line.startswith("DEFINE_VAL:"):
            if "EDITION_STRING" in line: continue
            defines.append("-D" + line.split(":")[1])
        if line.startswith("SOURCE:"): extra_sources.append(line.split(":")[1].replace("../", ""))
        if line.startswith("GIT_HASH:"): v_commit = line.split(":")[1]
        if line.startswith("BUILD_DATE:"): v_date = line.split(":")[1]

    defines += [
        f"-DGIT_COMMIT_HASH=\"{v_commit}\"", f"-DBUILD_DATE=\"{v_date}\"",
        f"-DAUTHOR_STRING=\"deltafw\"", f"-DVERSION_STRING=\"v1.6.6\"",
        f"-DAUTHOR_STRING_2=\"qshosfw\"", f"-DVERSION_STRING_2=\"v1.6.6\"",
        f"-DEDITION_STRING=\"{preset}\""
    ]
    if "ENABLE_USB" in "".join(defines):
        for inc in ["src/middlewares/CherryUSB/core", "src/middlewares/CherryUSB/port", "src/middlewares/CherryUSB/common", "src/middlewares/CherryUSB/class/cdc"]:
            defines += ["-I", inc]

    all_srcs = []
    seen = set()
    for s in CORE_SOURCES + extra_sources:
        if s not in seen:
            all_srcs.append(s)
            seen.add(s)

    print(f"{BADGE_BUILD} Performing {BOLD}Collective Compilation{RESET} of {BOLD}{len(all_srcs)}{RESET} files...")
    start = time.time()
    
    # Collective compilation + Link in one go for perfect LTO
    elf = "build/bin/deltafw"
    bin_file = "build/bin/deltafw.bin"
    os.makedirs("build/bin", exist_ok=True)
    
    build_cmd = ["zig", "cc"] + COMMON_FLAGS + defines + [
        "-Wl,--gc-sections", "-Tsrc/core/py32f071xb_lld.ld", "-Wl,--entry=Reset_Handler",
        "-o", elf, "src/core/startup_py32f071xx.s"
    ] + all_srcs

    # Show live logs (file list) before running
    for i, src in enumerate(all_srcs):
        sys.stdout.write(f"\r\033[K 📂 Preparing {BOLD}{os.path.basename(src):<20}{RESET} ({i+1}/{len(all_srcs)})")
        sys.stdout.flush()
    
    print(f"\n{BADGE_BUILD} Invoking {BOLD}Clang/LLD Optimizer{RESET} (LTO)...")
    
    res = subprocess.run(build_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"\n{RED}{BOLD}Build failed!{RESET}\n{res.stderr}")
        sys.exit(1)

    subprocess.check_call(["zig", "objcopy", "-O", "binary", elf, bin_file])
    
    # Calculate size for report
    bin_size = os.path.getsize(bin_file)
    print(f"{BADGE_OK} Build complete in {time.time()-start:.1f}s | Size: {BOLD}{bin_size}{RESET} bytes")

if __name__ == "__main__":
    main()
