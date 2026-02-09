#!/usr/bin/env python3
"""
QSH Container Tool - Official Reference Implementation
Specification Revision: 1
"""

import struct
import hashlib
import binascii
import sys
import argparse
import time
import os
from datetime import datetime

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

# ========== TUI HELPERS ==========

def print_header():
    print(f"\n  ⚡ {BOLD}deltafw QSH Tool{RESET}")
    print(f"  ══════════════════════════════════════════════════════════\n")

def log_info(msg):    print(f"{BLUE}{INVERT} INFO {RESET} {msg}")
def log_ok(msg):      print(f"{GREEN}{INVERT}  OK  {RESET} {msg}")
def log_warn(msg):    print(f"{YELLOW}{INVERT} WARN {RESET} {msg}")
def log_err(msg):     print(f"{RED}{INVERT} FAIL {RESET} {msg}"); sys.exit(1)
def log_debug(msg):   print(f"{GRAY}[DEBUG] {msg}{RESET}")

# =============================================================================
# 1. CONSTANTS & PROTOCOL DEFINITIONS
# =============================================================================

# File Header
MAGIC_BYTES = b'\xe6QSH\r\n\x1a\n'  # 8 bytes
SPEC_REV    = 1                   # UINT16

# --- TLV Tag Map ---

# 1.0 Global Metadata Tags (0x01 - 0x0F)
TAG_G_TITLE       = 0x01 # ASCII
TAG_G_AUTHOR      = 0x02 # ASCII
TAG_G_DESC        = 0x03 # ASCII
TAG_G_DATE        = 0x04 # UINT32 (Unix Timestamp)
TAG_G_TYPE        = 0x05 # ASCII: 'firmware', 'resource', 'dump', 'config', 'channels'
TAG_G_COMPRESSION = 0x06 # ASCII: 'none', 'deflate'
TAG_TERMINATOR    = 0x00 # End of TLV block

# 2.0 Firmware Blob Tags (0x10 - 0x2F)
TAG_F_NAME        = 0x11 # ASCII
TAG_F_VERSION     = 0x12 # ASCII
TAG_F_DESC        = 0x13 # ASCII
TAG_F_AUTHOR      = 0x14 # ASCII
TAG_F_LICENSE     = 0x15 # ASCII (New)
TAG_F_ARCH        = 0x16 # ASCII (e.g. 'cortex-m0+')
TAG_F_TARGET      = 0x17 # ASCII (e.g. 'uvk5,uvk1')
TAG_F_DATE        = 0x18 # UINT32
TAG_F_GIT         = 0x19 # ASCII (New)
TAG_F_BOOT_MIN    = 0x1A # ASCII (New)
TAG_F_PAGE_SIZE   = 0x1B # UINT32 (New)
TAG_F_BASE_ADDR   = 0x1C # UINT32 (New)

# 3.0 Resource Blob Tags (0x30 - 0x3F)
TAG_R_LABEL       = 0x30 # ASCII
TAG_R_TYPE        = 0x31 # ASCII

# 4.0 Memory/Dump/Config Tags (0x40 - 0x5F)
TAG_D_LABEL       = 0x40 # ASCII
TAG_D_START_ADDR  = 0x41 # UINT32
TAG_D_END_ADDR    = 0x42 # UINT32
TAG_D_WRITABLE    = 0x43 # BOOL (1 byte)
TAG_D_CH_COUNT    = 0x44 # UINT16
TAG_D_CH_NAMES    = 0x45 # ASCII (Comma separated)

# 5.0 Radio Identity Tags (0x60 - 0x6F)
# Used to link a dump/config to a specific physical radio
TAG_ID_FW_STR     = 0x60 # ASCII (FW string sent over serial)
TAG_ID_RADIO_UID  = 0x61 # UINT64 (Unique Hardware ID)
TAG_ID_LABEL      = 0x62 # ASCII (User Label/SN)

# 6.0 Auxiliary/Extra Blob Tags (0x70 - 0x7F)
TAG_X_TYPE        = 0x70 # ASCII ('elf', 'map', 'sym')
TAG_X_LABEL       = 0x71 # ASCII
TAG_X_COMPILER    = 0x72 # ASCII

