#!/usr/bin/env python3
import sys
import os
import re
import shutil

# ANSI Colors
RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
MAGENTA = "\033[35m"
CYAN = "\033[36m"
WHITE = "\033[37m"
GRAY = "\033[90m"

BG_RED = "\033[41m"
BG_GREEN = "\033[42m"
BG_YELLOW = "\033[43m"
BG_BLUE = "\033[44m"

# Icons
ICON_BUILD = "🔨"
ICON_LINK = "🔗"
ICON_WARN = "⚠️ "
ICON_ERR = "❌"
ICON_INFO = "ℹ️ "
ICON_OK = "✅"
ICON_MEM = "💾"
ICON_CFG = "⚙️ "

def draw_progress_bar(percent, width=20):
    val = max(0, min(100, percent))
    filled = int(width * val / 100)
    empty = width - filled
    
    color = GREEN
    if val > 80: color = YELLOW
    if val > 90: color = RED
    
    bar = "█" * filled + "░" * empty
    return f"{color}{bar}{RESET} {BOLD}{val:>5.1f}%{RESET}"

def format_line(line):
    # Strip ANSI codes for clean matching
    clean_line = re.sub(r'\x1b\[[0-9;]*m', '', line).strip()
    if not clean_line:
        return ""

    # 1. Ninja Progress
    match = re.search(r'\[\s*(\d+)/(\d+)\]\s+(.*)', clean_line)
    if match:
        current, total, rest = match.groups()
        percent = int(current) / int(total) * 100
        prog_color = BLUE
        if percent > 90: prog_color = GREEN
        elif percent > 50: prog_color = CYAN
        
        icon = ICON_BUILD
        if "Linking" in rest: icon = ICON_LINK
        
        if "Building" in rest and "object" in rest:
            obj_match = re.search(r'(\S+\.(?:c|s|S|cpp|asm))(\.obj|\.o)?', rest, re.IGNORECASE)
            if obj_match:
                rest = f"Compiling {BOLD}{os.path.basename(obj_match.group(1))}{RESET}"
        
        return f"{prog_color}[{current:>3}/{total:<3}] {RESET}{icon} {rest}"

    # 2. CMake/Compiler Info & Headers
    if clean_line.startswith("-- ") or "Preset CMake variables" in clean_line or "Manually-specified variables" in clean_line:
        return f"{GRAY}{clean_line}{RESET}"

    # 3. Known Variables & Comments
    # Handle comments starting with // (including emojis)
    if clean_line.startswith("//"):
        return f"{GRAY}{clean_line}{RESET}"

    # Handle indented variable assignments: VAR="VAL" or VAR = VAL
    if "=" in clean_line:
        # Relaxed regex: allow spaces, quotes, optional type
        # Match: CMAKE_BUILD_TYPE="Release"
        # Match: ENABLE_FOO:BOOL="TRUE"
        # Match: EXE_NAME = deltafw
        var_match = re.search(r'^\s*([A-Za-z0-9_]+)(:[A-Z]+)?\s*=\s*["\']?([^"\']*)["\']?$', clean_line)
        if var_match:
             key, type_tag, val = var_match.groups()
             # Broaden the filter or just colorize all uppercase keys that look like config
             if key.isupper() or key in ["EXE_NAME", "Output file", "Git commit"]:
                 return f"  {ICON_CFG} {CYAN}{key}{RESET} = {BOLD}{MAGENTA}{val}{RESET}"

    # Handle "Property: Value" style
    if ":" in clean_line:
         # CMAKE_OBJCOPY: arm-none-eabi-objcopy
         # Git commit: ...
         prop_match = re.search(r'^\s*([A-Za-z0-9_ ]+):\s*(.*)$', clean_line)
         if prop_match:
             key, val = prop_match.groups()
             if "warning" not in key.lower() and "error" not in key.lower():
                 if key in ["Git commit", "Build date", "Output file", "Build type", "CMAKE_OBJCOPY"] or key.startswith("CMAKE_"):
                    return f"  {ICON_CFG} {CYAN}{key.strip()}{RESET}: {BOLD}{MAGENTA}{val.strip()}{RESET}"

    # 4. Warnings & Errors
    if "warning:" in clean_line.lower():
        return f"{YELLOW}{ICON_WARN} {clean_line}{RESET}"
    if "error:" in clean_line.lower():
        return f"{RED}{ICON_ERR} {clean_line}{RESET}"
    if "Manually-specified variables were not used" in clean_line:
        return f"{YELLOW}{clean_line}{RESET}"

    # Standalone variable names in warning block (e.g. ENABLE_FOO)
    if re.match(r'^\s*ENABLE_[A-Z0-9_]+\s*$', clean_line):
        return f"{YELLOW}{clean_line}{RESET}"

    # 5. GCC Context (Caret lines)
    # 40 | static void ...
    #    |             ^~~~~~~
    if re.match(r'^\s*\d+\s*\|', clean_line) or clean_line.startswith('|') or (clean_line.startswith('^') and '~' in clean_line):
        return f"{CYAN}{line.rstrip()}{RESET}"

    if "In function" in clean_line:
        return f"\n{MAGENTA}{clean_line}{RESET}"

    # 6. Memory Usage
    if "Memory region" in clean_line and "Used Size" in clean_line:
        return f"\n{BOLD}{WHITE}Memory Usage:{RESET}"
    
    # RAM:       13568 B        16 KB     82.81%
    if clean_line.startswith("RAM:") or clean_line.startswith("FLASH:"):
        parts = clean_line.split()
        if len(parts) >= 6:
            region = parts[0]
            used = f"{parts[1]} {parts[2]}"
            total = f"{parts[3]} {parts[4]}"
            perc_str = parts[5].replace('%', '')
            try:
                perc = float(perc_str)
                bar = draw_progress_bar(perc, width=15)
                return f"  {BOLD}{region:<8}{RESET} {used:>10} / {total:<8} {bar}"
            except ValueError:
                pass
        # Fallback if parsing fails but still looks like RAM/FLASH line
        return f"  {BOLD}{clean_line}{RESET}"

    return line.rstrip()

def main():
    current_progress = ""

    try:
        for line in sys.stdin:
            formatted = format_line(line)
            if not formatted:
                continue

            # Check if progress
            clean = re.sub(r'\x1b\[[0-9;]*m', '', formatted)
            is_progress = "[" in clean and "/" in clean and "]" in clean and clean.strip().startswith("[")

            if is_progress:
                current_progress = formatted
                # Overwrite line
                sys.stdout.write(f"\r\033[K{formatted}")
            else:
                # Clear line
                sys.stdout.write(f"\r\033[K")
                # Print log
                sys.stdout.write(f"{formatted}\n")
                # Restore progress
                if current_progress:
                    sys.stdout.write(f"{current_progress}")
            
            sys.stdout.flush()
            
        print() # Final newline

    except KeyboardInterrupt:
        pass
    except BrokenPipeError:
        pass


if __name__ == "__main__":
    main()
