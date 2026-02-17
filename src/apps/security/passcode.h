#ifndef APPS_SECURITY_PASSCODE_H
#define APPS_SECURITY_PASSCODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_PASSCODE
void Passcode_Init(void);
bool Passcode_IsSet(void);
bool Passcode_Validate(const char *input);
void Passcode_Set(const char *input);
uint8_t Passcode_GetLength(void);
uint8_t Passcode_GetMaxTries(void);
void Passcode_SetMaxTries(uint8_t maxTries);
bool Passcode_IsLocked(void);
void Passcode_Lock(void);

#include "features/storage.h"

// For Storage
void Passcode_DeriveKEK(const char *password, uint8_t *kek_out);
uint8_t* Passcode_GetMasterKey(void);
uint32_t Passcode_GetMasterKeyHash(void);
bool Passcode_IsMigrated(RecordID_t id);
void Passcode_SetMigrated(RecordID_t id);
void Passcode_SaveConfig(void);
void Passcode_MigrateStorage(void);

// UI Functions
void Passcode_Prompt(void);  // Boot lock screen
void Passcode_Change(void);  // Settings menu entry
#else
// Stubs or transparent bypasses when disabled
#define Passcode_Init()
#define Passcode_IsSet() (false)
#define Passcode_Validate(input) (false)
#define Passcode_Set(input)
#define Passcode_GetLength() (0)
#define Passcode_GetMaxTries() (10)
#define Passcode_SetMaxTries(val)
#define Passcode_IsLocked() (false)
#define Passcode_Lock()
#define Passcode_GetMasterKey() ((uint8_t*)NULL)
#define Passcode_GetMasterKeyHash() (0)
#define Passcode_IsMigrated(id) (true)
#define Passcode_SetMigrated(id)
#define Passcode_MigrateStorage()
#define Passcode_Prompt()
#define Passcode_Change()
#endif

#endif // APPS_SECURITY_PASSCODE_H
