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
UNDERLINE = "\033[4m"

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
        
        # Prettify "Compiling C object ..."
        # Match: Compiling C object deltafw.elf.elf.p/src_ui_bitmaps.c.o
        obj_match = re.search(r'Compiling \w+ object \S+/(\S+\.(?:c|s|S|cpp|asm))(?:\.o)?', rest, re.IGNORECASE)
        if obj_match:
            rest = f"{BOLD}{obj_match.group(1)}{RESET}"
        
        return f"{prog_color}[{current:>3}/{total:<3}] {RESET}{icon} {rest}"

    # 2. Suppress/Prettify INFO lines
    if clean_line.startswith("INFO:"):
        # "INFO: autodetecting backend as ninja" -> Skip or Simplify
        if "backend" in clean_line or "calculating" in clean_line:
            return ""
        return f"{GRAY}{clean_line}{RESET}"

    # 3. Meson Configuration Setup
    # "The Meson build system" -> Title
    if "The Meson build system" in clean_line:
        return f"{BOLD}{MAGENTA}Meson Build System{RESET}"
    
    # "running build ..."
    if clean_line.startswith("Cleaning..."):
        return f"{YELLOW}Cleaning...{RESET}"

    # "Project name: deltafw"
    if clean_line.startswith("Project name:"):
        return f"{ICON_CFG} {CYAN}Project:{RESET} {BOLD}{clean_line.split(':')[1].strip()}{RESET}"
    
    # "Project version: 1.0.0"
    if clean_line.startswith("Project version:"):
        return f"{ICON_CFG} {CYAN}Version:{RESET} {BOLD}{clean_line.split(':')[1].strip()}{RESET}"

    # "C compiler for the host machine: ..."
    # Simplify to "C Compiler: arm-none-eabi-gcc"
    comp_match = re.search(r'(C|C\+\+) (compiler|linker) for the (host|build) machine: (.*)', clean_line)
    if comp_match:
        lang, tool, machine, details = comp_match.groups()
        # Only show host machine tools (ARM), suppress build machine (x86) to reduce noise
        if machine == "build":
            return "" 
        
        # Extract just the compiler name if possible (before parentheses)
        short_details = details.split('(')[0].strip()
        return f"  {GRAY}{lang} {tool}:{RESET} {short_details}"

    # "Program arm-none-eabi-objcopy found: YES"
    prog_match = re.search(r'Program (.*) found: YES', clean_line)
    if prog_match:
         return f"  {ICON_OK} Found {BOLD}{prog_match.group(1)}{RESET}"

    # "Build targets in project: 3"
    if clean_line.startswith("Build targets"):
         return f"{ICON_INFO} {clean_line}"

    # "Found ninja-1.11.1 at ..."
    if clean_line.startswith("Found ninja"):
         return f"{ICON_OK} {clean_line}"

    # User defined options (indentation)
    if clean_line.strip() == "User defined options":
        return f"\n{BOLD}{UNDERLINE}Configuration:{RESET}"

    # 4. Warnings & Errors
    if "warning:" in clean_line.lower():
        return f"{YELLOW}{ICON_WARN} {clean_line}{RESET}"
    if "error:" in clean_line.lower():
        return f"{RED}{ICON_ERR} {clean_line}{RESET}"
    if "Manually-specified variables were not used" in clean_line:
        # This is often noise in meson
        return ""

    # 5. GCC Context (Caret lines)
    if re.match(r'^\s*\d+\s*\|', clean_line) or clean_line.startswith('|') or (clean_line.startswith('^') and '~' in clean_line):
        return f"{CYAN}{line.rstrip()}{RESET}"

    if "In function" in clean_line:
        return f"\n{MAGENTA}{clean_line}{RESET}"

    # 6. Memory Usage
    if "Memory region" in clean_line and "Used Size" in clean_line:
        return f"\n{BOLD}{WHITE}Memory Usage:{RESET}"
    
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
        return f"  {BOLD}{clean_line}{RESET}"

    # Handle "Property: Value" style (generic) - keep it low noise
    if ":" in clean_line and not clean_line.startswith("Compiling"):
         # Check if it looks like a config line
         # EDITION_STRING : Fusion
         prop_match = re.search(r'^\s*([A-Za-z0-9_]+)\s*:\s*(.*)$', clean_line)
         if prop_match:
             key, val = prop_match.groups()
             # Filter out some noise
             if key not in ["buildtype", "warning_level", "werror", "b_staticpic"]:
                return f"  {CYAN}{key:<30}{RESET} : {BOLD}{val}{RESET}"

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
