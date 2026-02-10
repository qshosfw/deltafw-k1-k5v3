#!/usr/bin/env python3
"""
deltafw Build Configurator
A user-friendly menu for building deltafw firmware.
Supports whiptail (TUI) with text-mode fallback.
"""

import subprocess
import sys
import os

# =============================================================================
# Feature Database - ALL settings from CMakePresets.json
# =============================================================================

FEATURES = {
    # Communication
    "ENABLE_UART": {"title": "UART", "desc": "Serial UART communication", "category": "Comm", "size": 800, "default": True},
    "ENABLE_USB": {"title": "USB", "desc": "USB CDC virtual COM port", "category": "Comm", "size": 2000, "default": True},
    "ENABLE_AIRCOPY": {"title": "AirCopy", "desc": "Wireless config transfer", "category": "Comm", "size": 1200, "default": False},
    
    # Radio
    "ENABLE_FMRADIO": {"title": "FM Radio", "desc": "Broadcast FM receiver", "category": "Radio", "size": 1500, "default": False},
    "ENABLE_SPECTRUM": {"title": "Spectrum Analyzer", "desc": "RF spectrum view (F+5)", "category": "Radio", "size": 3500, "default": False},
    "ENABLE_SPECTRUM_EXTENSIONS": {"title": "Spectrum Extensions", "desc": "Extra spectrum features", "category": "Radio", "size": 500, "default": True},
    "ENABLE_NOAA": {"title": "NOAA Weather", "desc": "NOAA weather channels", "category": "Radio", "size": 200, "default": False},
    "ENABLE_VOX": {"title": "VOX", "desc": "Voice-activated transmit", "category": "Radio", "size": 400, "default": True},
    "ENABLE_VOICE": {"title": "Voice Prompts", "desc": "Spoken announcements", "category": "Radio", "size": 2000, "default": False},
    "ENABLE_ALARM": {"title": "Alarm", "desc": "Emergency tone", "category": "Radio", "size": 300, "default": False},
    "ENABLE_TX1750": {"title": "1750Hz Tone", "desc": "Repeater tone burst", "category": "Radio", "size": 200, "default": True},
    "ENABLE_DTMF_CALLING": {"title": "DTMF Calling", "desc": "Selective calling", "category": "Radio", "size": 600, "default": False},
    "ENABLE_AM_FIX": {"title": "AM Fix", "desc": "Improved AM reception", "category": "Radio", "size": 1000, "default": False},
    "ENABLE_WIDE_RX": {"title": "Wide RX", "desc": "Extended frequency range", "category": "Radio", "size": 100, "default": True},
    "ENABLE_TX_NON_FM": {"title": "TX When AM", "desc": "Allow TX in AM mode", "category": "Radio", "size": 50, "default": False},
    "ENABLE_SQUELCH_MORE_SENSITIVE": {"title": "Sensitive Squelch", "desc": "More squelch levels", "category": "Radio", "size": 100, "default": True},
    "ENABLE_FASTER_CHANNEL_SCAN": {"title": "Fast Scan", "desc": "Faster scanning", "category": "Radio", "size": 100, "default": True},
    "ENABLE_CTCSS_TAIL_PHASE_SHIFT": {"title": "CTCSS Tail Phase", "desc": "Tail elimination", "category": "Radio", "size": 150, "default": False},
    "ENABLE_NO_CODE_SCAN_TIMEOUT": {"title": "No Scan Timeout", "desc": "Disable scan timeout", "category": "Radio", "size": 50, "default": True},
    "ENABLE_SCAN_RANGES": {"title": "Scan Ranges", "desc": "Custom scan ranges", "category": "Radio", "size": 300, "default": True},
    "ENABLE_NARROWER_BW_FILTER": {"title": "Narrower BW", "desc": "Narrow bandwidth filter", "category": "Radio", "size": 100, "default": True},
    "ENABLE_BYP_RAW_DEMODULATORS": {"title": "Bypass Raw Demod", "desc": "Raw demod bypass", "category": "Radio", "size": 100, "default": False},
    "ENABLE_REDUCE_LOW_MID_TX_POWER": {"title": "Reduce TX Power", "desc": "Lower power levels", "category": "Radio", "size": 50, "default": False},
    
    # UI
    "ENABLE_BIG_FREQ": {"title": "Big Frequency", "desc": "Large freq display", "category": "UI", "size": 300, "default": True},
    "ENABLE_SMALL_BOLD": {"title": "Small Bold Font", "desc": "Bold small font", "category": "UI", "size": 200, "default": True},
    "ENABLE_CUSTOM_MENU_LAYOUT": {"title": "Custom Menu", "desc": "Enhanced menus", "category": "UI", "size": 200, "default": True},
    "ENABLE_RSSI_BAR": {"title": "RSSI Bar", "desc": "Signal strength bar", "category": "UI", "size": 400, "default": True},
    "ENABLE_MIC_BAR": {"title": "Audio Bar", "desc": "TX audio indicator", "category": "UI", "size": 300, "default": True},
    "ENABLE_RX_TX_TIMER_DISPLAY": {"title": "RX/TX Timer", "desc": "Duration timers", "category": "UI", "size": 200, "default": True},
    "ENABLE_INVERTED_LCD_MODE": {"title": "Inverted LCD", "desc": "Invert colors option", "category": "UI", "size": 100, "default": True},
    "ENABLE_LCD_CONTRAST_OPTION": {"title": "LCD Contrast", "desc": "Adjustable contrast", "category": "UI", "size": 100, "default": True},
    "ENABLE_REVERSE_BAT_SYMBOL": {"title": "Reverse Battery", "desc": "Flip battery icon", "category": "UI", "size": 50, "default": False},
    "ENABLE_SHOW_CHARGE_LEVEL": {"title": "Charge Level", "desc": "Show charging %", "category": "UI", "size": 150, "default": False},
    "ENABLE_USBC_CHARGING_INDICATOR": {"title": "USB-C Indicator", "desc": "USB-C charge status", "category": "UI", "size": 100, "default": False},
    "ENABLE_NAVIG_LEFT_RIGHT": {"title": "L/R Navigation", "desc": "Left/right key nav", "category": "UI", "size": 100, "default": True},
    
    # Apps
    "ENABLE_FLASHLIGHT": {"title": "Flashlight", "desc": "Backlight flashlight", "category": "Apps", "size": 200, "default": True},
    "ENABLE_APP_BREAKOUT_GAME": {"title": "Breakout Game", "desc": "Arcade game", "category": "Apps", "size": 800, "default": False},
    "ENABLE_KEEP_MEM_NAME": {"title": "Keep Mem Name", "desc": "Preserve ch names", "category": "Apps", "size": 100, "default": True},
    "ENABLE_COPY_CHAN_TO_VFO": {"title": "Copy to VFO", "desc": "Ch to VFO copy", "category": "Apps", "size": 150, "default": True},
    "ENABLE_RESCUE_OPERATIONS": {"title": "Rescue Ops", "desc": "Emergency features", "category": "Apps", "size": 400, "default": False},
    "ENABLE_SYSTEM_INFO_MENU": {"title": "System Info", "desc": "System info menu", "category": "Apps", "size": 300, "default": False},
    "ENABLE_RESET_CHANNEL_FUNCTION": {"title": "Reset Channel", "desc": "Reset ch settings", "category": "Apps", "size": 150, "default": False},
    "ENABLE_F_CAL_MENU": {"title": "Freq Cal Menu", "desc": "Freq calibration", "category": "Apps", "size": 200, "default": False},
    "ENABLE_PWRON_PASSWORD": {"title": "Power-On PWD", "desc": "Boot password", "category": "Apps", "size": 400, "default": False},
    "ENABLE_BOOT_BEEPS": {"title": "Boot Beeps", "desc": "Sound on boot", "category": "Apps", "size": 100, "default": False},
    "ENABLE_DEEP_SLEEP_MODE": {"title": "Deep Sleep", "desc": "Power saving", "category": "Apps", "size": 150, "default": True},
    "ENABLE_BOOT_RESUME_STATE": {"title": "Resume State", "desc": "Restore on boot", "category": "Apps", "size": 200, "default": True},
    "ENABLE_BLMIN_TMP_OFF": {"title": "BL Min Temp Off", "desc": "Temp backlight off", "category": "Apps", "size": 100, "default": False},
    
    # Bands
    "ENABLE_PMR446_FREQUENCY_BAND": {"title": "PMR446 Band", "desc": "EU PMR446 freqs", "category": "Bands", "size": 100, "default": False},
    "ENABLE_GMRS_FRS_MURS_BANDS": {"title": "GMRS/FRS/MURS", "desc": "US radio bands", "category": "Bands", "size": 150, "default": False},
    "ENABLE_FREQUENCY_LOCK_REGION_CA": {"title": "Freq Lock CA", "desc": "Canada freq lock", "category": "Bands", "size": 50, "default": True},
    
    # Debug
    "ENABLE_SERIAL_SCREENCAST": {"title": "Screencast", "desc": "Stream display", "category": "Debug", "size": 600, "default": False},
    "ENABLE_SWD": {"title": "SWD Debug", "desc": "SWD interface", "category": "Debug", "size": 100, "default": False},
    "ENABLE_UART_RW_BK_REGS": {"title": "UART BK Regs", "desc": "BK4819 via UART", "category": "Debug", "size": 300, "default": False},
    "ENABLE_FIRMWARE_DEBUG_LOGGING": {"title": "Debug Logging", "desc": "Debug output", "category": "Debug", "size": 400, "default": False},
    "ENABLE_AM_FIX_SHOW_DATA": {"title": "AM Fix Data", "desc": "AM fix debug", "category": "Debug", "size": 200, "default": False},
    "ENABLE_AGC_SHOW_DATA": {"title": "AGC Data", "desc": "AGC debug", "category": "Debug", "size": 200, "default": False},
    "ENABLE_EXTRA_UART_CMD": {"title": "Extra UART Cmds", "desc": "More UART cmds", "category": "Debug", "size": 300, "default": False},
    "ENABLE_REGA": {"title": "REGA Features", "desc": "REGA mods", "category": "Debug", "size": 200, "default": False},
}

