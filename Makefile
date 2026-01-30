# deltafw Makefile
# User-friendly build targets

.PHONY: all fusion custom menuconfig config help clean \
        bandscope broadcast basic rescue game all-presets release debug

# Default target - show help
all: help

# ============================================================================
# Quick Build Targets
# ============================================================================

# Recommended for new UV-K1/K5 V3 radios with expanded flash
fusion:
	./compile-with-docker.sh Fusion

# Standard balanced build
custom:
	./compile-with-docker.sh Custom

# ============================================================================
# Preset Builds
# ============================================================================

bandscope:
	./compile-with-docker.sh Bandscope

broadcast:
	./compile-with-docker.sh Broadcast

basic:
	./compile-with-docker.sh Basic

rescue:
	./compile-with-docker.sh RescueOps

game:
	./compile-with-docker.sh Game

all-presets:
	./compile-with-docker.sh All

# ============================================================================
# Configuration UI
# ============================================================================

# Interactive build configurator
menuconfig:
	@python3 tools/menuconfig.py

config: menuconfig

# ============================================================================
# Aliases
# ============================================================================

release: fusion
debug:
	./compile-with-docker.sh Fusion -DCMAKE_BUILD_TYPE=Debug

# ============================================================================
# Utilities
# ============================================================================

IMAGE=uvk1-uvk5v3

clean:
	docker run --rm -v "$(PWD)":/src -w /src $(IMAGE) rm -rf build
	rm -f *.bin *.hex *.packed.bin

help:
	@echo ""
	@echo "  ⚡ deltafw Build System"
	@echo "  ══════════════════════════════════════════════════════════"
	@echo ""
	@echo "  Quick Start:"
	@echo "    make fusion      Build Fusion (recommended for new radios)"
	@echo "    make custom      Build Custom (balanced features)"
	@echo "    make menuconfig  Interactive build configurator"
	@echo ""
	@echo "  Presets:"
	@echo "    make bandscope   Spectrum analyzer focused"
	@echo "    make broadcast   FM radio and comms focused"
	@echo "    make basic       Minimal stable build"
	@echo "    make rescue      Emergency operations"
	@echo "    make game        Includes Breakout game"
	@echo "    make all-presets Build all presets"
	@echo ""
	@echo "  Utilities:"
	@echo "    make clean       Remove build artifacts"
	@echo "    make debug       Build with debug symbols"
	@echo ""
