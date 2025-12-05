TARGET=n64brew-gamejam-6
BUILD_DIR=build
include $(N64_INST)/include/n64.mk

src = main.c

#N64_CFLAGS = -Wno-error

all: $(TARGET).z64

$(BUILD_DIR)/$(TARGET).elf: $(src:%.c=$(BUILD_DIR)/%.o)

$(TARGET).z64: N64_ROM_TITLE="N64brew Gamejam 2025"

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean

