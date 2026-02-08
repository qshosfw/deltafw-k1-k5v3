#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------
# Colors & Formatting
# ---------------------------------------------
BOLD="\e[1m"
RESET="\e[0m"
RED="\e[31m"
GREEN="\e[32m"
YELLOW="\e[33m"
BLUE="\e[34m"
CYAN="\e[36m"
WHITE="\e[37m"

BG_RED="\e[41m"
BG_GREEN="\e[42m"
BG_YELLOW="\e[43m"
BG_BLUE="\e[44m"
BG_CYAN="\e[46m"

# Inverted Badges
BADGE_INFO="${BOLD}${WHITE}${BG_BLUE} INFO ${RESET}"
BADGE_OK="${BOLD}${WHITE}${BG_GREEN}  OK  ${RESET}"
BADGE_WARN="${BOLD}${WHITE}${BG_YELLOW} WARN ${RESET}"
BADGE_ERR="${BOLD}${WHITE}${BG_RED} ERR  ${RESET}"
BADGE_BUILD="${BOLD}${WHITE}${BG_CYAN} BLD  ${RESET}"

# ---------------------------------------------
# Logging Functions
# ---------------------------------------------
log_header() {
    echo -e "\n  ⚡ ${BOLD}${CYAN}$1${RESET}"
    echo -e "  ══════════════════════════════════════════════════════════"
}

log_info() {
    echo -e "${BADGE_INFO} $1"
}

log_success() {
    echo -e "${BADGE_OK} $1"
}

log_warn() {
    echo -e "${BADGE_WARN} $1"
}

log_error() {
    echo -e "${BADGE_ERR} $1"
}

log_build() {
    echo -e "${BADGE_BUILD} $1"
}

print_logo() {
    echo -e "${CYAN}${BOLD}"
    echo "      _      _ _        __"
    echo "   __| | ___| | |_ __ _/ _|_      __"
    echo "  / _\` |/ _ \\ | __/ _\` | |_\\ \\ /\\ / /"
    echo " | (_| |  __/ | || (_| |  _|\\ V  V /"
    echo "  \\__,_|\\___|_|\\__\\__,_|_|   \\_/\\_/"
    echo "           meson tool suite"
    echo -e "${RESET}"
}

# ---------------------------------------------
# Usage:
#   ./compile-with-meson.sh [Preset] [Meson options...]
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
PRESET=${1:-Custom}
shift || true

# Any remaining args will be treated as Meson options
EXTRA_ARGS=("$@")

# ---------------------------------------------
# Build the Docker image (ensure we have Meson)
# ---------------------------------------------
# ---------------------------------------------
# Build the Docker image (ensure we have Meson)
# ---------------------------------------------
if ! docker image inspect "$IMAGE" &> /dev/null; then
    log_build "Building Docker image..."
    docker build -t "$IMAGE" -f toolchain/Dockerfile . > /dev/null
fi

# ---------------------------------------------
# Configuration Helper
# ---------------------------------------------
get_config() {
    # Run config parser inside docker to ensure tomli is available
    docker run --rm -v "$PWD":/src -w /src "$IMAGE" python3 toolchain/config_parser.py config.toml "$@"
}

# ---------------------------------------------
# Main Build Function
# ---------------------------------------------
build_preset() {
    local preset="$1"
    
    # Check if preset exists in config
    presets_list=$(get_config list_presets)
    if [[ ! " $presets_list " =~ " $preset " ]]; then
        log_error "Unknown preset: '$preset'"
        exit 1
    fi
    
    # Fetch Meson Args
    MESON_ARGS_STR=$(get_config get_preset_args "$preset")
    read -r -a MESON_ARGS <<< "$MESON_ARGS_STR"

    # Fetch Project Info
    eval $(get_config get_project_info)
    
    log_header "Building ${preset}..."
    start_time=$(date +%s)
    
    # Single build directory as requested
    BUILD_DIR="build"

    # Logic: Always wipe and start fresh as requested
    if [ -d "$BUILD_DIR" ]; then
        # Use docker (root) to remove potentially root-owned files and fix parent dir permissions
        # We wipe the CONTENTS of build to keep the dir itself if possible, or just nuke it.
        docker run --rm -v "$PWD":/src -w /src "$IMAGE" sh -c "rm -rf '$BUILD_DIR' && mkdir -p build && chmod -R 777 build && chown -R $(id -u):$(id -g) build"
    fi

    log_build "Configuring ${preset}..."
    # Source dir is toolchain/ relative to root
    # We pipe stderr to stdout (2>&1) so the formatter catches everything
    docker run --rm -u $(id -u):$(id -g) -v "$PWD":/src -w /src "$IMAGE" \
        meson setup "$BUILD_DIR" toolchain \
        --cross-file toolchain/cross_arm.ini \
        "${MESON_ARGS[@]}" \
        "${EXTRA_ARGS[@]}" 2>&1 | python3 toolchain/build_formatter.py

    # Compile
    log_build "Compiling ${preset}..."
    
    # Calculate version info for filename
    # VERSION comes from config.toml (get_project_info)
    if command -v git &> /dev/null; then
        COMMIT=$(git rev-parse --short HEAD)
    else
        COMMIT="unknown"
    fi
    DATE=$(date +%d%m%Y)
    LOWER_PRESET=$(echo "$preset" | tr '[:upper:]' '[:lower:]')
    FILENAME="deltafw.${LOWER_PRESET}.v${VERSION}.${COMMIT}.${DATE}.bin"

    # Run build with formatter
    # We pipe stderr to stdout (2>&1) so the formatter catches everything
    if docker run --rm -u $(id -u):$(id -g) -v "$PWD":/src -w /src "$IMAGE" \
        meson compile -C "$BUILD_DIR" 2>&1 | python3 toolchain/build_formatter.py; then
        
        # Copy artifacts to build folder
        log_build "Finalizing artifacts..."
        if [ -f "$BUILD_DIR/deltafw.bin" ]; then
            # Move/Rename directly in build dir
            cp "$BUILD_DIR/deltafw.bin" "$BUILD_DIR/$FILENAME"
            
            # Also pack it if needed (optional, keeping simple for now)
            
            end_time=$(date +%s)
            duration=$((end_time - start_time))
            log_success "Built ${preset} in ${duration}s"
            echo -e "${BADGE_OK} Firmware: ${BOLD}${CYAN}${BUILD_DIR}/${FILENAME}${RESET}"
        else
            log_error "Artifact not found!"
            exit 1
        fi
    else
        log_error "Compilation failed for ${preset}!"
        exit 1
    fi
}

print_logo
log_info "Target Preset: ${BOLD}${PRESET}${RESET}"

if [[ "$PRESET" == "All" ]]; then
    # Dynamic list from config
    PRESETS_STR=$(get_config list_presets)
    read -r -a PRESETS <<< "$PRESETS_STR"
    for p in "${PRESETS[@]}"; do
        build_preset "$p"
    done
else
    build_preset "$PRESET"
fi
