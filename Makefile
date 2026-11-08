
ifndef NT_API_PATH
	NT_API_PATH := distingNT_API
endif

INCLUDE_PATH := $(NT_API_PATH)/include

PLUGIN_DIR := plugins
PLUGIN_O := $(PLUGIN_DIR)/dnb_seq.o

inputs := $(wildcard *cpp)
outputs := $(patsubst %.cpp,plugins/%.o,$(inputs))

# Get version from git tag, fallback to "dev" if not available
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")

all: $(outputs)

clean:
	rm -f $(outputs)

plugins/%.o: %.cpp
	mkdir -p $(@D)
	arm-none-eabi-c++ -std=gnu++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fno-pic -Wno-reorder -Wall -MMD -MP -ffunction-sections -fdata-sections -I$(INCLUDE_PATH) -DVERSION=\"$(VERSION)\" -c -o $@ $^

# Native host build for nt_emu / VCV Rack (not part of the hardware build)
HOST_CXX ?= clang++
HOST_OS := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
	HOST_DYLIB_EXT := dylib
	HOST_DYLIB_LDFLAGS := -dynamiclib -undefined dynamic_lookup
else
	HOST_DYLIB_EXT := so
	HOST_DYLIB_LDFLAGS := -shared
endif
HOST_DYLIB := $(PLUGIN_DIR)/dnb_seq.$(HOST_DYLIB_EXT)
HOST_DYLIB_FLAGS := -std=gnu++17 -O2 -fPIC -Wall -Wno-reorder -fno-rtti -fno-exceptions

dylib: $(HOST_DYLIB)

$(HOST_DYLIB): dnb_seq.cpp
	mkdir -p $(@D)
	$(HOST_CXX) $(HOST_DYLIB_FLAGS) -I$(INCLUDE_PATH) -DVERSION=\"$(VERSION)\" \
		$(HOST_DYLIB_LDFLAGS) -o $@ $<
	@echo "Built host plugin: $@"

check: all
	@echo "Checking for undefined symbols in $(PLUGIN_O)…"
	@arm-none-eabi-nm $(PLUGIN_O) | grep ' U ' || \
	    echo "No undefined symbols found (or grep failed)."
	@echo "(Only _NT_* plus memcpy/memmove/memset should remain undefined.)"

	@echo "Checking .bss footprint…"
		@bss=$$(arm-none-eabi-size -B $(PLUGIN_O) | awk 'NR==2 {print $$3}'); \
			printf ".bss size = %s bytes\n" "$$bss"; \
			if [ "$$bss" -gt 8192 ]; then \
				echo "❌  .bss exceeds 8 KiB limit!  Loader will reject the plug-in."; \
				exit 1; \
			else \
				echo "✅  .bss within limit."; \
			fi

.PHONY: all clean check dylib