# Dictionary for Human Readable Debugging
TAG_MAP = {
    TAG_G_TITLE: "Title", TAG_G_AUTHOR: "Author", TAG_G_DESC: "Description", 
    TAG_G_DATE: "Date", TAG_G_TYPE: "Global Type", TAG_G_COMPRESSION: "Compression",
    TAG_F_NAME: "FW Name", TAG_F_VERSION: "FW Version", TAG_F_DESC: "FW Desc",
    TAG_F_AUTHOR: "FW Author", TAG_F_LICENSE: "License", TAG_F_ARCH: "Architecture",
    TAG_F_TARGET: "Target HW", TAG_F_DATE: "Build Date", TAG_F_GIT: "Git Commit",
    TAG_F_BOOT_MIN: "Min Bootloader", TAG_F_PAGE_SIZE: "Page Size", TAG_F_BASE_ADDR: "Flash Base Addr",
    TAG_R_LABEL: "Res Label", TAG_R_TYPE: "Res Type",
    TAG_D_LABEL: "Mem Label", TAG_D_START_ADDR: "Start Addr", TAG_D_END_ADDR: "End Addr",
    TAG_D_WRITABLE: "Writable", TAG_D_CH_COUNT: "Channel Count", TAG_D_CH_NAMES: "Channel Names",
    TAG_ID_FW_STR: "Source FW String", TAG_ID_RADIO_UID: "Radio UID", TAG_ID_LABEL: "Radio Label",
    TAG_X_TYPE: "Aux Type", TAG_X_LABEL: "Aux Label", TAG_X_COMPILER: "Compiler"
}

# =============================================================================
# 2. LOW-LEVEL TLV ENGINE
# =============================================================================

def pack_u16(n): return struct.pack('<H', n)
def pack_u32(n): return struct.pack('<I', n)
def pack_u64(n): return struct.pack('<Q', n)
def pack_bool(b): return b'\x01' if b else b'\x00'

def create_tlv(tag, value):
    """
    Creates a TLV binary block (Tag-Length-Value).
    Auto-detects format based on Tag definition to ensure strict type compliance.
    """
    payload = b''
    
    # --- Integer (UINT32) Tags ---
    if tag in [TAG_G_DATE, TAG_F_DATE, TAG_F_PAGE_SIZE, TAG_F_BASE_ADDR, 
               TAG_D_START_ADDR, TAG_D_END_ADDR]:
        payload = pack_u32(int(value))
    
    # --- Integer (UINT16) Tags ---
    elif tag in [TAG_D_CH_COUNT]:
        payload = pack_u16(int(value))
        
    # --- Integer (UINT64) Tags ---
    elif tag in [TAG_ID_RADIO_UID]:
        payload = pack_u64(int(value))
        
    # --- Boolean Tags ---
    elif tag in [TAG_D_WRITABLE]:
        payload = pack_bool(value)
        
    # --- String/ASCII Tags ---
    # Default behavior for unknown tags is also bytes/string
    else:
        if isinstance(value, str):
            payload = value.encode('ascii', errors='ignore')
        elif isinstance(value, bytes):
            payload = value
        elif isinstance(value, int):
             # Fallback if int passed to string tag
            payload = str(value).encode('ascii')
            
    # Assemble: Tag (2) + Len (2) + Value (N)
    return pack_u16(tag) + pack_u16(len(payload)) + payload

def parse_tlv_stream(data, start_offset=0):
    """
    Parses a stream of TLVs until a Terminator (0x00) or end of stream.
    Returns ({tag: value}, next_offset)
    """
    attributes = {}
    ptr = start_offset
    
    while ptr < len(data):
        # Check buffer limits
        if ptr + 4 > len(data): break 
        
        tag = struct.unpack('<H', data[ptr:ptr+2])[0]
        length = struct.unpack('<H', data[ptr+2:ptr+4])[0]
        ptr += 4
        
        if tag == TAG_TERMINATOR:
            break
            
        if ptr + length > len(data):
            log_warn(f"TLV overflow at tag {tag}")
            break
            
        raw_val = data[ptr:ptr+length]
        ptr += length
        
        # --- Type Decoding ---
        val = raw_val
        
        # UINT32
        if tag in [TAG_G_DATE, TAG_F_DATE, TAG_F_PAGE_SIZE, TAG_F_BASE_ADDR, 
                   TAG_D_START_ADDR, TAG_D_END_ADDR]:
            if len(raw_val) == 4: val = struct.unpack('<I', raw_val)[0]
            
        # UINT16
        elif tag in [TAG_D_CH_COUNT]:
            if len(raw_val) == 2: val = struct.unpack('<H', raw_val)[0]
            
        # UINT64
        elif tag in [TAG_ID_RADIO_UID]:
            if len(raw_val) == 8: val = struct.unpack('<Q', raw_val)[0]
            
        # BOOL
        elif tag in [TAG_D_WRITABLE]:
            val = (raw_val != b'\x00')
            
        # STRINGS
        else:
            try:
                val = raw_val.decode('ascii')
            except:
                pass # Keep as bytes
                
        attributes[tag] = val
        
    return attributes, ptr