CATEGORY_ORDER = ["Comm", "Radio", "UI", "Apps", "Bands", "Debug"]

PRESETS = {
    "Fusion": {"desc": "Full features for new radios (recommended)", "features": ["ENABLE_SPECTRUM", "ENABLE_FMRADIO", "ENABLE_VOX", "ENABLE_SERIAL_SCREENCAST", "ENABLE_SWD", "ENABLE_PMR446_FREQUENCY_BAND", "ENABLE_GMRS_FRS_MURS_BANDS"]},
    "Custom": {"desc": "Balanced features", "features": []},
    "Basic": {"desc": "Minimal stable build", "features": ["ENABLE_SPECTRUM", "ENABLE_FMRADIO"]},
    "Bandscope": {"desc": "Spectrum focused", "features": ["ENABLE_SPECTRUM", "ENABLE_AIRCOPY", "ENABLE_SERIAL_SCREENCAST", "ENABLE_PMR446_FREQUENCY_BAND", "ENABLE_GMRS_FRS_MURS_BANDS"]},
    "Broadcast": {"desc": "FM radio focused", "features": ["ENABLE_FMRADIO", "ENABLE_VOX", "ENABLE_AIRCOPY", "ENABLE_SERIAL_SCREENCAST", "ENABLE_PMR446_FREQUENCY_BAND", "ENABLE_GMRS_FRS_MURS_BANDS"]},
    "RescueOps": {"desc": "Emergency operations", "features": ["ENABLE_VOX", "ENABLE_AIRCOPY", "ENABLE_NOAA", "ENABLE_SERIAL_SCREENCAST", "ENABLE_RESCUE_OPERATIONS", "ENABLE_PMR446_FREQUENCY_BAND", "ENABLE_GMRS_FRS_MURS_BANDS"]},
    "Game": {"desc": "Includes Breakout game", "features": ["ENABLE_FMRADIO", "ENABLE_AIRCOPY", "ENABLE_APP_BREAKOUT_GAME", "ENABLE_SERIAL_SCREENCAST", "ENABLE_PMR446_FREQUENCY_BAND", "ENABLE_GMRS_FRS_MURS_BANDS"]},
}

