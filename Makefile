TARGET=n64brew-gamejam-6
BUILD_DIR=build
N64_ROM_HEADER=ipl3_dev_patched.z64
include $(N64_INST)/include/n64.mk

src = main.c entrypoint.S

#N64_CFLAGS = -Wno-error

N64_LDFLAGS := -Theaps.ld $(N64_LDFLAGS)

all: $(TARGET).z64

$(BUILD_DIR)/$(TARGET).elf: $(src:%.c=$(BUILD_DIR)/%.o) $(src:%.S=$(BUILD_DIR)/%.o)

$(TARGET).z64: N64_ROM_TITLE="N64brew Gamejam 2025"

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean

