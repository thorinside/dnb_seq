
ifndef NT_API_PATH
	NT_API_PATH := distingNT_API
endif

INCLUDE_PATH := $(NT_API_PATH)/include

PLUGIN_DIR := plugins
PLUGIN_O := $(PLUGIN_DIR)/dnb_seq.o

inputs := $(wildcard *cpp)
outputs := $(patsubst %.cpp,plugins/%.o,$(inputs))

all: $(outputs)

clean:
	rm -f $(outputs)

plugins/%.o: %.cpp
	mkdir -p $(@D)
	arm-none-eabi-c++ -std=gnu++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fno-pic -Wno-reorder -Wall -MMD -MP -ffunction-sections -fdata-sections -I$(INCLUDE_PATH) -c -o $@ $^

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

.PHONY: all clean check