# =============================================================================
# 3. CONTAINER LOGIC
# =============================================================================

class QSHBlob:
    def __init__(self, data=b'', metadata=None):
        self.data = data
        self.metadata = metadata if metadata else {}

    def to_bytes(self):
        # 1. Build TLV Header
        tlv_block = b''
        for t, v in self.metadata.items():
            if v is not None:
                tlv_block += create_tlv(t, v)
        tlv_block += create_tlv(TAG_TERMINATOR, b'')
        
        # 2. Concatenate TLV + Data
        blob_body = tlv_block + self.data
        
        # 3. Prepend Size (UINT32) of the whole blob structure
        # Size = Length(TLV + Data)
        return pack_u32(len(blob_body)) + blob_body

class QSHFile:
    def __init__(self):
        self.global_meta = {}
        self.blobs = []

    def set_global(self, meta_dict):
        self.global_meta = {k: v for k, v in meta_dict.items() if v is not None}
        # Auto-set date if missing
        if TAG_G_DATE not in self.global_meta:
            self.global_meta[TAG_G_DATE] = int(time.time())

    def add_blob(self, data, meta_dict):
        clean_meta = {k: v for k, v in meta_dict.items() if v is not None}
        self.blobs.append(QSHBlob(data, clean_meta))

    def save(self, filename):
        # 1. Header
        out = MAGIC_BYTES + pack_u16(SPEC_REV)
        
        # 2. Global TLVs
        for t, v in self.global_meta.items():
            out += create_tlv(t, v)
        out += create_tlv(TAG_TERMINATOR, b'')
        
        # 3. Blobs
        for b in self.blobs:
            out += b.to_bytes()
            
        # 4. SHA-256 Checksum
        checksum = hashlib.sha256(out).digest()
        
        with open(filename, 'wb') as f:
            f.write(out)
            f.write(checksum)
            
        log_ok(f"Created {BOLD}{filename}{RESET}")
        log_info(f"Size: {BOLD}{len(out)+32}{RESET} bytes")
        log_info(f"SHA-256: {binascii.hexlify(checksum).decode()}")

    @staticmethod
    def load(filename):
        with open(filename, 'rb') as f:
            raw = f.read()
            
        if len(raw) < 42: # Min header + empty TLV + hash
            log_err("File too short")
            return None
            
        # Verify Hash
        body = raw[:-32]
        file_hash = raw[-32:]
        calc_hash = hashlib.sha256(body).digest()
        
        if file_hash != calc_hash:
            log_err("INTEGRITY CHECK FAILED: Hash mismatch")
            return None
            
        # Header
        if body[:8] != MAGIC_BYTES:
            log_err("Invalid Magic Bytes")
            return None
            
        rev = struct.unpack('<H', body[8:10])[0]
        
        # Global TLV
        g_meta, ptr = parse_tlv_stream(body, 10)
        
        blobs = []
        while ptr < len(body):
            # Read Blob Size
            if ptr + 4 > len(body): break
            b_size = struct.unpack('<I', body[ptr:ptr+4])[0]
            ptr += 4
            
            if ptr + b_size > len(body):
                log_warn("Blob truncated")
                break
                
            # Blob Content
            b_content = body[ptr : ptr+b_size]
            b_meta, b_head_len = parse_tlv_stream(b_content, 0)
            b_data = b_content[b_head_len:]
            
            blobs.append(QSHBlob(b_data, b_meta))
            ptr += b_size
            
        container = QSHFile()
        container.global_meta = g_meta
        container.blobs = blobs
        return container

