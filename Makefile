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

.PHONY: all fusion custom menuconfig config help clean \
        bandscope broadcast basic rescue game all-presets release debug \
        gcc-fusion gcc-custom gcc-bandscope gcc-broadcast gcc-basic gcc-rescue gcc-game gcc-all-presets gcc-debug

# Default target - show help
all: help

# ============================================================================
# Quick Build Targets (Zig — default)
# ============================================================================

# Recommended for new UV-K1/K5 V3 radios with expanded flash
fusion:
	@./toolchain/compile-with-zig.sh Fusion

# Standard balanced build
custom:
	@./toolchain/compile-with-zig.sh Custom

# ============================================================================
# Preset Builds (Zig)
# ============================================================================

bandscope:
	@./toolchain/compile-with-zig.sh Bandscope

broadcast:
	@./toolchain/compile-with-zig.sh Broadcast

basic:
	@./toolchain/compile-with-zig.sh Basic

rescue:
	@./toolchain/compile-with-zig.sh RescueOps

game:
	@./toolchain/compile-with-zig.sh Game

all-presets:
	@./toolchain/compile-with-zig.sh All

# ============================================================================
# GCC Build Targets (Docker + Meson, legacy)
# ============================================================================

gcc-fusion:
	@./toolchain/compile-with-meson.sh Fusion

gcc-custom:
	@./toolchain/compile-with-meson.sh Custom

gcc-bandscope:
	@./toolchain/compile-with-meson.sh Bandscope

gcc-broadcast:
	@./toolchain/compile-with-meson.sh Broadcast

gcc-basic:
	@./toolchain/compile-with-meson.sh Basic

gcc-rescue:
	@./toolchain/compile-with-meson.sh RescueOps

gcc-game:
	@./toolchain/compile-with-meson.sh Game

gcc-all-presets:
	@./toolchain/compile-with-meson.sh All

gcc-debug:
	@./toolchain/compile-with-meson.sh Fusion --buildtype=debug

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

release: fusion
debug:
	@./toolchain/compile-with-zig.sh Fusion

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
	-docker run --rm -v "$(PWD)":/src -w /src $(IMAGE) rm -rf build build_meson 2>/dev/null || true
	rm -rf build zig-out .zig-cache
	rm -f *.bin *.hex *.packed.bin

help:
	@echo -e ""
	@echo -e "  ⚡ ${BOLD}${CYAN}deltafw Build System${RESET}"
	@echo -e "  ══════════════════════════════════════════════════════════"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Quick Start:${RESET}"
	@echo -e "    ${GREEN}make fusion${RESET}         Build Fusion (recommended)"
	@echo -e "    ${GREEN}make custom${RESET}         Build Custom (balanced features)"
	@echo -e "    ${GREEN}make menuconfig${RESET}     Interactive build configurator"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Preset Builds:${RESET}"
	@echo -e "    ${CYAN}make bandscope${RESET}      Spectrum analyzer focused"
	@echo -e "    ${CYAN}make broadcast${RESET}      FM radio and comms focused"
	@echo -e "    ${CYAN}make basic${RESET}          Minimal stable build"
	@echo -e "    ${CYAN}make rescue${RESET}         Emergency operations"
	@echo -e "    ${CYAN}make game${RESET}           Includes Breakout game"
	@echo -e "    ${CYAN}make all-presets${RESET}    Build all available presets"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}GCC Legacy Builds:${RESET}  ${GRAY}(Docker + Meson + arm-none-eabi-gcc)${RESET}"
	@echo -e "    ${YELLOW}make gcc-fusion${RESET}     Build Fusion with GCC"
	@echo -e "    ${YELLOW}make gcc-custom${RESET}     Build Custom with GCC"
	@echo -e "    ${YELLOW}make gcc-debug${RESET}      Build Fusion with debug symbols"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Flashing:${RESET}"
	@echo -e "    ${GREEN}make flash${RESET}          Flash the most recent binary to radio"
	@echo -e "    ${GRAY}Usage: make flash [PORT=/dev/ttyUSBx] [FILE=path/to/bin]${RESET}"
	@echo -e ""
	@echo -e "  ${BOLD}${BLUE}Utilities:${RESET}"
	@echo -e "    ${WHITE}make clean${RESET}          Remove all build artifacts"
	@echo -e ""
