
#ifdef VERSION_STRING
    #define VER     " "VERSION_STRING
#else
    #define VER     ""
#endif

#ifndef GIT_COMMIT_HASH
    #define GIT_COMMIT_HASH "unknown"
#endif

#ifndef BUILD_DATE
    #define BUILD_DATE "unknown"
#endif

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    const char Version[]      = AUTHOR_STRING " " VERSION_STRING_2;
    const char Edition[]      = EDITION_STRING;
#else
    const char Version[]      = AUTHOR_STRING VER;
#endif

const char UART_Version[] = "deltafw by qshosfw " VERSION_STRING "\r\n";

// Build info for System Info menu
const char GitCommit[] = GIT_COMMIT_HASH;
const char BuildDate[] = BUILD_DATE;
