#ifdef ENABLE_PASSCODE
#include <string.h>
#include "passcode.h"
#include "features/storage/storage.h"
#include "helper/crypto.h"
#include "helper/identifier.h"
#include "ui/textinput.h"
#include "ui/ui.h"
#include "ui/menu.h" // For gUpdateDisplay etc
#include "features/audio/audio.h"
#include "drivers/bsp/st7565.h"
#include "drivers/bsp/keyboard.h"
#include "core/misc.h" // for gStatusLine
#include "ui/helper.h"
#include "drivers/bsp/system.h"
#include "apps/settings/settings.h" // gEeprom (if needed, but we use direct storage)
#include "drivers/hal/Inc/py32f071_ll_iwdg.h" // For Watchdog
#include "ui/status.h"
#include "ui/ag_graphics.h"
#include "core/board.h"

#define PASSCODE_MAGIC 0x51534850 // "QSHP" (Bumped for safe MigratedMask init)
#define PASSCODE_MAX_LEN 32
#define KDF_ITERATIONS 8192
#define DEFAULT_MAX_TRIES 10

static PasscodeConfig_t gPasscodeConfig;
static bool gPasscodeLoaded = false;
static bool gPasscodeLocked = true; // Default to locked until validated

// --- Backend ---

static void LoadConfig(void) {
    if (!gPasscodeLoaded) {
        Storage_ReadRecord(REC_PASSCODE, &gPasscodeConfig, 0, sizeof(PasscodeConfig_t));
        gPasscodeLoaded = true;
        
        // Sanitize
        if (gPasscodeConfig.fields.Length > PASSCODE_MAX_LEN) gPasscodeConfig.fields.Length = PASSCODE_MAX_LEN;
        if (gPasscodeConfig.fields.Iterations == 0xFFFFFFFF) gPasscodeConfig.fields.Iterations = KDF_ITERATIONS;
    }
}

void Passcode_SaveConfig(void) {
    Storage_WriteRecord(REC_PASSCODE, &gPasscodeConfig, 0, sizeof(PasscodeConfig_t));
}

// Global Master Key in RAM
static uint8_t gMasterKey[32] = {0};

// Forward declarations for helpers used in Init
static void GenerateNewMasterKey(void);
static void CryptMasterKey(const uint8_t *kek, uint8_t *in_out);

static void KickWatchdog(void) {
    #ifdef IWDG
    LL_IWDG_ReloadCounter(IWDG);
    #endif
}

static void DrawDialogMessage(const char *message) {
    ST7565_FillScreen(0x00);

    // Center the message
    AG_PrintMediumBoldEx(64, 38, POS_C, C_FILL, message);
    ST7565_BlitFullScreen();
}

void Passcode_Init(void) {
    LoadConfig();
    memset(gMasterKey, 0, 32);
    
    // Validate Magic
    if (gPasscodeConfig.fields.Magic != PASSCODE_MAGIC) {
        // First Boot / Not Set / Structure Update
        memset(&gPasscodeConfig, 0, sizeof(PasscodeConfig_t));
        
        // Generate MK and Nonce
        GenerateNewMasterKey();
        TRNG_Fill(gPasscodeConfig.fields.Nonce, 16);
        gPasscodeConfig.fields.Iterations = KDF_ITERATIONS;
        gPasscodeConfig.fields.Magic = PASSCODE_MAGIC;
        gPasscodeConfig.fields.Length = 0; // No user passcode yet
        
        // Derive Default KEK (Empty Password)
        uint8_t kek[32];
        Passcode_DeriveKEK("", kek);
        
        // Encrypt MK -> Storage
        memcpy(gPasscodeConfig.fields.EncryptedMasterKey, gMasterKey, 32);
        CryptMasterKey(kek, gPasscodeConfig.fields.EncryptedMasterKey);
        
        Passcode_SaveConfig(); 
        gPasscodeLocked = false; 
    } else {
        // If Magic is valid, we are only locked if Length > 0
        gPasscodeLocked = (gPasscodeConfig.fields.Length > 0);
    }

    // If unlocked (no password or already validated), load MK
    if (!gPasscodeLocked) {
        uint8_t kek[32];
        Passcode_DeriveKEK("", kek); // Use Empty password to decrypt MK
        memcpy(gMasterKey, gPasscodeConfig.fields.EncryptedMasterKey, 32);
        CryptMasterKey(kek, gMasterKey);
    }
    
    // Attempt migration (for CPUID and for records if key is available)
    Passcode_MigrateStorage();
    
    // Disable SWD if locked (PA13/14)
    BOARD_SWD_Enable(!gPasscodeLocked);
}

bool Passcode_IsSet(void) {
    LoadConfig();
    return (gPasscodeConfig.fields.Magic == PASSCODE_MAGIC && gPasscodeConfig.fields.Length > 0);
}

bool Passcode_IsLocked(void) {
    return gPasscodeLocked;
}

uint8_t Passcode_GetLength(void) {
    LoadConfig();
    if (gPasscodeConfig.fields.Magic != PASSCODE_MAGIC) return 0;
    return gPasscodeConfig.fields.Length;
}

uint8_t Passcode_GetMaxTries(void) {
    LoadConfig();
    if (gPasscodeConfig.fields.MaxTriesConfig == 0) return DEFAULT_MAX_TRIES;
    return gPasscodeConfig.fields.MaxTriesConfig;
}

void Passcode_SetMaxTries(uint8_t maxTries) {
    LoadConfig();
    if (maxTries < 3) maxTries = 3;
    if (maxTries > 50) maxTries = 50;
    gPasscodeConfig.fields.MaxTriesConfig = maxTries;
    Passcode_SaveConfig();
}

void Passcode_Lock(void) {
    if (Passcode_IsSet()) {
        memset(gMasterKey, 0, 32);
        gPasscodeLocked = true;
        // Re-disable SWD
        BOARD_SWD_Enable(false);
    }
}

// Key Derivation Function:
// Key = Input (Padded to 32)
// Nonce = StoredNonce (16) XOR CPU_ID (12+padding)
// Verifier = Iterative ChaCha20 State Mixing
static void Passcode_Stretcher(chacha20_ctx *ctx, uint32_t iterations, uint8_t *out16) {
    uint8_t state_block[64];
    for (uint32_t i = 0; i < iterations; i++) {
        chacha20_block(ctx->state, state_block);
        uint32_t *p = (uint32_t *)state_block;
        for (int j = 0; j < 8; j++) ctx->state[4 + j] ^= p[j];
        ctx->state[12]++; 
    }
    if (out16) memcpy(out16, state_block, 16);
    memset(state_block, 0, 64);
}

static void ComputeVerifier(const char *input, const uint8_t *nonce, uint8_t *verifier) {
    uint8_t key[32] = {0};
    strncpy((char*)key, input, 32);
    uint8_t dnonce[12], cpu[16];
    GetCpuId(cpu, 12);
    for(int i=0; i<12; i++) dnonce[i] = nonce[i] ^ cpu[i];

    chacha20_ctx ctx;
    uint32_t it = gPasscodeConfig.fields.Iterations > 0 ? gPasscodeConfig.fields.Iterations : KDF_ITERATIONS;
    chacha20_init(&ctx, key, dnonce, 0);
    Passcode_Stretcher(&ctx, it, verifier);
    memset(key, 0, 32);
    memset(&ctx, 0, sizeof(ctx));
}

void Passcode_DeriveKEK(const char *password, uint8_t *kek_out) {
    LoadConfig();
    uint8_t key[32] = {0};
    strncpy((char*)key, password, 32);
    uint8_t salt[16] = {0};
    GetCpuId(salt, 12); 
    salt[12] = 0xAA; salt[13] = 0xBB; salt[14] = 0xCC; salt[15] = 0xDD;
    
    chacha20_ctx ctx;
    uint32_t it = gPasscodeConfig.fields.Iterations > 0 ? gPasscodeConfig.fields.Iterations : KDF_ITERATIONS;
    chacha20_init(&ctx, key, salt, 0);
    uint8_t block[64];
    for (uint32_t i = 0; i < it; i++) {
        chacha20_block(ctx.state, block);
        uint32_t *p = (uint32_t *)block;
        for (int j = 0; j < 8; j++) ctx.state[4 + j] ^= p[j];
        ctx.state[12]++; 
    }
    memcpy(kek_out, block, 32);
    memset(key, 0, 32);
    memset(block, 0, 64);
}

