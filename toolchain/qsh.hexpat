#include <std/mem.pat>
#include <std/sys.pat>
#include <std/time.pat>

// ============================================================================
// QSH FILE FORMAT PATTERN (RFC REV 1)
// ============================================================================

// --- Constants & Enums ---

enum TagType : u16 {
    // Global Tags
    G_Title         = 0x01,
    G_Author        = 0x02,
    G_Desc          = 0x03,
    G_Date          = 0x04,
    G_Type          = 0x05,
    G_Compression   = 0x06,
    
    // Firmware Tags
    F_Name          = 0x11,
    F_Version       = 0x12,
    F_Desc          = 0x13,
    F_Author        = 0x14,
    F_License       = 0x15,
    F_Arch          = 0x16,
    F_Target        = 0x17,
    F_Date          = 0x18,
    F_Git           = 0x19,
    F_BootMin       = 0x1A,
    F_PageSize      = 0x1B,
    F_BaseAddr      = 0x1C,

    // Resource Tags
    R_Label         = 0x30,
    R_Type          = 0x31,

    // Dump/Config Tags
    D_Label         = 0x40,
    D_StartAddr     = 0x41,
    D_EndAddr       = 0x42,
    D_Writable      = 0x43,
    D_ChCount       = 0x44,
    D_ChNames       = 0x45,

    // Radio Identity
    ID_FwStr        = 0x60,
    ID_RadioUid     = 0x61,
    ID_Label        = 0x62,

    // Auxiliary
    X_Type          = 0x70,
    X_Label         = 0x71,
    X_Compiler      = 0x72,
    
    Terminator      = 0x00
};

// --- Structures ---

struct TLV {
    TagType tag [[color("0000FF")]];
    u16 length;
    
    if (length > 0) {
        // Interpret Value based on Tag
        match (tag) {
            // Timestamps (UINT32)
            (TagType::G_Date):   u32 value [[format("std::time::to_string")]];
            (TagType::F_Date):   u32 value [[format("std::time::to_string")]];
            
            // Addresses / Sizes (UINT32 Hex)
            (TagType::F_BaseAddr):   u32 value [[format("hex")]];
            (TagType::F_PageSize):   u32 value [[format("hex")]];
            (TagType::D_StartAddr):  u32 value [[format("hex")]];
            (TagType::D_EndAddr):    u32 value [[format("hex")]];
            
            // Integers
            (TagType::D_ChCount):    u16 value;
            (TagType::ID_RadioUid):  u64 value [[format("hex")]];
            
            // Boolean
            (TagType::D_Writable):   bool value;
            
            // Default: ASCII String
            (_): char value[length];
        }
    }
} [[inline]];

struct Header {
    char magic[8] [[color("FF0000")]];
    u16 spec_rev  [[color("FFFF00")]];
};

struct Blob {
    u32 total_size [[color("00FF00")]];
    
    // Read TLVs until we hit a Terminator (Tag 0x00)
    // We use a peek check to look for tag 0x0000 without consuming it immediately in the array loop
    TLV metadata[while(std::mem::read_unsigned($, 2) != 0)];
    TLV terminator [[hidden]];
    
    // Calculate remaining size for payload
    // ImHex allows calculating relative offsets
    u128 header_size = $ - addressof(total_size);
    u32 payload_size = total_size - (header_size - 4); // -4 because total_size includes itself usually? 
    // Wait, typical pattern: Size includes everything following it? 
    // Spec says: "prefix each blob with its size". Usually size = len(TLV + Data).
    
    // Adjust logic: total_size is the size of (TLVs + Payload).
    // The current offset ($) is at the start of Payload.
    // The start of this struct was `addressof(total_size)`.
    // The TLVs started at `addressof(total_size) + 4`.
    // So current offset - (start + 4) is the TLV size.
    
    u8 payload[total_size - ($ - (addressof(total_size) + 4))] [[color("808080")]];
};

struct QSH_File {
    Header header;
    
    // Global Metadata Block
    TLV global_metadata[while(std::mem::read_unsigned($, 2) != 0)];
    TLV global_terminator [[name("Global Terminator")]];
    
    // Blobs
    // We continue reading blobs as long as we have more than 32 bytes left (Hash size)
    Blob blobs[while(std::mem::size() - $ > 32)];
    
    // Footer
    u8 sha256_hash[32] [[color("FF00FF")]];
};

// --- Main Execution ---

QSH_File file @ 0x00;