FLASH_MAX = 118 * 1024  # 118KB - PY32F071 flash size
FLASH_BASE = 85 * 1024   # ~85KB base firmware estimate

# =============================================================================
# Text-mode fallback (always works)
# =============================================================================

def print_box(title):
    print(f"\n  ╔══════════════════════════════════════════════════════════╗")
    print(f"  ║ {title:^56} ║")
    print(f"  ╚══════════════════════════════════════════════════════════╝\n")

def text_menu(title, items):
    """Simple text-based menu."""
    print_box(title)
    for i, (tag, desc) in enumerate(items, 1):
        print(f"  {i}. {desc}")
    print()
    while True:
        try:
            choice = input("Enter choice (q=quit): ").strip()
            if choice.lower() == 'q':
                return None
            idx = int(choice) - 1
            if 0 <= idx < len(items):
                return items[idx][0]
        except (ValueError, EOFError, KeyboardInterrupt):
            return None
        print("Invalid choice.")

def text_yesno(title, text):
    """Simple yes/no prompt."""
    print(f"\n{title}")
    print(text)
    try:
        choice = input("\nProceed? [Y/n]: ").strip().lower()
        return choice != 'n'
    except (EOFError, KeyboardInterrupt):
        return False

def format_size(bytes_val):
    """Format bytes as human readable."""
    if bytes_val >= 1024:
        return f"{bytes_val / 1024:.1f}KB"
    return f"{bytes_val}B"

def calc_flash_usage(selected_features):
    """Calculate estimated flash usage."""
    usage = FLASH_BASE
    for feat in selected_features:
        if feat in FEATURES:
            usage += FEATURES[feat]["size"]
    return usage

# =============================================================================
# Whiptail wrappers (with fallback)
# =============================================================================

USE_WHIPTAIL = False

def init_whiptail():
    """Check if whiptail works."""
    global USE_WHIPTAIL
    if not sys.stdin.isatty():
        return False
    try:
        result = subprocess.run(["whiptail", "--version"], capture_output=True, timeout=2)
        USE_WHIPTAIL = True
        return True
    except:
        return False

def show_menu(title, text, items, height=18, width=65):
    """Show menu with whiptail or fallback."""
    if USE_WHIPTAIL:
        cmd = ["whiptail", "--title", title, "--menu", text, str(height), str(width), str(len(items))]
        for tag, desc in items:
            cmd.extend([tag, desc])
        try:
            # Don't capture stdout - whiptail needs it for the TUI
            # Only capture stderr where whiptail returns the selection
            result = subprocess.run(cmd, stderr=subprocess.PIPE, text=True, timeout=60)
            if result.returncode == 0:
                return result.stderr.strip()
            return None
        except Exception as e:
            print(f"Whiptail error: {e}")
    return text_menu(title, items)

def show_yesno(title, text, height=10, width=50):
    """Show yes/no with whiptail or fallback."""
    if USE_WHIPTAIL:
        cmd = ["whiptail", "--title", title, "--yesno", text, str(height), str(width)]
        try:
            result = subprocess.run(cmd, timeout=60)
            return result.returncode == 0
        except Exception as e:
            print(f"Whiptail error: {e}")
    return text_yesno(title, text)

def show_msgbox(title, text, height=12, width=60):
    """Show message box with whiptail or fallback."""
    if USE_WHIPTAIL:
        cmd = ["whiptail", "--title", title, "--msgbox", text, str(height), str(width)]
        try:
            subprocess.run(cmd, timeout=60)
            return
        except:
            pass
    print(f"\n{title}")
    print(text)
    input("\nPress Enter to continue...")

# =============================================================================
# Main Menu Logic
# =============================================================================