// Ensure gMasterKey is valid (generate if empty/zero)
// But wait, if it's zero, maybe it's just locked?
// This function assumes we WANT to generate a new unique MK (e.g. factory reset or first init).
static void GenerateNewMasterKey(void) {
    TRNG_Fill(gMasterKey, 32);
}

// Encrypt/Decrypt MK using KEK
// We use simple XOR for MK protection since KEK is unique and MK is random.
// Actually, let's use ChaCha20 one-shot.
// IV can be fixed (0) because KEK changes when Passcode changes.
// Wait, if KEK is derived from CPUID (No Passcode), KEK is static.
// MK is static.
// EncryptedMK = Enc(KEK, MK).
// If KEK and MK are static, EncryptedMK is static. This is fine.
static void CryptMasterKey(const uint8_t *kek, uint8_t *in_out) {
    chacha20_ctx ctx;
    uint8_t nonce[12] = {0}; // Fixed nonce for MK wrapping
    // Maybe use CPUID as nonce? unique per device.
    GetCpuId(nonce, 12); 
    
    uint8_t keystream[64];
    chacha20_init(&ctx, kek, nonce, 0);
    chacha20_block(ctx.state, keystream);
    
    for(int i=0; i<32; i++) {
        in_out[i] ^= keystream[i];
    }
    
    memset(&ctx, 0, sizeof(ctx));
    memset(keystream, 0, 64);
}

uint8_t* Passcode_GetMasterKey(void) {
    return gMasterKey;
}

uint32_t Passcode_GetMasterKeyHash(void) {
    uint32_t hash = 0x811C9DC5; // FNV offset basis
    for (int i = 0; i < 32; i++) {
        hash ^= gMasterKey[i];
        hash *= 0x01000193; // FNV prime
    }
    return hash;
}

bool Passcode_IsMigrated(RecordID_t id) {
    return (gPasscodeConfig.fields.MigratedMask & (1ULL << id)) != 0;
}

void Passcode_SetMigrated(RecordID_t id) {
    gPasscodeConfig.fields.MigratedMask |= (1ULL << id);
}

void Passcode_MigrateStorage(void) {
    if (Passcode_IsLocked() && Passcode_IsSet()) return;
    bool changed = false;
    for (int i = 0; i < REC_MAX; i++) {
        if (Passcode_IsMigrated((RecordID_t)i)) continue;
        StorageEnc_t enc = Storage_GetEncryptionType((RecordID_t)i);
        if (enc == ENC_PLAIN) {
            Passcode_SetMigrated((RecordID_t)i); 
            changed = true;
            continue;
        }
        if (enc == ENC_PASSCODE && Passcode_IsLocked() && Passcode_IsSet()) continue;
        Storage_MigrateRecord((RecordID_t)i);
        changed = true;
        KickWatchdog();
    }
    if (changed) Passcode_SaveConfig();
}

bool Passcode_Validate(const char *input) {
    LoadConfig();
    if (gPasscodeConfig.fields.Magic != PASSCODE_MAGIC) {
        // Not set. KEK is derived from empty password?
        // Actually, if Magic is invalid, we shouldn't be validating.
        return true; 
    }
    
    uint8_t computed[16];
    ComputeVerifier(input, gPasscodeConfig.fields.Nonce, computed);
    
    // Constant time comparison
    bool match = true;
    for(int i=0; i<16; i++) {
        if (computed[i] != gPasscodeConfig.fields.Verifier[i]) match = false;
    }
    
    if (match) {
        if (gPasscodeConfig.fields.Tries > 0) {
            gPasscodeConfig.fields.Tries = 0;
            Passcode_SaveConfig();
        }
        gPasscodeLocked = false;
        
        // Derive KEK from input
        uint8_t kek[32];
        Passcode_DeriveKEK(input, kek);
        
        // Decrypt Master Key from storage to RAM
        memcpy(gMasterKey, gPasscodeConfig.fields.EncryptedMasterKey, 32);
        CryptMasterKey(kek, gMasterKey);
        
        // Migrate records now that we have the key
        Passcode_MigrateStorage();
        
        BOARD_SWD_Enable(true);
        return true;
    } else {
        // Clear MK
        memset(gMasterKey, 0, 32);
        
        gPasscodeConfig.fields.Tries++;
        Passcode_SaveConfig();
        
        // Reboot if limit reached
        if (gPasscodeConfig.fields.Tries >= Passcode_GetMaxTries()) {
             NVIC_SystemReset();
        }
        
        return false;
    }
}

