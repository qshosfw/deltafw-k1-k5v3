// deltafw — Zig Build System (Cortex-M0+ PY32F071)
const std = @import("std");

// ── Feature flag definition helper ──────────────────────────────────
const Feature = struct {
    name: []const u8,
    define: []const u8,
    sources: []const []const u8 = &.{},
    extra_includes: []const []const u8 = &.{},
};

const features: []const Feature = &.{
    .{ .name = "AIRCOPY", .define = "ENABLE_AIRCOPY", .sources = &.{ "../src/apps/aircopy/aircopy.c", "../src/apps/aircopy/aircopy_ui.c" } },
    .{ .name = "UART", .define = "ENABLE_UART", .sources = &.{ "../src/drivers/bsp/uart.c", "../src/features/uart/uart.c" } },
    .{ .name = "USB", .define = "ENABLE_USB", .sources = &.{
        "../src/drivers/bsp/vcp.c",
        "../src/usb/usbd_cdc_if.c",
        "../src/middlewares/CherryUSB/core/usbd_core.c",
        "../src/middlewares/CherryUSB/port/usb_dc_py32.c",
        "../src/middlewares/CherryUSB/class/cdc/usbd_cdc.c",
    }, .extra_includes = &.{
        "../src/middlewares/CherryUSB/core",
        "../src/middlewares/CherryUSB/port",
        "../src/middlewares/CherryUSB/common",
        "../src/middlewares/CherryUSB/class/cdc",
    } },
    .{ .name = "SERIAL_SCREENCAST", .define = "ENABLE_SERIAL_SCREENCAST", .sources = &.{"../src/features/screencast/screencast.c"} },
    .{ .name = "BK1080", .define = "ENABLE_BK1080", .sources = &.{"../src/drivers/bsp/bk1080.c"} },
    .{ .name = "FMRADIO", .define = "ENABLE_FMRADIO", .sources = &.{ "../src/apps/fm/fm.c", "../src/apps/fm/fm_ui.c" } },
    .{ .name = "VOICE", .define = "ENABLE_VOICE", .sources = &.{"../src/drivers/bsp/voice.c"} },
    .{ .name = "PWRON_PASSWORD", .define = "ENABLE_PWRON_PASSWORD", .sources = &.{"../src/ui/lock.c"} },
    .{ .name = "FLASHLIGHT", .define = "ENABLE_FLASHLIGHT", .sources = &.{"../src/features/flashlight/flashlight.c"} },
    .{ .name = "CW_KEYER", .define = "ENABLE_CW_KEYER", .sources = &.{"../src/features/cw/cw.c"} },
    .{ .name = "AM_FIX", .define = "ENABLE_AM_FIX", .sources = &.{"../src/features/am_fix/am_fix.c"} },
    .{ .name = "IDENTIFIER", .define = "ENABLE_IDENTIFIER", .sources = &.{"../src/helper/identifier.c"} },
    .{ .name = "CRYPTO", .define = "ENABLE_CRYPTO", .sources = &.{"../src/helper/crypto.c"} },
    .{ .name = "APP_BREAKOUT_GAME", .define = "ENABLE_APP_BREAKOUT_GAME", .sources = &.{"../src/features/breakout/breakout.c"} },
    .{ .name = "REGA", .define = "ENABLE_REGA", .sources = &.{"../src/features/rega/rega.c"} },
    .{ .name = "LIVESEEK", .define = "ENABLE_LIVESEEK", .sources = &.{"../src/apps/liveseek/liveseek.c"} },
    .{ .name = "ALARM", .define = "ENABLE_ALARM" },
    .{ .name = "TX1750", .define = "ENABLE_TX1750" },
    .{ .name = "VOX", .define = "ENABLE_VOX" },
    .{ .name = "NOAA", .define = "ENABLE_NOAA" },
    .{ .name = "NARROWER_BW_FILTER", .define = "ENABLE_NARROWER_BW_FILTER" },
    .{ .name = "WIDE_RX", .define = "ENABLE_WIDE_RX" },
    .{ .name = "TX_NON_FM", .define = "ENABLE_TX_NON_FM" },
    .{ .name = "SQUELCH_MORE_SENSITIVE", .define = "ENABLE_SQUELCH_MORE_SENSITIVE" },
    .{ .name = "CTCSS_TAIL_PHASE_SHIFT", .define = "ENABLE_CTCSS_TAIL_PHASE_SHIFT" },
    .{ .name = "REDUCE_LOW_MID_TX_POWER", .define = "ENABLE_REDUCE_LOW_MID_TX_POWER" },
    .{ .name = "BYP_RAW_DEMODULATORS", .define = "ENABLE_BYP_RAW_DEMODULATORS" },
    .{ .name = "RESCUE_OPERATIONS", .define = "ENABLE_RESCUE_OPERATIONS" },
    .{ .name = "SCAN_RANGES", .define = "ENABLE_SCAN_RANGES" },
    .{ .name = "BIG_FREQ", .define = "ENABLE_BIG_FREQ" },
    .{ .name = "SMALL_BOLD", .define = "ENABLE_SMALL_BOLD" },
    .{ .name = "INVERTED_LCD_MODE", .define = "ENABLE_INVERTED_LCD_MODE" },
    .{ .name = "LCD_CONTRAST_OPTION", .define = "ENABLE_LCD_CONTRAST_OPTION" },
    .{ .name = "RSSI_BAR", .define = "ENABLE_RSSI_BAR" },
    .{ .name = "MIC_BAR", .define = "ENABLE_MIC_BAR" },
    .{ .name = "SHOW_CHARGE_LEVEL", .define = "ENABLE_SHOW_CHARGE_LEVEL" },
    .{ .name = "USBC_CHARGING_INDICATOR", .define = "ENABLE_USBC_CHARGING_INDICATOR" },
    .{ .name = "REVERSE_BAT_SYMBOL", .define = "ENABLE_REVERSE_BAT_SYMBOL" },
    .{ .name = "BOOT_BEEPS", .define = "ENABLE_BOOT_BEEPS" },
    .{ .name = "BOOT_RESUME_STATE", .define = "ENABLE_BOOT_RESUME_STATE" },
    .{ .name = "DEEP_SLEEP_MODE", .define = "ENABLE_DEEP_SLEEP_MODE" },
    .{ .name = "RX_TX_TIMER_DISPLAY", .define = "ENABLE_RX_TX_TIMER_DISPLAY" },
    .{ .name = "NO_CODE_SCAN_TIMEOUT", .define = "ENABLE_NO_CODE_SCAN_TIMEOUT" },
    .{ .name = "BLMIN_TMP_OFF", .define = "ENABLE_BLMIN_TMP_OFF" },
    .{ .name = "KEEP_MEM_NAME", .define = "ENABLE_KEEP_MEM_NAME" },
    .{ .name = "COPY_CHAN_TO_VFO", .define = "ENABLE_COPY_CHAN_TO_VFO" },
    .{ .name = "CUSTOM_MENU_LAYOUT", .define = "ENABLE_CUSTOM_MENU_LAYOUT" },
    .{ .name = "DTMF_CALLING", .define = "ENABLE_DTMF_CALLING" },
    .{ .name = "PMR446_FREQUENCY_BAND", .define = "ENABLE_PMR446_FREQUENCY_BAND" },
    .{ .name = "GMRS_FRS_MURS_BANDS", .define = "ENABLE_GMRS_FRS_MURS_BANDS" },
    .{ .name = "FREQUENCY_LOCK_REGION_CA", .define = "ENABLE_FREQUENCY_LOCK_REGION_CA" },
    .{ .name = "SYSTEM_INFO_MENU", .define = "ENABLE_SYSTEM_INFO_MENU" },
    .{ .name = "F_CAL_MENU", .define = "ENABLE_F_CAL_MENU" },
    .{ .name = "RESET_CHANNEL_FUNCTION", .define = "ENABLE_RESET_CHANNEL_FUNCTION" },
    .{ .name = "EEPROM_HEXDUMP", .define = "ENABLE_EEPROM_HEXDUMP" },
    .{ .name = "FIRMWARE_DEBUG_LOGGING", .define = "ENABLE_FIRMWARE_DEBUG_LOGGING" },
    .{ .name = "AM_FIX_SHOW_DATA", .define = "ENABLE_AM_FIX_SHOW_DATA" },
    .{ .name = "BATTERY_CHARGING", .define = "ENABLE_BATTERY_CHARGING" },
    .{ .name = "STORAGE_ENCRYPTION", .define = "ENABLE_STORAGE_ENCRYPTION" },
    .{ .name = "PASSCODE", .define = "ENABLE_PASSCODE" },
    .{ .name = "TRNG_SENSORS", .define = "ENABLE_TRNG_SENSORS" },
    .{ .name = "AGC_SHOW_DATA", .define = "ENABLE_AGC_SHOW_DATA" },
    .{ .name = "UART_RW_BK_REGS", .define = "ENABLE_UART_RW_BK_REGS" },
    .{ .name = "SWD", .define = "ENABLE_SWD" },
    .{ .name = "FASTER_CHANNEL_SCAN", .define = "ENABLE_FASTER_CHANNEL_SCAN" },
    .{ .name = "SCRAMBLER", .define = "ENABLE_SCRAMBLER" },
    .{ .name = "ON_DEVICE_PROGRAMMING", .define = "ENABLE_ON_DEVICE_PROGRAMMING" },
    .{ .name = "SCAN_LIST_EDITING", .define = "ENABLE_SCAN_LIST_EDITING" },
    .{ .name = "TX_OFFSET", .define = "ENABLE_TX_OFFSET" },
    .{ .name = "EXTRA_ROGER", .define = "ENABLE_EXTRA_ROGER" },
    .{ .name = "CUSTOM_ROGER", .define = "ENABLE_CUSTOM_ROGER" },
    .{ .name = "UART_CMD_RSSI", .define = "ENABLE_UART_CMD_RSSI" },
    .{ .name = "UART_CMD_BATT", .define = "ENABLE_UART_CMD_BATT" },
    .{ .name = "UART_CMD_ID", .define = "ENABLE_UART_CMD_ID" },
    .{ .name = "EXTRA_UART_CMD", .define = "ENABLE_EXTRA_UART_CMD" },
};

const core_sources: []const []const u8 = &.{
    "../src/core/main.c",                        "../src/core/init.c",                         "../src/core/misc.c",                         "../src/core/scheduler.c",
    "../src/core/version.c",                     "../src/core/board.c",                        "../src/features/audio/audio.c",              "../src/features/radio/radio.c",
    "../src/features/radio/frequencies.c",       "../src/features/dcs/dcs.c",                  "../src/apps/settings/settings.c",            "../src/apps/settings/settings_ui.c",
    "../src/features/radio/functions.c",         "../src/ui/bitmaps.c",                        "../src/ui/font.c",                           "../src/features/action/action.c",
    "../src/features/app/app.c",                 "../src/apps/scanner/chFrScanner.c",          "../src/features/common/common.c",            "../src/features/dtmf/dtmf.c",
    "../src/features/generic/generic.c",         "../src/features/main_screen/main_screen.c",  "../src/features/menu/menu.c",                "../src/apps/security/passcode.c",
    "../src/features/storage/storage.c",         "../src/apps/scanner/scanner.c",              "../src/drivers/bsp/adc.c",                   "../src/drivers/bsp/backlight.c",
    "../src/drivers/bsp/bk4829.c",               "../src/drivers/bsp/py25q16.c",               "../src/drivers/bsp/gpio.c",                  "../src/drivers/bsp/i2c.c",
    "../src/drivers/bsp/keyboard.c",             "../src/drivers/bsp/st7565.c",                "../src/drivers/bsp/system.c",                "../src/drivers/bsp/systick.c",
    "../src/apps/battery/battery.c",             "../src/apps/battery/battery_ui.c",           "../src/apps/launcher/launcher.c",            "../src/apps/memories/memories.c",
    "../src/apps/sysinfo/sysinfo.c",             "../src/apps/boot/boot.c",                    "../src/ui/helper.c",                         "../src/ui/inputbox.c",
    "../src/ui/main.c",                          "../src/ui/bar.c",                            "../src/ui/menu.c",                           "../src/apps/scanner/scanner_ui.c",
    "../src/ui/status.c",                        "../src/ui/ui.c",                             "../src/ui/hexdump.c",                        "../src/ui/ag_graphics.c",
    "../src/ui/ag_menu.c",                       "../src/ui/textinput.c",                      "../src/ui/freqinput.c",                      "../src/apps/boot/welcome.c",
    "../src/core/Src/main.c",                    "../src/core/Src/py32f071_it.c",              "../src/core/Src/system_py32f071.c",          "../src/drivers/hal/Src/py32f071_ll_rcc.c",
    "../src/drivers/hal/Src/py32f071_ll_pwr.c",  "../src/drivers/hal/Src/py32f071_ll_dma.c",   "../src/drivers/hal/Src/py32f071_ll_gpio.c",  "../src/drivers/hal/Src/py32f071_ll_utils.c",
    "../src/drivers/hal/Src/py32f071_ll_adc.c",  "../src/drivers/hal/Src/py32f071_ll_comp.c",  "../src/drivers/hal/Src/py32f071_ll_crc.c",   "../src/drivers/hal/Src/py32f071_ll_dac.c",
    "../src/drivers/hal/Src/py32f071_ll_exti.c", "../src/drivers/hal/Src/py32f071_ll_i2c.c",   "../src/drivers/hal/Src/py32f071_ll_rtc.c",   "../src/drivers/hal/Src/py32f071_ll_spi.c",
    "../src/drivers/hal/Src/py32f071_ll_tim.c",  "../src/drivers/hal/Src/py32f071_ll_lptim.c", "../src/drivers/hal/Src/py32f071_ll_usart.c", "libc_shim/libc_shim.c",
};

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .thumb,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_m0plus },
        .os_tag = .freestanding,
        .abi = .eabi,
    });
    const optimize = b.standardOptimizeOption(.{});

    const git_hash = b.option([]const u8, "git_hash", "Git commit hash") orelse "unknown";
    const build_date = b.option([]const u8, "build_date", "Build date string") orelse "unknown";
    const version_str = b.option([]const u8, "version", "Version string") orelse "v1.6.6";
    const project_name = b.option([]const u8, "project_name", "Project name") orelse "deltafw";
    const project_authors = b.option([]const u8, "authors", "Authors") orelse "qshosfw";
    const edition = b.option([]const u8, "edition", "Edition string") orelse "Custom";

    const fw = b.addExecutable(.{
        .name = "deltafw",
        .target = target,
        .optimize = optimize,
        .link_libc = false,
        .use_llvm = true,
        .use_lld = true,
    });
    fw.want_lto = true;
    fw.setLinkerScript(b.path("../src/core/py32f071xb_lld.ld"));
    fw.entry = .{ .symbol_name = "Reset_Handler" };
    fw.link_gc_sections = true;

    // Collect all defines and include paths
    var c_flags = std.ArrayList([]const u8).init(b.allocator);
    const common_flags: []const []const u8 = &.{
        "-fno-common",                        "-ffunction-sections",     "-fdata-sections",                 "-fno-math-errno",
        "-fno-trapping-math",                 "-fmerge-all-constants",   "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-std=gnu2x",                         "-fno-builtin",            "-Oz",                             "-fshort-enums",
        "-fno-exceptions",                    "-fno-threadsafe-statics", "-fomit-frame-pointer",            "-mno-unaligned-access",
        "-fno-stack-protector",               "-Wno-type-limits",        "-Wno-c2x-extensions",             "-Wno-switch-bool",
        "-Wno-implicit-function-declaration", "-Wno-gnu-designator",     "-Wno-builtin-requires-header",    "-Wno-tautological-constant-out-of-range-compare",
    };
    for (common_flags) |f| c_flags.append(f) catch unreachable;

    c_flags.append("-DPY32F071x8") catch unreachable;
    c_flags.append("-DUSE_FULL_LL_DRIVER") catch unreachable;
    c_flags.append("-DSQL_TONE=550") catch unreachable;
    c_flags.append(b.fmt("-DGIT_COMMIT_HASH=\"{s}\"", .{git_hash})) catch unreachable;
    c_flags.append(b.fmt("-DBUILD_DATE=\"{s}\"", .{build_date})) catch unreachable;
    c_flags.append(b.fmt("-DAUTHOR_STRING=\"{s}\"", .{project_name})) catch unreachable;
    c_flags.append(b.fmt("-DVERSION_STRING=\"{s}\"", .{version_str})) catch unreachable;
    c_flags.append(b.fmt("-DAUTHOR_STRING_2=\"{s}\"", .{project_authors})) catch unreachable;
    c_flags.append(b.fmt("-DVERSION_STRING_2=\"{s}\"", .{version_str})) catch unreachable;
    c_flags.append(b.fmt("-DEDITION_STRING=\"{s}\"", .{edition})) catch unreachable;

    var cond_sources = std.ArrayList([]const u8).init(b.allocator);
    const custom_fw = b.option(bool, "CUSTOM_FIRMWARE_MODS", "") orelse true;
    if (custom_fw) {
        c_flags.append("-DENABLE_CUSTOM_FIRMWARE_MODS") catch unreachable;
        c_flags.append("-DALERT_TOT=10") catch unreachable;
    }

    var need_crc = false;
    for (features) |feat| {
        if (b.option(bool, feat.name, "") orelse false) {
            c_flags.append(b.fmt("-D{s}", .{feat.define})) catch unreachable;
            for (feat.sources) |src| cond_sources.append(src) catch unreachable;
            for (feat.extra_includes) |inc| fw.addIncludePath(b.path(inc));
            if (std.mem.eql(u8, feat.name, "AIRCOPY") or std.mem.eql(u8, feat.name, "UART") or std.mem.eql(u8, feat.name, "USB")) need_crc = true;
        }
    }

    const spectrum_en = b.option(bool, "SPECTRUM", "") orelse false;
    if (spectrum_en) {
        c_flags.append("-DENABLE_SPECTRUM") catch unreachable;
        if (b.option(bool, "SPECTRUM_WATERFALL", "") orelse false) {
            c_flags.append("-DENABLE_SPECTRUM_WATERFALL") catch unreachable;
            c_flags.append("-DENABLE_SPECTRUM_ADVANCED") catch unreachable;
            cond_sources.append("../src/apps/spectrum/spectrum_waterfall.c") catch unreachable;
        } else cond_sources.append("../src/apps/spectrum/spectrum.c") catch unreachable;
    }

    if (need_crc) {
        cond_sources.append("../src/drivers/bsp/crc.c") catch unreachable;
        cond_sources.append("../src/drivers/bsp/eeprom_compat.c") catch unreachable;
    }

    const flags_slice = c_flags.items;

    for (core_sources) |src| {
        fw.addCSourceFile(.{
            .file = b.path(src),
            .flags = flags_slice,
        });
    }

    if (cond_sources.items.len > 0) {
        for (cond_sources.items) |src| {
            fw.addCSourceFile(.{
                .file = b.path(src),
                .flags = flags_slice,
            });
        }
    }

    for ([_][]const u8{ "../src", "../src/core", "../src/core/Inc", "../src/drivers/hal/Inc", "../src/drivers/cmsis/Include", "../src/drivers/cmsis/Device/PY32F071/Include", "../src/usb", ".." }) |inc| {
        fw.addIncludePath(b.path(inc));
    }
    fw.addSystemIncludePath(b.path("libc_shim"));

    // Startup assembly
    fw.addAssemblyFile(b.path("../src/core/startup_py32f071xx.s"));

    const bin = fw.addObjCopy(.{ .format = .bin });
    const install_bin = b.addInstallBinFile(bin.getOutput(), "deltafw.bin");
    b.installArtifact(fw);
    b.default_step.dependOn(&install_bin.step);
}
