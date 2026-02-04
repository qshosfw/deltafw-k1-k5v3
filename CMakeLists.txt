cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME deltafw)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

# Git commit hash (always refresh, never cache)
if(NOT GIT_COMMIT_HASH)
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if("${GIT_COMMIT_HASH}" STREQUAL "")
        set(GIT_COMMIT_HASH "unknown")
    endif()
endif()

if(NOT BUILD_DATE)
    string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")
endif()

message("Git commit: " ${GIT_COMMIT_HASH})
message("Build date: " ${BUILD_DATE})

# deltafw Branding
if(ENABLE_CUSTOM_FIRMWARE_MODS)
    set(AUTHOR_STRING_1 "deltafw" CACHE STRING "Author 1")
    set(AUTHOR_STRING_2 "qshosfw" CACHE STRING "Author 2")
    set(VERSION_STRING_1 "v1.0" CACHE STRING "Version 1")
    set(VERSION_STRING_2 "v1.0.0" CACHE STRING "Version 2")
    set(EDITION_STRING "Custom" CACHE STRING "Edition")
    
    set(AUTHOR_STRING "${AUTHOR_STRING_1}")
    set(VERSION_STRING ${VERSION_STRING_2})
else()
    set(AUTHOR_STRING "deltafw" CACHE STRING "Author")
    set(VERSION_STRING "NOGIT" CACHE STRING "Version")
endif()

if(TARGET)
    set(EXE_NAME ${TARGET})
else()
    set(EXE_NAME "deltafw")
endif()

message("EXE_NAME = " ${EXE_NAME})

add_executable(${EXE_NAME})

add_subdirectory(src)

target_link_libraries(${EXE_NAME} PRIVATE Core App)

target_compile_definitions(${EXE_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)
target_link_libraries(${EXE_NAME} PRIVATE ${TOOLCHAIN_LINK_LIBRARIES})

# Generate map file
target_link_options(${EXE_NAME} PRIVATE "-Wl,-Map=${EXE_NAME}.map")

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${EXE_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${EXE_NAME}.map)

# Force linker to ignore RWX segment warning even if cache exists
target_link_options(${EXE_NAME} PRIVATE -Wl,--no-warn-rwx-segment)

# -----------------------------------
#  Post build processing
#

if(NOT BUILD_DATE_FILE)
    string(TIMESTAMP BUILD_DATE_FILE "%d%m%Y")
endif()

set(OUTPUT_FILENAME "${EXE_NAME}.${VERSION_STRING}.${GIT_COMMIT_HASH}.${BUILD_DATE_FILE}")
message("Output file: ${OUTPUT_FILENAME}")
message("CMAKE_OBJCOPY: ${CMAKE_OBJCOPY}")

# Generate .bin file
add_custom_command(
    TARGET ${EXE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${EXE_NAME}.elf ${OUTPUT_FILENAME}.bin
    COMMENT "Generating .bin file: ${OUTPUT_FILENAME}.bin"
)

# Generate .hex file
add_custom_command(
    TARGET ${EXE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${EXE_NAME}.elf ${OUTPUT_FILENAME}.hex
    COMMENT "Generating .hex file: ${OUTPUT_FILENAME}.hex"
)