void Passcode_Set(const char *input) {
    // 1. Ensure we have a valid Master Key in RAM.
    // If we are setting a passcode, we must be unlocked OR it's first setup.
    // If gMasterKey is all zeros, and we are setting passcode, we must GENERATE a new MK.
    // (This happens on Factory Reset or First Boot).
    // Check if MK is zero
    bool mkEmpty = true;
    for(int i=0; i<32; i++) if (gMasterKey[i] != 0) mkEmpty = false;
    
    if (mkEmpty) {
        GenerateNewMasterKey();
    }
    
    // 2. Generate new random Salt/Nonce
    TRNG_Fill(gPasscodeConfig.fields.Nonce, 16);
    
    // 3. Set Iterations for this new passcode
    gPasscodeConfig.fields.Iterations = KDF_ITERATIONS;

    // 4. Compute Verifier
    ComputeVerifier(input, gPasscodeConfig.fields.Nonce, gPasscodeConfig.fields.Verifier);
    
    // 5. Encrypt Master Key with New KEK
    uint8_t kek[32];
    Passcode_DeriveKEK(input, kek);
    
    // Copy MK to storage buffer and encrypt in-place
    memcpy(gPasscodeConfig.fields.EncryptedMasterKey, gMasterKey, 32);
    CryptMasterKey(kek, gPasscodeConfig.fields.EncryptedMasterKey);
    
    // 6. Set Magic and Length
    gPasscodeConfig.fields.Magic = PASSCODE_MAGIC;
    gPasscodeConfig.fields.Tries = 0;
    gPasscodeConfig.fields.Length = strlen(input);
    if (gPasscodeConfig.fields.Length > PASSCODE_MAX_LEN) gPasscodeConfig.fields.Length = PASSCODE_MAX_LEN;
    
    gPasscodeLocked = false; 
    Passcode_SaveConfig();
    
    // Migrate to encrypted storage
    Passcode_MigrateStorage();
    
    // Clean KEK
    memset(kek, 0, 32);
}

// --- UI ---

// --- UI ---

static char gPasscodeInput[PASSCODE_MAX_LEN + 1];
static bool gPasscodeDone = false;

static void InputCallback(void) {
    gPasscodeDone = true;
}