# =============================================================================
# 4. CLI INTERFACE
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="Quansheng QSH Container Tool", formatter_class=argparse.RawTextHelpFormatter)
    subparsers = parser.add_subparsers(dest="cmd", required=True, help="Action to perform")

    # --- PACK ---
    p_pack = subparsers.add_parser('pack', help="Create a .qsh file")
    p_pack.add_argument('output', help="Output filename")
    
    # Global Metadata Group
    g_grp = p_pack.add_argument_group('Global Metadata')
    g_grp.add_argument('--title', required=True, help="Package Title")
    g_grp.add_argument('--author', default="Unknown", help="Author")
    g_grp.add_argument('--desc', help="Description")
    g_grp.add_argument('--type', required=True, choices=['firmware', 'resource', 'dump', 'config', 'channels'], help="Type of content")
    g_grp.add_argument('--compress', default='none', help="Compression algo (none, deflate)")

    # Firmware Specific Group
    f_grp = p_pack.add_argument_group('Firmware Blob Options')
    f_grp.add_argument('--fw-bin', help="Firmware binary file")
    f_grp.add_argument('--fw-ver', help="Version string (v1.0.0)")
    f_grp.add_argument('--fw-license', help="License (GPL, MIT)")
    f_grp.add_argument('--fw-arch', default="cortex-m0+", help="Architecture")
    f_grp.add_argument('--fw-target', help="Target Hardware (uvk5, etc)")
    f_grp.add_argument('--fw-git', help="Git Commit Hash")
    f_grp.add_argument('--fw-boot-min', help="Min Bootloader Version")
    f_grp.add_argument('--fw-page-size', type=int, help="Flash Page Size")
    f_grp.add_argument('--fw-base-addr', help="Flash Base Address (Hex or Int)")

    # Memory/Dump/Config Group
    d_grp = p_pack.add_argument_group('Dump/Config/Channel Options')
    d_grp.add_argument('--mem-bin', help="Memory dump binary file")
    d_grp.add_argument('--mem-label', help="Label for the memory segment")
    d_grp.add_argument('--mem-start', help="Start Address (Hex/Int)")
    d_grp.add_argument('--mem-end', help="End Address (Hex/Int)")
    d_grp.add_argument('--mem-writable', action='store_true', help="Mark as Writable")
    
    # Radio Identity (For Dumps/Config)
    i_grp = p_pack.add_argument_group('Radio Identity (For Dumps)')
    i_grp.add_argument('--radio-uid', help="Radio Unique ID (Hex/Int)")
    i_grp.add_argument('--radio-fw-id', help="Radio Firmware String (Serial)")
    i_grp.add_argument('--radio-label', help="Radio User Label/SN")

    # Channels Specific
    c_grp = p_pack.add_argument_group('Channels Options')
    c_grp.add_argument('--ch-count', type=int, help="Number of channels")
    c_grp.add_argument('--ch-names', help="Comma separated channel names")

    # Auxiliary/Resource
    x_grp = p_pack.add_argument_group('Auxiliary/Resource')
    x_grp.add_argument('--aux-file', help="Auxiliary file (ELF/Map)")
    x_grp.add_argument('--aux-type', default='elf', help="Aux type")
    x_grp.add_argument('--res-file', help="Resource binary")
    x_grp.add_argument('--res-type', help="Resource type")
    x_grp.add_argument('--res-label', help="Resource label")

    # --- INSPECT ---
    p_insp = subparsers.add_parser('inspect', help="View metadata")
    p_insp.add_argument('input', help="Input .qsh file")

    # --- UNPACK ---
    p_unp = subparsers.add_parser('unpack', help="Extract all blobs")
    p_unp.add_argument('input', help="Input .qsh file")
    p_unp.add_argument('-d', '--dest', default=".", help="Destination folder")

    args = parser.parse_args()
    print_header()

    # --- EXECUTION ---

    if args.cmd == 'pack':
        f = QSHFile()
        
        # 1. Global Meta
        f.set_global({
            TAG_G_TITLE: args.title,
            TAG_G_AUTHOR: args.author,
            TAG_G_DESC: args.desc,
            TAG_G_TYPE: args.type,
            TAG_G_COMPRESSION: args.compress
        })
        
        # Helper for Hex Args
        def parse_int(x): return int(x, 0) if x else None

        # 2. Firmware Blob
        if args.type == 'firmware' and args.fw_bin:
            with open(args.fw_bin, 'rb') as bin_f:
                data = bin_f.read()
            
            meta = {
                TAG_F_NAME: args.title, # Default to title
                TAG_F_VERSION: args.fw_ver,
                TAG_F_DESC: args.desc,
                TAG_F_AUTHOR: args.author,
                TAG_F_LICENSE: args.fw_license,
                TAG_F_ARCH: args.fw_arch,
                TAG_F_TARGET: args.fw_target,
                TAG_F_DATE: int(time.time()),
                TAG_F_GIT: args.fw_git,
                TAG_F_BOOT_MIN: args.fw_boot_min,
                TAG_F_PAGE_SIZE: args.fw_page_size,
                TAG_F_BASE_ADDR: parse_int(args.fw_base_addr)
            }
            f.add_blob(data, meta)
            
            # Optional Aux Blob (only valid with firmware)
            if args.aux_file:
                with open(args.aux_file, 'rb') as af:
                    adata = af.read()
                ameta = {
                    TAG_X_TYPE: args.aux_type,
                    TAG_X_LABEL: os.path.basename(args.aux_file)
                }
                f.add_blob(adata, ameta)

        # 3. Dump / Config / Channels Blob
        elif args.type in ['dump', 'config', 'channels'] and args.mem_bin:
            with open(args.mem_bin, 'rb') as mf:
                data = mf.read()
                
            meta = {
                TAG_D_LABEL: args.mem_label or "Memory Dump",
                TAG_D_START_ADDR: parse_int(args.mem_start),
                TAG_D_END_ADDR: parse_int(args.mem_end),
                TAG_D_WRITABLE: args.mem_writable,
                TAG_ID_FW_STR: args.radio_fw_id,
                TAG_ID_RADIO_UID: parse_int(args.radio_uid),
                TAG_ID_LABEL: args.radio_label
            }
            
            # Add Channel Specifics
            if args.type == 'channels':
                meta[TAG_D_CH_COUNT] = args.ch_count
                meta[TAG_D_CH_NAMES] = args.ch_names
                
            f.add_blob(data, meta)
            
        # 4. Resource Blob
        elif args.type == 'resource' and args.res_file:
             with open(args.res_file, 'rb') as rf:
                data = rf.read()
             meta = {
                 TAG_R_LABEL: args.res_label or os.path.basename(args.res_file),
                 TAG_R_TYPE: args.res_type or "bin"
             }
             f.add_blob(data, meta)
             
        else:
            log_err(f"Missing required binary input for type '{args.type}'")
            # log_err calls sys.exit(1)
            
        f.save(args.output)

    elif args.cmd == 'inspect':
        c = QSHFile.load(args.input)
        if not c: sys.exit(1)
        
        log_info("Global Metadata")
        for t, v in c.global_meta.items():
            t_name = TAG_MAP.get(t, f"0x{t:02X}")
            if "Date" in t_name and isinstance(v, int):
                v = datetime.fromtimestamp(v).strftime('%Y-%m-%d %H:%M:%S')
            print(f"  {CYAN}{t_name:<20}{RESET}: {BOLD}{v}{RESET}")
            
        log_info(f"Blobs ({len(c.blobs)})")
        for i, b in enumerate(c.blobs):
            print(f"\n[Blob {i+1}] Size: {len(b.data)} bytes")
            for t, v in b.metadata.items():
                t_name = TAG_MAP.get(t, f"0x{t:02X}")
                # Format Conversions
                if "Date" in t_name and isinstance(v, int):
                    v = datetime.fromtimestamp(v).strftime('%Y-%m-%d %H:%M:%S')
                if "Addr" in t_name and isinstance(v, int):
                    v = f"0x{v:08X}"
                if "UID" in t_name and isinstance(v, int):
                    v = f"0x{v:016X}"
                print(f"  {GRAY}{t_name:<20}{RESET}: {v}")

    elif args.cmd == 'unpack':
        c = QSHFile.load(args.input)
        if not c: sys.exit(1)
        
        if not os.path.exists(args.dest):
            os.makedirs(args.dest)
            
        print(f"Extracting {len(c.blobs)} blobs to '{args.dest}/'...")
        
        for i, b in enumerate(c.blobs):
            # Determine Name
            name = f"blob_{i}"
            if TAG_F_NAME in b.metadata: name = "firmware"
            if TAG_X_LABEL in b.metadata: name = b.metadata[TAG_X_LABEL]
            if TAG_R_LABEL in b.metadata: name = b.metadata[TAG_R_LABEL]
            
            # Sanitize
            safe_name = "".join([x for x in str(name) if x.isalnum() or x in "._-"])
            
            # Determine Extension
            ext = ".bin"
            if TAG_X_TYPE in b.metadata: 
                ext = f".{b.metadata[TAG_X_TYPE]}"
            
            # Avoid redundant extensions
            if str(name).lower().endswith(ext.lower()):
                ext = ""
            
            out_path = os.path.join(args.dest, safe_name + ext)
            with open(out_path, 'wb') as f:
                f.write(b.data)
            log_ok(f"Extracted -> {BOLD}{out_path}{RESET}")

if __name__ == '__main__':
    main()