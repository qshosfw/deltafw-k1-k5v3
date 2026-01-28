# Makefile wrapper for compile-with-docker.sh

# Default target
all: custom

# Presets
custom:
	./compile-with-docker.sh Custom

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

fusion:
	./compile-with-docker.sh Fusion

all-presets:
	./compile-with-docker.sh All

# Aliases
release: custom
debug:
	./compile-with-docker.sh Custom -DCMAKE_BUILD_TYPE=Debug

# Clean
IMAGE=uvk1-uvk5v3

clean:
	docker run --rm -v "$(PWD)":/src -w /src $(IMAGE) rm -rf build
	rm -f *.bin *.hex *.packed.bin

.PHONY: all custom bandscope broadcast basic rescue game fusion all-presets release debug clean
