#!/usr/bin/env python3
import sys
import struct
import hashlib
import binascii
import os
import zlib
import time
import argparse

# QSHFW RFC v1.1.0 Constants
MAGIC = b"qshfw\x00\x00\x00"
SPEC_VERSION = 1
HEADER_SIZE = 64

# Tags Registry
TAG_TYPE        = 0x0001
TAG_ALGO        = 0x0002
TAG_VERSION     = 0x0003
TAG_TARGET      = 0x0004
TAG_AUTHOR      = 0x0005
TAG_ARCH        = 0x0006
TAG_LICENSE     = 0x0007
TAG_DATE        = 0x0008
TAG_DESC        = 0x0009
TAG_ELF         = 0x000A
TAG_MAP         = 0x000B
TAG_NAME        = 0x000C
TAG_GIT         = 0x0010
TAG_SIG         = 0x00FF

def zlib_crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF

def sha256_hash(data):
    return hashlib.sha256(data).digest()

class QSHFWPacker:
    def __init__(self, payload_path, output_path, addr=0x00000000):
        self.payload_path = payload_path
        self.output_path = output_path
        self.addr = addr
        self.tlvs = []
        self.payload = b""
        
        with open(payload_path, 'rb') as f:
            self.payload = f.read()

    def add_tlv(self, tag, value):
        if isinstance(value, str):
            value = value.encode('utf-8')
        elif isinstance(value, int):
            # Assumed 8 bytes for date, else 4 bytes? 
            # RFC says uint64 for date (0x0008)
            if tag == TAG_DATE:
                value = struct.pack('<Q', value)
            else:
                value = struct.pack('<I', value)
        
        self.tlvs.append((tag, value))

    def add_compressed_tlv(self, tag, file_path):
        if not os.path.exists(file_path):
            return
        with open(file_path, 'rb') as f:
            data = f.read()
        compressed = zlib.compress(data)
        self.tlvs.append((tag, compressed))

    def pack(self):
        # 1. Build Metadata Area
        meta_area = bytearray()
        for tag, val in self.tlvs:
            meta_area.extend(struct.pack('<HI', tag, len(val)))
            meta_area.extend(val)
        
        # 2. Signing (Optional)
        signing_key_hex = os.environ.get("QSHFW_SIGNING_KEY")
        if signing_key_hex:
            try:
                from cryptography.hazmat.primitives.asymmetric import ed25519
                # This is a bit complex to do without a full lib but let's assume we can use it if available
                # If not, we skip or error.
                key_bytes = binascii.unhexlify(signing_key_hex)
                priv_key = ed25519.Ed25519PrivateKey.from_private_bytes(key_bytes)
                
                # RFC says signature is of (Header + Payload + TLVs_preceding)
                # But Header has CRC which depends on itself. 
                # Actually, the signature tag should probably be LAST and sign everything before it.
                # Let's prepare header first (partial)
                partial_header = self._build_header_partial(len(self.payload), len(meta_area))
                to_sign = partial_header + self.payload + meta_area
                sig = priv_key.sign(to_sign)
                
                # Append Sig TLV
                meta_area.extend(struct.pack('<HI', TAG_SIG, len(sig)))
                meta_area.extend(sig)
            except ImportError:
                print("Warning: 'cryptography' library not found. Signing skipped.")
            except Exception as e:
                print(f"Error during signing: {e}")

        # 3. Build Final Header
        final_meta_size = len(meta_area)
        header = self._build_header_full(len(self.payload), final_meta_size)

        # 4. Write File
        with open(self.output_path, 'wb') as f:
            f.write(header)
            f.write(self.payload)
            f.write(meta_area)
            
        print(f"Successfully created: {self.output_path}")
        print(f"  Payload Size: {len(self.payload)} bytes")
        print(f"  Metadata Size: {final_meta_size} bytes")

    def _build_header_partial(self, p_size, m_size):
        # 0x00-0x1B
        header = bytearray(MAGIC) # 8
        header.extend(struct.pack('<I', SPEC_VERSION)) # 12
        header.extend(struct.pack('<I', self.addr)) # 16
        header.extend(struct.pack('<I', p_size)) # 20
        header.extend(struct.pack('<I', m_size)) # 24
        header.extend(struct.pack('<I', 0)) # 28 (flags)
        header.extend(sha256_hash(self.payload)) # 60 (32 bytes hash)
        return header

    def _build_header_full(self, p_size, m_size):
        header = self._build_header_partial(p_size, m_size)
        crc = zlib_crc32(header)
        header.extend(struct.pack('<I', crc)) # 64
        return header

def main():
    parser = argparse.ArgumentParser(description="QSHFW Container Packer")
    parser.add_argument("payload", help="Binary payload to pack")
    parser.add_argument("output", help="Output .qshfw file")
    parser.add_argument("--addr", type=lambda x: int(x, 0), default=0x0, help="Target address")
    parser.add_argument("--version", help="Version string")
    parser.add_argument("--name", help="Firmware name")
    parser.add_argument("--target", help="Device target")
    parser.add_argument("--author", help="Author name")
    parser.add_argument("--arch", help="Architecture")
    parser.add_argument("--license", help="License")
    parser.add_argument("--desc", help="Description")
    parser.add_argument("--git", help="Git commit hash")
    parser.add_argument("--elf", help="Path to ELF file to embed")
    parser.add_argument("--map", help="Path to Map file to embed")
    
    args = parser.parse_args()

    packer = QSHFWPacker(args.payload, args.output, args.addr)
    
    # Standard Tags
    packer.add_tlv(TAG_TYPE, "firmware")
    packer.add_tlv(TAG_ALGO, "none")
    if args.version: packer.add_tlv(TAG_VERSION, args.version)
    if args.name:    packer.add_tlv(TAG_NAME, args.name)
    if args.target:  packer.add_tlv(TAG_TARGET, args.target)
    if args.author:  packer.add_tlv(TAG_AUTHOR, args.author)
    if args.arch:   packer.add_tlv(TAG_ARCH, args.arch)
    if args.license: packer.add_tlv(TAG_LICENSE, args.license)
    packer.add_tlv(TAG_DATE, int(time.time()))
    if args.desc: packer.add_tlv(TAG_DESC, args.desc)
    if args.git:  packer.add_tlv(TAG_GIT, args.git)
    
    # Optional Assets
    if args.elf: packer.add_compressed_tlv(TAG_ELF, args.elf)
    if args.map: packer.add_compressed_tlv(TAG_MAP, args.map)
    
    packer.pack()

if __name__ == "__main__":
    main()
