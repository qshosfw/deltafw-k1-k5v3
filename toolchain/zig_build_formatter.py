#!/usr/bin/env python3
"""
Zig build output formatter — live progress display.

Parses 'zig build --verbose' to track individual file compilations.
Shows a live [N/M] progress bar and spinner.
"""
import sys
import re
import time
import threading
import os

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

ICON_BUILD = "🔨"
ICON_LINK = "🔗"
ICON_WARN = "⚠️ "
ICON_ERR = "❌"
ICON_OK = "✅"

def draw_progress_bar(percent, width=15):
    val = max(0, min(100, percent))
    filled = int(width * val / 100)
    empty = width - filled
    color = BLUE
    if val > 50: color = CYAN
    if val > 90: color = GREEN
    bar = "█" * filled + "░" * empty
    return f"{color}{bar}{RESET} {BOLD}{val:>5.1f}%{RESET}"

class LiveProgress:
    FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]

    def __init__(self, total_files=0):
        self.running = False
        self.start_time = time.time()
        self.total_files = total_files
        self.files_done = set()
        self.current_file = ""
        self.phase = "Compiling"
        self.lock = threading.Lock()

    def start(self):
        self.running = True
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def stop(self):
        self.running = False
        if hasattr(self, 'thread'):
            self.thread.join(timeout=1)
        sys.stderr.write("\r\033[K")
        sys.stderr.flush()

    def add_file(self, filename):
        with self.lock:
            basename = os.path.basename(filename)
            if basename not in self.files_done:
                self.files_done.add(basename)
                self.current_file = basename

    def set_phase(self, phase):
        with self.lock:
            self.phase = phase

    def print_above(self, text):
        sys.stderr.write(f"\r\033[K{text}\n")
        sys.stderr.flush()

    def _run(self):
        idx = 0
        while self.running:
            elapsed = time.time() - self.start_time
            frame = self.FRAMES[idx % len(self.FRAMES)]
            with self.lock:
                done = len(self.files_done)
                total = max(self.total_files, done)
                current = self.current_file
                phase = self.phase

            if total > 0:
                pct = (done / total) * 100
                bar = draw_progress_bar(pct)
                line = f"\r\033[K{CYAN}{frame}{RESET} [{done:>3}/{total:<3}] {bar} {ICON_BUILD} {BOLD}{current[:20]:<20}{RESET} {GRAY}({elapsed:.1f}s){RESET}"
            else:
                line = f"\r\033[K{CYAN}{frame}{RESET} {phase}... {GRAY}({elapsed:.1f}s){RESET}"

            sys.stderr.write(line)
            sys.stderr.flush()
            idx += 1
            time.sleep(0.08)

def format_line(line):
    clean = re.sub(r'\x1b\[[0-9;]*m', '', line).strip()
    if "error:" in clean:
        m = re.match(r'(.*?):(\d+):(\d+): error: (.*)', clean)
        if m:
            path, row, col, msg = m.groups()
            return f"{RED}{ICON_ERR} {BOLD}{path.split('/')[-1]}:{row}{RESET}{RED}:{col} {msg}{RESET}", True
        return f"{RED}{ICON_ERR} {clean}{RESET}", True
    if "warning:" in clean.lower() and "ld.lld" not in clean:
        return f"{YELLOW}{ICON_WARN} {clean}{RESET}", False
    if "note:" in clean:
        return f"  {GRAY}↳ {clean}{RESET}", False
    if re.match(r'^\s*\d+\s*\|', clean) or '^' in clean:
        return f"  {CYAN}{line.rstrip()}{RESET}", False
    return None, False

def main():
    total = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    progress = LiveProgress(total_files=total)
    progress.start()
    errors = 0

    try:
        for line in sys.stdin:
            # Synchronized progress logic via toolchain/prog.py
            if line.startswith("PROG:"):
                # Extract filename and update display
                filename = line.split(":")[-1].strip()
                progress.add_file(filename)
                continue

            # Detect compile command (backup logic)
            if "zig build-exe" in line or "zig cc" in line:
                source_files = re.findall(r'(\S+\.(?:c|s|S))', line)
                if source_files:
                    for f in source_files:
                        progress.add_file(f)
                    continue
            
            if "zig build-exe" in line: progress.set_phase("Linking")
            if "objcopy" in line: progress.set_phase("Packing")

            fmt, is_err = format_line(line)
            if fmt:
                progress.print_above(fmt)
                if is_err: errors += 1
    except KeyboardInterrupt:
        pass
    finally:
        progress.stop()

    elapsed = time.time() - progress.start_time
    if errors == 0:
        bar = draw_progress_bar(100)
        done = len(progress.files_done)
        print(f"[{done:>3}/{done:<3}] {bar} {ICON_OK} {BOLD}{GREEN}Complete{RESET} {GRAY}({elapsed:.1f}s){RESET}")
    else:
        print(f"\n{RED}{BOLD}Failed with {errors} error(s){RESET}")

if __name__ == "__main__":
    main()