def build_firmware(preset):
    """Build firmware with preset."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    
    print(f"\n🔨 Building {preset}...")
    print(f"   Command: make {preset.lower()}\n")
    
    os.chdir(project_dir)
    os.system(f"./toolchain/compile-with-docker.sh {preset}")

def show_checklist(title, text, items, height=22, width=75):
    """Show checklist with whiptail or fallback."""
    if USE_WHIPTAIL:
        list_height = min(len(items), height - 8)
        cmd = ["whiptail", "--title", title, "--checklist", text, 
               str(height), str(width), str(list_height)]
        for tag, desc, status in items:
            cmd.extend([tag, desc, status])
        try:
            result = subprocess.run(cmd, stderr=subprocess.PIPE, text=True, timeout=120)
            if result.returncode == 0:
                selected = result.stderr.strip()
                if selected:
                    return [s.strip('"') for s in selected.split()]
                return []
            return None
        except Exception as e:
            print(f"Whiptail error: {e}")
    
    # Text fallback for checklist
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")
    print(text)
    print("\nCurrent selections (enter numbers to toggle, 'done' to finish):\n")
    
    selected = set(tag for tag, _, status in items if status == "ON")
    while True:
        for i, (tag, desc, _) in enumerate(items, 1):
            mark = "[x]" if tag in selected else "[ ]"
            print(f"  {i:2}. {mark} {desc}")
        print()
        try:
            choice = input("Toggle # (or 'done'): ").strip().lower()
            if choice == 'done':
                return list(selected)
            idx = int(choice) - 1
            if 0 <= idx < len(items):
                tag = items[idx][0]
                if tag in selected:
                    selected.remove(tag)
                else:
                    selected.add(tag)
        except (ValueError, EOFError, KeyboardInterrupt):
            return None


def show_custom_features(selected=None):
    """Show feature selection by category."""
    if selected is None:
        selected = set(var for var, info in FEATURES.items() if info.get("default"))
    
    while True:
        # Category menu
        cat_items = [("all", "📋 All Features")]
        for cat in CATEGORY_ORDER:
            count = sum(1 for f in FEATURES.values() if f["category"] == cat)
            enabled = sum(1 for v, f in FEATURES.items() if f["category"] == cat and v in selected)
            cat_items.append((cat, f"{cat} ({enabled}/{count} enabled)"))
        cat_items.append(("done", "✓ Done - Build with selected features"))
        cat_items.append(("cancel", "✗ Cancel"))
        
        usage = calc_flash_usage(selected)
        percent = int((usage / FLASH_MAX) * 100)
        
        choice = show_menu("🔧 Custom Features",
                          f"Flash: {format_size(usage)}/{format_size(FLASH_MAX)} ({percent}%)\n"
                          "Select category:", cat_items)
        
        if choice == "done":
            return selected
        elif choice == "cancel" or choice is None:
            return None
        elif choice in CATEGORY_ORDER or choice == "all":
            # Show features for this category
            if choice == "all":
                cats = CATEGORY_ORDER
            else:
                cats = [choice]
            
            items = []
            for cat in cats:
                for var, info in FEATURES.items():
                    if info["category"] == cat:
                        size_str = f"+{format_size(info['size'])}"
                        desc = f"[{cat:5}] {info['title'][:20]:20} {size_str:>7}"
                        status = "ON" if var in selected else "OFF"
                        items.append((var, desc, status))
            
            result = show_checklist(
                f"Features: {choice}",
                f"SPACE=toggle, ENTER=confirm",
                items
            )
            
            if result is not None:
                # Update selected based on shown features
                shown = set(item[0] for item in items)
                selected -= shown  # Remove all shown
                selected.update(result)  # Add back selected ones


def build_custom(features):
    """Build with custom features."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    
    usage = calc_flash_usage(features)
    
    print(f"\n🔧 Custom Build")
    print(f"   Features: {len(features)} enabled")
    print(f"   Est. flash: {format_size(usage)}\n")
    print("   CMake flags:")
    for f in sorted(features):
        info = FEATURES.get(f, {})
        print(f"     -D{f}=ON")
    print()
    
    # For now, print the flags - full custom build would need cmake integration
    print("Note: Custom builds require passing these flags to cmake.")
    print("For now, use a preset or modify CMakePresets.json.\n")


def main():
    """Main entry point."""
    init_whiptail()
    
    while True:
        items = [
            ("fusion", "🚀 Build Fusion (recommended for new radios)"),
            ("presets", "📦 Choose a different preset"),
            ("custom", "🔧 Custom build (select features)"),
            ("quit", "❌ Exit"),
        ]
        
        choice = show_menu("⚡ deltafw Build", 
                          "Select build option:", items)
        
        if choice == "fusion":
            if show_yesno("Build Fusion?", 
                         "Fusion is recommended for new UV-K1/K5 V3\n"
                         "radios with expanded flash.\n\n"
                         "Build now?"):
                build_firmware("Fusion")
                break
        
        elif choice == "presets":
            preset_items = [(name, info["desc"]) for name, info in PRESETS.items()]
            preset = show_menu("📦 Select Preset", "Choose preset:", preset_items)
            if preset and preset in PRESETS:
                if show_yesno(f"Build {preset}?", PRESETS[preset]["desc"]):
                    build_firmware(preset)
                    break
        
        elif choice == "custom":
            features = show_custom_features()
            if features:
                usage = calc_flash_usage(features)
                if usage > FLASH_MAX:
                    show_msgbox("⚠️ Flash Exceeded",
                               f"Selected: {format_size(usage)}\n"
                               f"Maximum: {format_size(FLASH_MAX)}\n\n"
                               "Please deselect some features.")
                else:
                    if show_yesno("Build Custom?",
                                 f"{len(features)} features enabled\n"
                                 f"Flash: {format_size(usage)}/{format_size(FLASH_MAX)}"):
                        build_custom(features)
                        break
        
        elif choice == "quit" or choice is None:
            break
    
    print("\n✨ Done!\n")

if __name__ == "__main__":
    main()
