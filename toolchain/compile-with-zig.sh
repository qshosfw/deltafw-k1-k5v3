#!/usr/bin/env bash
set -euo pipefail

# ─────────────────────────────────────────────────────────────────────
# deltafw — compile-with-zig.sh
# Reads config.toml and builds with Zig (Clang/LLD), outputting to build/
#
# Usage:  ./toolchain/compile-with-zig.sh [Preset] [extra zig build args...]
# ─────────────────────────────────────────────────────────────────────

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
GRAY="\e[90m"

BG_RED="\e[41m"
BG_GREEN="\e[42m"
BG_YELLOW="\e[43m"
BG_BLUE="\e[44m"
BG_CYAN="\e[46m"

# Inverted Badges (same as compile-with-meson.sh)
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
    echo "            zig tool suite"
    echo -e "${RESET}"
}

draw_bar() {
    local percent=$1
    local width=${2:-15}

    local val=$percent
    (( val > 100 )) && val=100
    (( val < 0 )) && val=0

    local filled=$(( val * width / 100 ))
    local empty=$(( width - filled ))

    local color="$GREEN"
    (( val > 80 )) && color="$YELLOW"
    (( val > 90 )) && color="$RED"

    local bar=""
    for ((i=0; i<filled; i++)); do bar+="█"; done
    for ((i=0; i<empty; i++)); do bar+="░"; done

    echo -e "${color}${bar}${RESET} ${BOLD}${val}%${RESET}"
}

# ─────────────────────────────────────────────────────────────────────
# Arguments
# ─────────────────────────────────────────────────────────────────────
PRESET=${1:-Custom}
shift || true
EXTRA_ARGS=("$@")

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

mkdir -p "$BUILD_DIR"

# ─────────────────────────────────────────────────────────────────────
# Parse config.toml
# ─────────────────────────────────────────────────────────────────────
CONFIG_OUTPUT=$(python3 "$SCRIPT_DIR/zig_config.py" "$PROJECT_DIR/config.toml" "$PRESET")

ZIG_ARGS=()
ZIG_ARGS+=("--build-file" "toolchain/build.zig")
ZIG_ARGS+=("--prefix" "build")
ZIG_ARGS+=("-Doptimize=ReleaseSmall")

VERSION=""
COMMIT=""

while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    key="${line%%:*}"
    val="${line#*:}"

    case "$key" in
        DEFINE)
            opt_name="${val#ENABLE_}"
            [[ "$opt_name" == "PY32F071x8" || "$opt_name" == "USE_FULL_LL_DRIVER" ]] && continue
            ZIG_ARGS+=("-D${opt_name}=true")
            ;;
        DEFINE_VAL)
            if [[ "$val" == EDITION_STRING=* ]]; then
                edition="${val#EDITION_STRING=}"
                edition="${edition//\"/}"
                ZIG_ARGS+=("-Dedition=${edition}")
            fi
            ;;
        VERSION)    VERSION="$val";   ZIG_ARGS+=("-Dversion=v${val}") ;;
        NAME)       ZIG_ARGS+=("-Dproject_name=${val}") ;;
        AUTHORS)    ZIG_ARGS+=("-Dauthors=${val}") ;;
        GIT_HASH)   COMMIT="$val";    ZIG_ARGS+=("-Dgit_hash=${val}") ;;
        BUILD_DATE) ZIG_ARGS+=("-Dbuild_date=${val}") ;;
    esac
done <<< "$CONFIG_OUTPUT"

ZIG_ARGS+=("${EXTRA_ARGS[@]}")

# ─────────────────────────────────────────────────────────────────────
# Filename convention (matches meson output)
# ─────────────────────────────────────────────────────────────────────
DATE=$(date +%d%m%Y)
LOWER_PRESET=$(echo "$PRESET" | tr '[:upper:]' '[:lower:]')
BASENAME="deltafw.${LOWER_PRESET}.v${VERSION}.${COMMIT}.${DATE}"
FILENAME_BIN="${BASENAME}.bin"
FILENAME_QSH="${BASENAME}.qsh"

# ─────────────────────────────────────────────────────────────────────
# Build
# ─────────────────────────────────────────────────────────────────────
build_preset() {
    local preset="$1"

    log_header "Building ${preset}..."
    start_time=$(date +%s)

    log_build "Configuring ${preset}..."

    # Count total source files for progress display
    # 78 core files + any dynamic sources from the config output
    local core_count=78
    local cond_count
    cond_count=$(echo "$CONFIG_OUTPUT" | grep -c '^SOURCE:' || echo 0)
    local total_sources=$(( core_count + cond_count ))

    # ── Run the high-performance Python build driver ──────────────
    cd "$PROJECT_DIR"
    local build_success=true
    if python3 toolchain/compile.py "$preset"; then
        build_success=true
    else
        build_success=false
    fi

    if [ "$build_success" = false ]; then
        log_error "Compilation failed for ${preset}!"
        exit 1
    fi

    # ── Check output ──────────────────────────────────────────────
    local BIN_SRC="$BUILD_DIR/bin/deltafw.bin"
    local ELF_SRC="$BUILD_DIR/bin/deltafw"

    if [ ! -f "$BIN_SRC" ]; then
        log_error "Artifact not found!"
        exit 1
    fi

    # ── Copy artifacts with standard names ────────────────────────
    log_build "Finalizing artifacts..."
    cp "$BIN_SRC" "$BUILD_DIR/$FILENAME_BIN"
    [ -f "$ELF_SRC" ] && cp "$ELF_SRC" "$BUILD_DIR/deltafw.elf"

    # ── Memory usage report ──────────────────────────────────────
    local FLASH_TOTAL=120832  # 118K
    local RAM_TOTAL=16384     # 16K

    local BIN_SIZE
    BIN_SIZE=$(stat -c%s "$BIN_SRC")

    local RAM_USED=0
    if command -v readelf &>/dev/null && [ -f "$ELF_SRC" ]; then
        # Parse section sizes from ELF ($7 is the Size column in readelf -S)
        local sections
        sections=$(readelf -S "$ELF_SRC" 2>/dev/null || true)

        local data_sz noncache_sz bss_sz heap_sz
        data_sz=$(echo "$sections" | awk '/\.data /{print $7}' | head -1)
        noncache_sz=$(echo "$sections" | awk '/\.noncacheable/{print $7}' | head -1)
        bss_sz=$(echo "$sections" | awk '/\.bss /{print $7}' | head -1)
        heap_sz=$(echo "$sections" | awk '/heap_stack/{print $7}' | head -1)

        # Convert hex to decimal
        RAM_USED=$(printf '%d + %d + %d + %d\n' \
            "0x${data_sz:-0}" "0x${noncache_sz:-0}" "0x${bss_sz:-0}" "0x${heap_sz:-0}" | bc)
    fi

    local FLASH_PCT=$(( BIN_SIZE * 100 / FLASH_TOTAL ))
    local RAM_PCT=$(( RAM_USED * 100 / RAM_TOTAL ))

    echo ""
    echo -e "${BOLD}${WHITE}Memory Usage:${RESET}"
    printf "  ${BOLD}RAM:    ${RESET} %6d B / 16 KB    " "$RAM_USED"
    draw_bar "$RAM_PCT"
    printf "  ${BOLD}FLASH:  ${RESET} %6d B / 118 KB   " "$BIN_SIZE"
    draw_bar "$FLASH_PCT"

    # ── Pack into QSH Container ──────────────────────────────────
    local FULL_GIT_MSG
    FULL_GIT_MSG=$(git log -1 --pretty="%B" 2>/dev/null | tr -d '"' | tr '\n' ' ' || echo "")

    if python3 toolchain/qsh_packer.py pack \
        "$BUILD_DIR/$FILENAME_QSH" \
        --type firmware \
        --fw-bin "$BIN_SRC" \
        --title "deltafw" \
        --author "qshosfw" \
        --desc "Preset: $preset | $FULL_GIT_MSG" \
        --fw-ver "$VERSION" \
        --fw-target "uvk1,uv-k5v3" \
        --fw-git "$COMMIT" \
        --fw-arch "arm" \
        --fw-license "GPL-3.0" \
        --fw-page-size 256 \
        --fw-base-addr 0x08000000 \
        --aux-file "$ELF_SRC" \
        --aux-type "elf" 2>/dev/null; then
        :
    fi

    end_time=$(date +%s)
    duration=$((end_time - start_time))
    log_success "Built ${preset} in ${duration}s"
    echo -e "${BADGE_OK} Firmware: ${BOLD}${CYAN}build/${FILENAME_BIN}${RESET}"
    [ -f "$BUILD_DIR/$FILENAME_QSH" ] && echo -e "${BADGE_OK} Container: ${BOLD}${CYAN}build/${FILENAME_QSH}${RESET}"
}

# ─────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────
print_logo
log_info "Target Preset: ${BOLD}${PRESET}${RESET}"

if [[ "$PRESET" == "All" ]]; then
    PRESETS_STR=$(python3 "$SCRIPT_DIR/config_parser.py" "$PROJECT_DIR/config.toml" list_presets)
    read -r -a PRESETS <<< "$PRESETS_STR"
    for p in "${PRESETS[@]}"; do
        build_preset "$p"
    done
else
    build_preset "$PRESET"
fi
