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
BADGE_SUCCESS="${BOLD}${WHITE}${BG_GREEN} SUSS ${RESET}" # "SUCCESS" too long? "DONE"? Let's use " OK "
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
    echo "           make tool suite"
    echo -e "${RESET}"
}

# ---------------------------------------------
# Usage:
#   ./compile-with-docker.sh [Preset] [CMake options...]
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
PRESET=${1:-Custom}
shift || true  # remove preset from arguments if present

# Any remaining args will be treated as CMake cache variables
EXTRA_ARGS=("$@")

# Fetch git source version and build date from host
GIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_DATE=$(date "+%Y-%m-%d %H:%M:%S")
BUILD_DATE_FILE=$(date "+%d%m%Y")

EXTRA_ARGS+=("-DGIT_COMMIT_HASH=$GIT_HASH" "-DBUILD_DATE=\"$BUILD_DATE\"" "-DBUILD_DATE_FILE=$BUILD_DATE_FILE")

# ---------------------------------------------
# Validate preset name
# ---------------------------------------------
if [[ ! "$PRESET" =~ ^(Custom|Bandscope|Broadcast|Basic|RescueOps|Game|Fusion|All)$ ]]; then
  log_error "Unknown preset: '$PRESET'"
  log_info "Valid presets are: Custom, Bandscope, Broadcast, Basic, RescueOps, Game, Fusion, All"
  exit 1
fi

print_logo
log_info "Target Preset: ${BOLD}${PRESET}${RESET}"
log_info "Git Hash: ${GIT_HASH}"
log_info "Build Date: ${BUILD_DATE}"

# ---------------------------------------------
# Build the Docker image (only needed once)
# ---------------------------------------------
# log_build "Checking Docker image..."
# docker build -t "$IMAGE" . > /dev/null 2>&1

# ---------------------------------------------
# Clean existing CMake cache to ensure toolchain reload
# ---------------------------------------------
log_build "Cleaning build environment..."
docker run --rm -v "$PWD":/src -w /src "$IMAGE" rm -rf build

# ---------------------------------------------
# Function to build one preset
# ---------------------------------------------
build_preset() {
  local preset="$1"
  log_header "Building ${preset}..."
  
  start_time=$(date +%s)

  # Configure
  log_build "Configuring ${preset}..."
  # Use -i only (no -t) to force line-oriented output for formatter
  if ! docker run --rm -i -v "$PWD":/src -w /src "$IMAGE" \
    bash -c "cmake -S toolchain --preset ${preset} ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}" 2>&1 | python3 -u toolchain/build_formatter.py; then
      # If pipe fails, we need to check exit status. 
      # Since set -o pipefail is on, the loop fails if docker fails.
      # But python script usually succeeds.
      # Wait, if docker fails, pipefail ensures the whole pipeline returns non-zero?
      # Yes.
      log_error "Configuration failed for ${preset}!"
      exit 1
  fi

  # Build
  log_build "Compiling ${preset}..."
  if ! docker run --rm -i -v "$PWD":/src -w /src "$IMAGE" \
    bash -c "cd build/${preset} && ninja" 2>&1 | python3 -u toolchain/build_formatter.py; then
      log_error "Compilation failed for ${preset}!"
      exit 1
  fi
  
  end_time=$(date +%s)
  duration=$((end_time - start_time))
  
  # Copy artifacts to root
  if compgen -G "build/${preset}/deltafw.*.bin" > /dev/null; then
      # Create generic alias for flashing tools
      # Find the main binary (shortest name or just take one)
      MAIN_BIN=$(find build/${preset} -name "deltafw.*.bin" ! -name "*.packed.bin" | head -n 1)
      if [ -f "$MAIN_BIN" ]; then
          # Just print the relative path. Terminals like VSCode/Gnome auto-link this.
          REL_PATH="build/${preset}/$(basename "$MAIN_BIN")"
          
          echo -e "${BADGE_OK} Firmware: ${BOLD}${CYAN}${REL_PATH}${RESET}"
      fi
      if compgen -G "build/${preset}/deltafw.*.packed.bin" > /dev/null; then
          true # No longer copying packed bin to root
      fi
  else
      log_warn "No .bin artifact found to copy!"
  fi
  
  log_success "Built ${preset} in ${duration}s"
}

# ---------------------------------------------
# Handle 'All' preset
# ---------------------------------------------
if [[ "$PRESET" == "All" ]]; then
  PRESETS=(Bandscope Broadcast Basic RescueOps Game Fusion)
  for p in "${PRESETS[@]}"; do
    build_preset "$p"
  done
  log_header "Build Summary"
  log_success "All presets built successfully!"
else
  build_preset "$PRESET"
fi
