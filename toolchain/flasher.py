#!/usr/bin/env python3
import sys
import struct
import time
import serial
import math
import argparse
import os
import glob
from serial.tools import list_ports

# ========== TUI COLORS ==========
RESET = "\033[0m"
BOLD = "\033[1m"
INVERT = "\033[7m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
CYAN = "\033[36m"
WHITE = "\033[37m"
GRAY = "\033[90m"

# ========== CONFIG ==========
BAUDRATE = 38400
VID_PID_TARGET = "36b7:0001" 

# ========== PROTOCOL VALUES ==========
MSG_NOTIFY_DEV_INFO = 0x0518
MSG_NOTIFY_BL_VER   = 0x0530
MSG_PROG_FW         = 0x0519
MSG_PROG_FW_RESP    = 0x051A
MSG_REBOOT          = 0x05DD

OBFUS_TBL = [
  0x16, 0x6c, 0x14, 0xe6, 0x2e, 0x91, 0x0d, 0x40,
  0x21, 0x35, 0xd5, 0x40, 0x13, 0x03, 0xe9, 0x80
]

# ========== TUI HELPERS ==========

def print_header():
    print(f"\n  ⚡ {BOLD}deltafw Flasher{RESET}")
    print(f"  ══════════════════════════════════════════════════════════\n")

def log_info(msg):    print(f"{BLUE}{INVERT} INFO {RESET} {msg}")
def log_ok(msg):      print(f"{GREEN}{INVERT}  OK  {RESET} {msg}")
def log_warn(msg):    print(f"{YELLOW}{INVERT} WARN {RESET} {msg}")
def log_err(msg):     print(f"{RED}{INVERT} FAIL {RESET} {msg}"); sys.exit(1)
def log_debug(msg):   print(f"{GRAY}[DEBUG] {msg}{RESET}")

def draw_progress(current, total, width=25, status_override=None, color_override=None, stats=None):
    if status_override:
        # Full bar
        bar = "█" * width
        return f"{color_override}[{bar}]{RESET} {BOLD}{status_override}{RESET}"
    
    percent = (current / total) * 100
    val = max(0, min(100, percent))
    filled = int(width * val / 100)
    empty = width - filled
    
    color = BLUE
    if val > 60: color = GREEN
    
    bar = "█" * filled + "·" * empty
    stats_str = f" ({stats})" if stats else ""
    return f"{color}[{bar}]{RESET} {BOLD}{val:>5.1f}%{RESET}{GRAY}{stats_str}{RESET} {GRAY}({current}/{total}){RESET}"



# ========== PORT SELECTION ==========

def find_port(custom_port=None):
    if custom_port:
        return custom_port, "Custom"

    ports = list(list_ports.comports())
    
    # Priority 1: Match VID:PID
    for p in ports:
        if p.vid is not None and p.pid is not None:
            vid_pid = f"{p.vid:04x}:{p.pid:04x}".lower()
            if vid_pid == VID_PID_TARGET:
                return p.device, f"Auto-Detected ({vid_pid})"
    
    # Priority 2: Candidates (ACM/USB)
    candidates = [p for p in ports if "USB" in p.device or "ACM" in p.device]
    if len(candidates) == 1:
        return candidates[0].device, "Auto-Detected (Candidate)"
        
    return None, None

# ========== PROTOCOL CLASS ==========

class Protocol:
    def __init__(self, port):
        self.port = port
        self.ser = None
        self.rx_buffer = bytearray()
        self.verbose = False
        self.total_rx = 0

    def connect(self):
        try:
            # SerialTool parity settings: 0.0001s timeout, no DTR/RTS touch
            self.ser = serial.Serial(
                self.port, 
                baudrate=BAUDRATE, 
                timeout=0.0001, 
                write_timeout=None
            )
            self.ser.reset_input_buffer()
        except Exception as e:
            log_err(f"Connection failed: {e}")

    def close(self):
        if self.ser: self.ser.close()

    def crc16(self, data):
        crc = 0
        for b in data:
            crc ^= b << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF
        return crc & 0xFFFF

    def obfuscate(self, data):
        for i in range(len(data)):
            data[i] ^= OBFUS_TBL[i % 16]

    def make_packet(self, msg_type, payload=b''):
        # Body Construction
        inner_len = len(payload)
        msg_body = bytearray(struct.pack('<HH', msg_type, inner_len) + payload)
        
        # Padding (Even Length)
        if len(msg_body) % 2 != 0:
            msg_body.append(0x00)
            
        # CRC
        crc = self.crc16(msg_body)
        
        # Encrypt Body+CRC
        to_obfus = bytearray(msg_body) + struct.pack('<H', crc)
        self.obfuscate(to_obfus)
        
        # Packet Wrap
        pkt = bytearray()
        pkt += struct.pack('<H', 0xCDAB)
        pkt += struct.pack('<H', len(msg_body))
        pkt += to_obfus
        pkt += struct.pack('<H', 0xBADC)
        return pkt

    def send(self, msg_type, payload=b''):
        pkt = self.make_packet(msg_type, payload)
        if self.verbose: log_debug(f"TX: {pkt.hex()}")
        self.ser.write(pkt)

    def fetch_message(self):
        if self.ser.in_waiting:
            try:
                c = self.ser.read(self.ser.in_waiting)
                if c:
                    self.total_rx += len(c)
                    if self.verbose: log_debug(f"RX: {c.hex()}")
                    self.rx_buffer.extend(c)
            except OSError: pass

        buf = self.rx_buffer
        if len(buf) < 8: return None

        # Find Header
        idx = buf.find(b'\xAB\xCD')
        if idx == -1:
            if buf[-1] == 0xAB: del buf[:-1]
            else: del buf[:]
            return None
        
        if idx > 0:
            del buf[:idx]
            idx = 0

        if len(buf) < 4: return None
        
        msg_len = struct.unpack('<H', buf[2:4])[0]
        if msg_len > 1024:
            del buf[:2]
            return None

        full_size = msg_len + 8
        if len(buf) < full_size: return None
        
        tail = struct.unpack('<H', buf[full_size-2:full_size])[0]
        if tail != 0xBADC:
            del buf[:2]
            return None
            
        encrypted = buf[4 : full_size-2]
        self.obfuscate(encrypted)
        
        if len(encrypted) < 4:
            del buf[:full_size]
            return None

        m_type = struct.unpack('<H', encrypted[0:2])[0]
        payload = encrypted[4:-2]
        
        del buf[:full_size]
        return (m_type, payload)


# ========== FLASHER LOGIC ==========

def flash_process(proto, fw_data):
    # 1. Handshake
    log_info("Waiting for bootloader (Hold PTT + Turn ON)...")
    
    start = time.time()
    last_ts = 0
    acc = 0
    dev_info = None
    
    spinner = "|/-\\"
    spin_idx = 0
    
    while time.time() - start < 15:
        msg = proto.fetch_message()
        if not msg:
            # Spinner
            elapsed = time.time() - start
            if elapsed > 1.0:
                 sys.stdout.write(f"\r {spinner[spin_idx]} ")
                 sys.stdout.flush()
                 spin_idx = (spin_idx + 1) % 4
            time.sleep(0.01)
            continue
            
        m_type, data = msg
        sys.stdout.write("\r   \r") # Clear spinner
        
        if m_type == MSG_NOTIFY_DEV_INFO:
            now = time.time()
            dt = (now - last_ts) * 1000
            last_ts = now
            
            # JS Timing Logic
            if 5 <= dt <= 1000:
                acc += 1
                if acc >= 5:
                    uid = data[:16]
                    ver_raw = data[16:].split(b'\x00')[0]
                    dev_info = (uid, ver_raw.decode('utf-8', errors='ignore'))
                    break
            else:
                acc = 0
                
    if not dev_info:
        log_err("Timeout. Bootloader not found.")
        
    uid, ver = dev_info
    log_ok(f"Bootloader Found: {BOLD}{ver}{RESET} (UID: {uid.hex()[:8]}...)")
    
    # 2. Handshake Reply
    # log_info("Completing handshake...") 
    # ^ Redundant, implied by moving to next step.
    
    acc = 0
    retries = 0
    while acc < 3 and retries < 20:
        time.sleep(0.05)
        msg = proto.fetch_message()
        if msg and msg[0] == MSG_NOTIFY_DEV_INFO:
            if acc == 0:
                # Send first 4 chars of BL Version
                payload = ver[:4].encode('utf-8')
                proto.send(MSG_NOTIFY_BL_VER, payload)
            acc += 1
        retries += 1
    
    time.sleep(0.2)
    while proto.fetch_message(): pass
    
    # 3. Flash
    log_info("Flashing...")
    
    PAGE_SIZE = 256
    total_pages = math.ceil(len(fw_data) / PAGE_SIZE)
    ts = int(time.time()) & 0xFFFFFFFF
    
    start_flash = time.time()
    
    for page in range(total_pages):
        offset = page * PAGE_SIZE
        chunk = fw_data[offset : offset + PAGE_SIZE]
        
        # Payload
        payload = bytearray(268)
        struct.pack_into('<IHH', payload, 0, ts, page, total_pages)
        payload[12 : 12 + len(chunk)] = chunk
        
        # Write Loop
        success = False
        attempts = 0
        while not success and attempts < 3:
            proto.send(MSG_PROG_FW, payload)
            
            # Wait ACK
            ack_wait = time.time()
            while time.time() - ack_wait < 3.0:
                resp = proto.fetch_message()
                if not resp: 
                    time.sleep(0.005)
                    continue
                rt, rd = resp
                if rt == MSG_PROG_FW_RESP:
                   if len(rd) >= 8:
                       r_page, r_err = struct.unpack_from('<HH', rd, 4)
                       if r_page == page and r_err == 0:
                           success = True
                           break
            
            if not success:
               attempts += 1
        
        if not success:
            print()
            log_err(f"Write failed at page {page}")
        
        # UI
        elapsed = time.time() - start_flash
        rate = (offset + len(chunk)) / elapsed / 1024 if elapsed > 0 else 0
        stats_str = f"{elapsed:.1f}s @ {rate:.1f} KB/s"
        
        if page + 1 == total_pages:
            line = draw_progress(page+1, total_pages, status_override=f"Complete ({stats_str})", color_override=GREEN)
        else:
            line = draw_progress(page+1, total_pages, stats=stats_str)
            
        sys.stdout.write(f"\r\033[K{line}")
        sys.stdout.flush()
        
    print() # Newline
    
    # Reboot
    proto.send(MSG_REBOOT)
    

def main():
    parser = argparse.ArgumentParser("flasher")
    parser.add_argument("--port", "-p", help="Serial port")
    parser.add_argument("file", nargs="?", help="Firmware file")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args()
    
    print_header()
    
    # Resolve File
    fw_file = args.file
    if not fw_file:
        # Look in build/ for recent .bin files (preset-named)
        bins = sorted(glob.glob("build/deltafw.*.bin"), key=os.path.getmtime, reverse=True)
        valid = [b for b in bins if not b.endswith("packed.bin")]
        if valid: 
            fw_file = valid[0]
        elif os.path.exists("firmware.bin"):
            fw_file = "firmware.bin"
            
    if not fw_file or not os.path.exists(fw_file):
        log_err("No firmware file found.")

    # File Info
    size = os.path.getsize(fw_file)
    log_info(f"Firmware: {BOLD}{os.path.basename(fw_file)}{RESET} ({size/1024:.1f} KB)")
    
    # Resolve Port
    port, desc = find_port(args.port)
    if not port: 
        log_err("No suitable port found.")
        
    log_info(f"Port: {BOLD}{port}{RESET}")
    
    # Connect
    proto = Protocol(port)
    proto.verbose = args.verbose
    proto.connect()
    
    try:
        flash_process(proto, open(fw_file, "rb").read())
    except KeyboardInterrupt:
        print()
        log_warn("Cancelled.")
    finally:
        proto.close()

if __name__ == "__main__":
    main()
