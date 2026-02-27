# deltafw Makefile
# User-friendly build targets

# Colors
BOLD   := \033[1m
CYAN   := \033[36m
GREEN  := \033[32m
YELLOW := \033[33m
BLUE   := \033[34m
WHITE  := \033[37m
GRAY   := \033[90m
RESET  := \033[0m

.PHONY: all default dev custom menuconfig config help clean \
        pro ham survive explorer barebones release debug

# Default target - show help
all: help

# ============================================================================
# Quick Build Targets
# ============================================================================

# Recommended for daily use
default:
	@./toolchain/compile-with-meson.sh default

# Developer build (all features)
dev:
	@./toolchain/compile-with-meson.sh dev

# ============================================================================
# Preset Builds
# ============================================================================

pro:
	@./toolchain/compile-with-meson.sh pro

ham:
	@./toolchain/compile-with-meson.sh ham

survive:
	@./toolchain/compile-with-meson.sh survive

explorer:
	@./toolchain/compile-with-meson.sh explorer

barebones:
	@./toolchain/compile-with-meson.sh barebones

all-presets:
	@./toolchain/compile-with-meson.sh All

# ============================================================================
# Configuration UI
# ============================================================================

# Interactive build configurator
menuconfig:
	@python3 toolchain/menuconfig.py

config: menuconfig

# ============================================================================
# Aliases
# ============================================================================

release: default
debug:
	@./toolchain/compile-with-meson.sh default --buildtype=debug

# ============================================================================
# Flashing
# ============================================================================

# Port is auto-detected by tools/flash.py unless specified
# PORT = /dev/ttyUSB0 

flash:
	@python3 toolchain/flasher.py $(if $(PORT),--port $(PORT),) $(if $(FILE),$(FILE),)


# ============================================================================
# Utilities
# ============================================================================

IMAGE=uvk1-uvk5v3

clean:
	docker run --rm -v "$(PWD)":/src -w /src $(IMAGE) rm -rf build build_meson
	rm -f *.bin *.hex *.packed.bin

help:
	@echo -e ""
	@echo -e "  ⚡ ${BOLD}${CYAN}deltafw Build System${RESET}"
	@echo -e "  ══════════════════════════════════════════════════════════"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Quick Start:${RESET}"
	@echo -e "    ${GREEN}make default${RESET}      Standard balanced build (daily use)"
	@echo -e "    ${GREEN}make dev${RESET}          Developer build (experimental, all features)"
	@echo -e "    ${GREEN}make menuconfig${RESET}   Interactive build configurator"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Professional Presets:${RESET}"
	@echo -e "    ${CYAN}make pro${RESET}          Security focus, stealth comms"
	@echo -e "    ${CYAN}make ham${RESET}          Amateur Radio focus, full features"
	@echo -e "    ${CYAN}make survive${RESET}      Emergency & Survival focus"
	@echo -e "    ${CYAN}make explorer${RESET}     Signals Intelligence & Scanning"
	@echo -e "    ${CYAN}make barebones${RESET}    Minimal setup, maximum stability"
	@echo -e "    ${CYAN}make all-presets${RESET}  Build all available presets"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Flashing:${RESET}"
	@echo -e "    ${YELLOW}make flash${RESET}       Flash the most recent binary to radio"
	@echo -e "    ${GRAY}Usage: make flash [PORT=/dev/ttyUSBx] [FILE=path/to/bin]${RESET}"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Utilities:${RESET}"
	@echo -e "    ${WHITE}make clean${RESET}       Remove all build artifacts"
	@echo -e "    ${WHITE}make debug${RESET}       Build default with debug symbols"
	@echo -e ""