// Displays input prompt and blocks until valid passcode or locked out
void Passcode_Prompt(void) {
    if (!Passcode_IsSet()) return;
    
    // Wait for key release before starting input
    while (KEYBOARD_Poll() != KEY_INVALID) {
        SYSTEM_DelayMs(1);
        KickWatchdog();
    }
    gKeyReading0 = KEY_INVALID;
    gKeyReading1 = KEY_INVALID;
    gDebounceCounter = 0;

    memset(gPasscodeInput, 0, sizeof(gPasscodeInput));
    // Use stored length for validation input
    TextInput_Init(gPasscodeInput, gPasscodeConfig.fields.Length, false, InputCallback); 
    UI_SetStatusTitle("Enter Passcode");

    while(1) {
        uint8_t maxTries = Passcode_GetMaxTries();
        
        // Enforce delay if too many tries
        if (gPasscodeConfig.fields.Tries >= maxTries) {
             int totalWait = (gPasscodeConfig.fields.Tries - maxTries + 1) * 30;
             
             for (int remaining = totalWait; remaining > 0; remaining--) {
                 ST7565_FillScreen(0x00);
                 UI_SetStatusTitle("LOCKED");
                 UI_DisplayStatus(); 
                 
                 AG_PrintMediumBoldEx(64, 28, POS_C, C_FILL, "SECURITY LOCKOUT");
                 
                 char buf[32] = "      seconds";
                 NUMBER_ToDecimal(buf, remaining, 5, false);
                 AG_PrintSmallEx(64, 42, POS_C, C_FILL, "Please wait");
                 AG_PrintMediumEx(64, 54, POS_C, C_FILL, buf);

                 // Animated "Wait" dots or progress bar could go here
                 ST7565_BlitFullScreen();
                 
                 // Wait 1 second with watchdog kicks
                 for(int j=0; j < 100; j++) {
                     SYSTEM_DelayMs(10);
                     if (j % 20 == 0) KickWatchdog(); 
                 }
             }
             
             // After wait, give the user 1 last attempt before re-locking
             gPasscodeConfig.fields.Tries = maxTries - 1;
             Passcode_SaveConfig();
             UI_SetStatusTitle("Enter Passcode");
             ST7565_FillScreen(0x00);
             ST7565_BlitFullScreen();
        }

        // Render Loop
        while(!gPasscodeDone) {
            while (!gNextTimeslice) {}
            gNextTimeslice = false;
            
            KickWatchdog(); // Ensure watchdog doesn't bite during input

            KEY_Code_t key = KEYBOARD_Poll();
            if (gKeyReading0 == key) {
                if (++gDebounceCounter == key_debounce_10ms) {
                    if (key != KEY_INVALID) {
                        TextInput_HandleInput(key, true, false);
                    }
                }
            } else {
                gDebounceCounter = 0;
                gKeyReading0 = key;
            }
            
            if (key == KEY_INVALID && gKeyReading1 != KEY_INVALID) {
                 TextInput_HandleInput(gKeyReading1, false, false); // Release
                 gKeyReading1 = KEY_INVALID;
            } else if (key != KEY_INVALID) {
                gKeyReading1 = key;
            }
            
            // Render
            TextInput_Tick(); // Update cursor blink timer
            UI_DisplayStatus(); // Updates status bar in RAM and blits line 0
            TextInput_Render(); // Updates input area in RAM and blits all lines (0-7)
            
            // Check for Auto-Submit
            if (strlen(gPasscodeInput) >= gPasscodeConfig.fields.Length) {
                gPasscodeDone = true;
            }
        }
        
        // Validation
        DrawDialogMessage("Verifying...");

        if (Passcode_Validate(gPasscodeInput)) {
            AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
            TextInput_Deinit();
            UI_SetStatusTitle(NULL);
            return; // Unlocked
        } else {
            AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
            DrawDialogMessage("Invalid");
            SYSTEM_DelayMs(1000);
            
            // Reset input
            memset(gPasscodeInput, 0, sizeof(gPasscodeInput));
            gPasscodeDone = false;
            // Use stored length again
            TextInput_Init(gPasscodeInput, gPasscodeConfig.fields.Length, false, InputCallback);
        }
    }
}

// Called from Settings Menu
void Passcode_Change(void) {
    // Wait for key release (e.g. MENU key used to enter this action)
    while (KEYBOARD_Poll() != KEY_INVALID) {
        SYSTEM_DelayMs(1);
        KickWatchdog();
    }
    gKeyReading0 = KEY_INVALID;
    gKeyReading1 = KEY_INVALID;
    gDebounceCounter = 0;

    // 1. If set, ask old
    if (Passcode_IsSet()) {
        memset(gPasscodeInput, 0, sizeof(gPasscodeInput));
        gPasscodeDone = false;
        // Use stored length for old passcode
        TextInput_Init(gPasscodeInput, gPasscodeConfig.fields.Length, false, InputCallback); 
        UI_SetStatusTitle("Verify Passcode");
        
        while(!gPasscodeDone) {
             while (!gNextTimeslice) {}
             gNextTimeslice = false;
             
             KickWatchdog();
             
             KEY_Code_t key = KEYBOARD_Poll();
             if (gKeyReading0 == key) {
                 if (++gDebounceCounter == key_debounce_10ms) {
                     if (key != KEY_INVALID) TextInput_HandleInput(key, true, false);
                 }
             } else {
                 gDebounceCounter = 0;
                 gKeyReading0 = key;
             }
             if (key == KEY_INVALID && gKeyReading1 != KEY_INVALID) {
                  TextInput_HandleInput(gKeyReading1, false, false);
                  gKeyReading1 = KEY_INVALID;
             } else if (key != KEY_INVALID) gKeyReading1 = key;

             TextInput_Tick();
             UI_DisplayStatus();
             TextInput_Render();
        }
        
        if (!Passcode_Validate(gPasscodeInput)) {
             DrawDialogMessage("Wrong");
             SYSTEM_DelayMs(1000);
             TextInput_Deinit();
             UI_SetStatusTitle(NULL);
             return;
        }
        TextInput_Deinit();
    }
    
    // 2. Ask New
    memset(gPasscodeInput, 0, sizeof(gPasscodeInput));
    gPasscodeDone = false;
    // Use MAX length for new passcode
    TextInput_Init(gPasscodeInput, PASSCODE_MAX_LEN, false, InputCallback);
    UI_SetStatusTitle("Set Passcode");
    
    while(!gPasscodeDone) {
         while (!gNextTimeslice) {}
         gNextTimeslice = false;
         
         KickWatchdog();
         
         KEY_Code_t key = KEYBOARD_Poll();
         if (gKeyReading0 == key) {
             if (++gDebounceCounter == key_debounce_10ms) {
                 if (key != KEY_INVALID) TextInput_HandleInput(key, true, false);
             }
         } else {
             gDebounceCounter = 0;
             gKeyReading0 = key;
         }
         if (key == KEY_INVALID && gKeyReading1 != KEY_INVALID) {
              TextInput_HandleInput(gKeyReading1, false, false);
              gKeyReading1 = KEY_INVALID;
         } else if (key != KEY_INVALID) gKeyReading1 = key;

         TextInput_Tick();
         UI_DisplayStatus();
         TextInput_Render();
    }
    
    char newPass[PASSCODE_MAX_LEN + 1];
    strcpy(newPass, gPasscodeInput);
    uint8_t newLen = strlen(newPass);
    TextInput_Deinit();
    
    // 3. Confirm New (Skip if empty/off)
    if (newLen > 0) {
        memset(gPasscodeInput, 0, sizeof(gPasscodeInput));
        gPasscodeDone = false;
        // Use ACTUAL length of new passcode for confirmation
        TextInput_Init(gPasscodeInput, newLen, false, InputCallback);
        UI_SetStatusTitle("Confirm Passcode");
        
        while(!gPasscodeDone) {
             while (!gNextTimeslice) {}
             gNextTimeslice = false;
             
             KickWatchdog();
             
             KEY_Code_t key = KEYBOARD_Poll();
             if (gKeyReading0 == key) {
                 if (++gDebounceCounter == key_debounce_10ms) {
                     if (key != KEY_INVALID) TextInput_HandleInput(key, true, false);
                 }
             } else {
                 gDebounceCounter = 0;
                 gKeyReading0 = key;
             }
             if (key == KEY_INVALID && gKeyReading1 != KEY_INVALID) {
                  TextInput_HandleInput(gKeyReading1, false, false);
                  gKeyReading1 = KEY_INVALID;
             } else if (key != KEY_INVALID) gKeyReading1 = key;

             TextInput_Tick();
             UI_DisplayStatus();
             TextInput_Render();
        }
    } else {
        // For empty passcode, confirmation is identity
        strcpy(gPasscodeInput, "");
    }
    
    if (strcmp(newPass, gPasscodeInput) == 0) {
        DrawDialogMessage("Saving...");
        SYSTEM_DelayMs(500); // Give user time to see it
        Passcode_Set(newPass);
        DrawDialogMessage("Saved");
        SYSTEM_DelayMs(500); // Give user time to see it
    } else {
        DrawDialogMessage("Mismatch");
    }
    SYSTEM_DelayMs(1000);
    TextInput_Deinit();
    UI_SetStatusTitle(NULL);
}
#endif
